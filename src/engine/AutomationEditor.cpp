#include "engine/AutomationEditor.h"
#include "engine/AudioEngine.h"
#include "engine/SendBuses.h"
#include "engine/SoundControls.h"
#include "engine/TrackHelpers.h"
#include <algorithm>

namespace te = tracktion;

namespace ikaros
{

using namespace detail;

namespace
{
    // The lane's settings, on the track's state (saved with the session).
    const juce::Identifier laneShownProp  ("ikarosAutomationShown");
    const juce::Identifier laneTargetProp ("ikarosAutomationTarget");

    // A plug-in parameter's name, stored on its curve so a missing plug-in's
    // automation can still be named in the picker.
    const juce::Identifier curveNameProp ("ikarosName");

    const juce::String paramPrefix ("param:");

    juce::String percent (float v)    { return juce::String (juce::roundToInt (v * 100.0f)) + "%"; }

    te::AuxSendPlugin* findSend (te::AudioTrack& t, int bus)
    {
        for (auto* p : t.pluginList)
            if (auto* send = dynamic_cast<te::AuxSendPlugin*> (p))
                if (send->getBusNumber() == bus)
                    return send;

        return nullptr;
    }

    // The third-party plug-ins on a track: its instrument and effects.
    std::vector<te::ExternalPlugin*> thirdPartyPluginsOn (te::AudioTrack& t)
    {
        std::vector<te::ExternalPlugin*> result;

        for (auto* p : t.pluginList)
            if (auto* external = dynamic_cast<te::ExternalPlugin*> (p))
                result.push_back (external);

        return result;
    }

    bool isUnavailable (te::ExternalPlugin& p)    { return p.isMissing() || p.isDisabled(); }

    juce::String pluginName (te::ExternalPlugin& p)
    {
        // A plug-in that never loaded still has the name it was saved with.
        auto name = p.getName();
        return name.isNotEmpty() ? name : p.state.getProperty (te::IDs::name).toString();
    }

    juce::String paramTargetId (te::Plugin& p, const juce::String& paramID)
    {
        return paramPrefix + juce::String (p.itemID.getRawID()) + ":" + paramID;
    }

    // Splits "param:<plugin>:<parameter>". (Parameter ids can contain ':',
    // so only the first one after the plug-in id counts.)
    bool parseParamTarget (const juce::String& id, PluginId& plugin, juce::String& paramID)
    {
        if (! id.startsWith (paramPrefix))
            return false;

        auto rest = id.substring (paramPrefix.length());
        plugin = (PluginId) rest.upToFirstOccurrenceOf (":", false, false).getLargeIntValue();
        paramID = rest.fromFirstOccurrenceOf (":", false, false);
        return plugin != 0 && paramID.isNotEmpty();
    }

    te::ExternalPlugin* findThirdPartyPlugin (te::AudioTrack& t, PluginId id)
    {
        for (auto* p : thirdPartyPluginsOn (t))
            if (p->itemID.getRawID() == id)
                return p;

        return nullptr;
    }

    // The curves stored on a plug-in that has no parameters to own them
    // (a missing one): the raw AUTOMATIONCURVE trees, by parameter id.
    std::vector<juce::ValueTree> storedCurvesOf (te::Plugin& p)
    {
        std::vector<juce::ValueTree> result;

        for (auto child : p.state)   // ValueTree iterates over its children
            if (child.hasType (te::IDs::AUTOMATIONCURVE) && child.getNumChildren() > 0
                 && child.getProperty (te::IDs::paramID).toString().isNotEmpty()
                 && p.getAutomatableParameterByID (child.getProperty (te::IDs::paramID).toString()) == nullptr)
                result.push_back (child);

        return result;
    }

    juce::ValueTree storedCurve (te::Plugin& p, const juce::String& paramID)
    {
        for (auto& curve : storedCurvesOf (p))
            if (curve.getProperty (te::IDs::paramID).toString() == paramID)
                return curve;

        return {};
    }
}

//==============================================================================
// A target, found on its track: the Tracktion parameter behind it, and how
// its control's 0..1 maps onto the parameter's own values.
struct AutomationEditor::Resolved
{
    AutomationTarget target;
    te::AudioTrack* track = nullptr;
    te::AutomatableParameter* parameter = nullptr;   // nullptr for a missing plug-in's (see storedCurve)
    juce::ValueTree storedCurve;
    std::function<float (float)> toParameter = [] (float v) { return v; };
    std::function<float (float)> fromParameter = [] (float v) { return v; };
    std::function<juce::String (float)> describe = percent;
    float valueWithoutParameter = 0.0f;   // what the control shows while there's no parameter yet
    juce::Identifier stateProperty;       // where the parameter's plugin stores its value (empty if unknown)
};

AutomationEditor::AutomationEditor (AudioEngine& o, SendBuses& s) : owner (o), sends (s)
{
}

std::optional<AutomationEditor::Resolved> AutomationEditor::resolve (TrackId trackId, const juce::String& id, bool createIfNeeded) const
{
    auto* t = findTrack (owner.getEdit(), trackId);

    if (t == nullptr)
        return std::nullopt;

    Resolved r;
    r.track = t;
    r.target.id = id;
    r.target.group = "Mixer";

    if (id == automationTargets::volume || id == automationTargets::pan)
    {
        auto* vol = t->getVolumePlugin();

        if (vol == nullptr)
            return std::nullopt;

        if (id == automationTargets::volume)
        {
            r.target.name = "Volume";
            r.parameter = vol->volParam.get();
            r.stateProperty = te::IDs::volume;
            r.describe = [] (float v) { return juce::String (te::volumeFaderPositionToDB (v), 1) + " dB"; };
        }
        else
        {
            // The pan knob goes -1..1; the lane goes 0..1 with the centre in the middle.
            r.target.name = "Pan";
            r.parameter = vol->panParam.get();
            r.stateProperty = te::IDs::pan;
            r.toParameter = [] (float v) { return v * 2.0f - 1.0f; };
            r.fromParameter = [] (float pan) { return (pan + 1.0f) * 0.5f; };
            r.describe = [] (float v)
            {
                const int amount = juce::roundToInt (std::abs (v * 2.0f - 1.0f) * 100.0f);
                return amount == 0 ? juce::String ("Center") : (v < 0.5f ? "L " : "R ") + juce::String (amount);
            };
        }
    }
    else if (id.startsWith ("send:"))
    {
        const int bus = id.fromFirstOccurrenceOf (":", false, false).getIntValue();

        if (! juce::isPositiveAndBelow (bus, SendBuses::numBuses))
            return std::nullopt;

        if (createIfNeeded)
            sends.createSend (trackId, bus);

        r.target.name = SendBuses::getBusName (bus) + " Send";

        if (auto* send = findSend (*t, bus))
            r.parameter = send->gain.get();

        r.stateProperty = te::IDs::auxSendSliderPos;

        // A send level of 1 is 0 dB, a little below the top of the send's fader.
        const float full = SendBuses::getFullLevelPosition();
        r.toParameter = [full] (float v) { return v * full; };
        r.fromParameter = [full] (float pos) { return juce::jlimit (0.0f, 1.0f, pos / full); };
    }
    else if (id.startsWith ("sound:"))
    {
        const int index = id.fromFirstOccurrenceOf (":", false, false).getIntValue();
        auto knobs = getSoundControlParameters (*t, owner.getSounds(), createIfNeeded);

        if (! juce::isPositiveAndBelow (index, (int) knobs.size()))
            return std::nullopt;

        auto& knob = knobs[(size_t) index];
        r.target.name = knob.name;
        r.target.group = "Sound";
        r.parameter = knob.parameter;
        r.toParameter = knob.toParameter;
        r.fromParameter = knob.fromParameter;
        r.describe = knob.describe;
        r.valueWithoutParameter = knob.valueWithoutParameter;
        r.stateProperty = knob.stateProperty;
    }
    else
    {
        PluginId pluginId = 0;
        juce::String paramID;

        if (! parseParamTarget (id, pluginId, paramID))
            return std::nullopt;

        auto* plugin = findThirdPartyPlugin (*t, pluginId);

        if (plugin == nullptr)
            return std::nullopt;

        r.target.group = pluginName (*plugin);

        if (auto param = plugin->getAutomatableParameterByID (paramID))
        {
            r.parameter = param.get();
            r.target.name = param->getParameterName();
            r.toParameter = [p = param.get()] (float v) { return p->valueRange.convertFrom0to1 (v); };
            r.fromParameter = [p = param.get()] (float raw) { return p->valueRange.convertTo0to1 (raw); };
            r.describe = [p = param.get()] (float v) { return p->valueToString (p->valueRange.convertFrom0to1 (v)); };
        }
        else
        {
            // A missing plug-in: its automation is kept as it was saved
            // (third-party parameters store 0..1 values), but can't change.
            r.storedCurve = storedCurve (*plugin, paramID);

            if (! r.storedCurve.isValid())
                return std::nullopt;

            r.target.name = r.storedCurve.getProperty (curveNameProp, paramID).toString();
            r.target.editable = false;
            r.target.automated = true;
            return r;
        }
    }

    // A send or a hidden Sound plug-in that isn't there yet has nothing to
    // automate, but can still be picked (it's made when the first point is).
    r.target.automated = r.parameter != nullptr && r.parameter->getCurve().getNumPoints() > 0;
    return r;
}

//==============================================================================
std::vector<AutomationTarget> AutomationEditor::getTargets (TrackId trackId) const
{
    std::vector<AutomationTarget> result;
    auto* t = findTrack (owner.getEdit(), trackId);

    if (t == nullptr)
        return result;

    auto add = [&] (const juce::String& id)
    {
        if (auto r = resolve (trackId, id, false))
            result.push_back (r->target);
    };

    add (automationTargets::volume);
    add (automationTargets::pan);

    for (int bus = 0; bus < SendBuses::numBuses; ++bus)
        add (automationTargets::send (bus));

    const auto numKnobs = (int) getSoundControlParameters (*t, owner.getSounds(), false).size();

    for (int i = 0; i < numKnobs; ++i)
        add (automationTargets::soundControl (i));

    for (auto* plugin : thirdPartyPluginsOn (*t))
    {
        if (isUnavailable (*plugin))
        {
            for (auto& curve : storedCurvesOf (*plugin))
                add (paramTargetId (*plugin, curve.getProperty (te::IDs::paramID).toString()));
        }
        else
        {
            for (auto* param : plugin->getAutomatableParameters())
                add (paramTargetId (*plugin, param->paramID));
        }
    }

    return result;
}

std::optional<AutomationTarget> AutomationEditor::getTarget (TrackId trackId, const juce::String& id) const
{
    if (auto r = resolve (trackId, id, false))
        return r->target;

    return std::nullopt;
}

juce::StringArray AutomationEditor::getAutomatedTargets (TrackId trackId) const
{
    juce::StringArray result;
    auto* t = findTrack (owner.getEdit(), trackId);

    if (t == nullptr)
        return result;

    // This runs for every track each time the UI re-reads them (many times
    // a second while automation plays), and looking up each target is slow:
    // first see whether anything on the track has automation at all.
    bool anyPoints = false;

    for (auto plugin : t->pluginList)
        for (auto* param : plugin->getAutomatableParameters())
            anyPoints = anyPoints || param->hasAutomationPoints();

    if (! anyPoints)
        return result;

    // Only the mixer's and the Sound tab's: those are the controls the app
    // draws itself (a plug-in's own window moves its own knobs).
    juce::StringArray ids { automationTargets::volume, automationTargets::pan };

    for (int bus = 0; bus < SendBuses::numBuses; ++bus)
        ids.add (automationTargets::send (bus));

    const auto numKnobs = (int) getSoundControlParameters (*t, owner.getSounds(), false).size();

    for (int i = 0; i < numKnobs; ++i)
        ids.add (automationTargets::soundControl (i));

    for (auto& id : ids)
        if (auto r = resolve (trackId, id, false); r && r->target.automated)
            result.add (id);

    return result;
}

//==============================================================================
void AutomationEditor::setLaneShown (TrackId trackId, bool shown)
{
    // Not an undo step (nullptr): showing a lane changes the view, not the song.
    if (auto* t = findTrack (owner.getEdit(), trackId))
        t->state.setProperty (laneShownProp, shown, nullptr);
}

bool AutomationEditor::isLaneShown (TrackId trackId) const
{
    if (auto* t = findTrack (owner.getEdit(), trackId))
        return t->state.getProperty (laneShownProp, false);

    return false;
}

void AutomationEditor::setAllLanesShown (bool shown)
{
    auto* edit = owner.getEdit();

    if (edit == nullptr)
        return;

    for (auto* t : getUserTracks (*edit))
    {
        const auto id = t->itemID.getRawID();

        const auto current = getTarget (id, getLaneTarget (id));

        if (shown && ! (current && current->automated))
        {
            const auto targets = getTargets (id);
            auto automated = std::find_if (targets.begin(), targets.end(), [] (const AutomationTarget& a) { return a.automated; });

            if (automated != targets.end())
                setLaneTarget (id, automated->id);
        }

        setLaneShown (id, shown);
    }
}

bool AutomationEditor::areAllLanesShown() const
{
    auto* edit = owner.getEdit();

    if (edit == nullptr)
        return false;

    auto tracks = getUserTracks (*edit);
    return ! tracks.isEmpty()
            && std::all_of (tracks.begin(), tracks.end(), [this] (te::AudioTrack* t) { return isLaneShown (t->itemID.getRawID()); });
}

void AutomationEditor::setLaneTarget (TrackId trackId, const juce::String& id)
{
    if (auto* t = findTrack (owner.getEdit(), trackId))
        t->state.setProperty (laneTargetProp, id, nullptr);
}

juce::String AutomationEditor::getLaneTarget (TrackId trackId) const
{
    auto* t = findTrack (owner.getEdit(), trackId);

    if (t == nullptr)
        return {};

    // A target that's gone (its plug-in was removed) falls back to volume.
    const auto id = t->state.getProperty (laneTargetProp).toString();
    return id.isNotEmpty() && resolve (trackId, id, false).has_value() ? id : automationTargets::volume;
}

//==============================================================================
std::vector<AutomationPoint> AutomationEditor::getPoints (TrackId trackId, const juce::String& id) const
{
    std::vector<AutomationPoint> points;
    auto r = resolve (trackId, id, false);

    if (! r)
        return points;

    auto& edit = r->track->edit;

    if (r->parameter != nullptr)
    {
        auto& curve = r->parameter->getCurve();

        for (int i = 0; i < curve.getNumPoints(); ++i)
            points.push_back ({ te::toBeats (curve.getPointPosition (i), edit.tempoSequence).inBeats(),
                                juce::jlimit (0.0f, 1.0f, r->fromParameter (curve.getPointValue (i))) });
    }
    else
    {
        for (auto point : r->storedCurve)
            points.push_back ({ timeToBeat (edit, te::TimePosition::fromSeconds ((double) point.getProperty (te::IDs::t))),
                                juce::jlimit (0.0f, 1.0f, (float) point.getProperty (te::IDs::v)) });
    }

    return points;
}

bool AutomationEditor::setPoints (TrackId trackId, const juce::String& id, std::vector<AutomationPoint> points)
{
    auto r = resolve (trackId, id, ! points.empty());

    if (! r || ! r->target.editable || r->parameter == nullptr)
        return false;

    for (auto& p : points)
    {
        p.beat = juce::jmax (0.0, p.beat);
        p.value = juce::jlimit (0.0f, 1.0f, p.value);
    }

    // stable_sort keeps points on the same beat in the order given, so a
    // sudden jump (two points on one beat) stays the way it was drawn.
    std::stable_sort (points.begin(), points.end(),
                      [] (const AutomationPoint& a, const AutomationPoint& b) { return a.beat < b.beat; });

    auto& curve = r->parameter->getCurve();
    auto* um = &r->track->edit.getUndoManager();
    auto& tempo = r->track->edit.tempoSequence;

    if ((int) points.size() == curve.getNumPoints())
    {
        // The same number of points (a drag): change the ones that moved, in
        // place, so a long drag doesn't pile up removals and additions.
        for (int i = 0; i < (int) points.size(); ++i)
        {
            const auto& p = points[(size_t) i];
            const double oldBeat = te::toBeats (curve.getPointPosition (i), tempo).inBeats();

            if (std::abs (oldBeat - p.beat) > 1.0e-9)
                curve.setPointPosition (i, te::BeatPosition::fromBeats (p.beat), um);

            const float raw = r->toParameter (p.value);

            if (curve.getPointValue (i) != raw)
                curve.setPointValue (i, raw, um);
        }
    }
    else
    {
        curve.clear (um);

        for (auto& p : points)
            curve.addPoint (te::BeatPosition::fromBeats (p.beat), r->toParameter (p.value), 0.0f, um);
    }

    // Remember a plug-in parameter's name, for when the plug-in is missing.
    if (id.startsWith (paramPrefix) && curve.state.isValid() && ! points.empty()
         && curve.state.getProperty (curveNameProp).toString() != r->target.name)
        curve.state.setProperty (curveNameProp, r->target.name, um);

    return true;
}

float AutomationEditor::getCurrentValue (TrackId trackId, const juce::String& id) const
{
    auto r = resolve (trackId, id, false);

    if (! r)
        return 0.0f;

    if (r->parameter == nullptr)
        return r->valueWithoutParameter;   // e.g. a send that was never turned up

    return juce::jlimit (0.0f, 1.0f, r->fromParameter (r->parameter->getCurrentValue()));
}

juce::String AutomationEditor::describeValue (TrackId trackId, const juce::String& id, float value) const
{
    if (auto r = resolve (trackId, id, false))
        return r->describe (juce::jlimit (0.0f, 1.0f, value));

    return {};
}

//==============================================================================
void AutomationEditor::keepUnloadedCurvesOnBeats (const std::function<void()>& changeTempo)
{
    auto* edit = owner.getEdit();

    struct StoredPoint { juce::ValueTree point; double beat; };
    std::vector<StoredPoint> stored;

    if (edit != nullptr)
        for (auto* plugin : te::getAllPlugins (*edit, false))
            for (auto& curve : storedCurvesOf (*plugin))
                for (auto point : curve)
                    stored.push_back ({ point, timeToBeat (*edit, te::TimePosition::fromSeconds ((double) point.getProperty (te::IDs::t))) });

    changeTempo();

    // Into the tempo change's undo step, so undo puts them back too.
    for (auto& s : stored)
        s.point.setProperty (te::IDs::t, beatToTime (*edit, s.beat).inSeconds(), &edit->getUndoManager());
}

void AutomationEditor::keepValuesWhenAutomationGoes (const std::function<void()>& undoOrRedo)
{
    // What's automated now, and the value each setting was given by hand.
    // (Following a curve doesn't change the "explicit" value, only the
    // stored one.)
    struct Automated { TrackId track; juce::String id; float value; };
    std::vector<Automated> automated;

    if (auto* edit = owner.getEdit())
        for (auto* t : te::getAudioTracks (*edit))
            for (auto& id : getAutomatedTargets (t->itemID.getRawID()))
                if (auto r = resolve (t->itemID.getRawID(), id, false); r && r->parameter != nullptr)
                    automated.push_back ({ t->itemID.getRawID(), id, r->parameter->getCurrentExplicitValue() });

    undoOrRedo();

    for (auto& a : automated)
    {
        auto r = resolve (a.track, a.id, false);

        if (! r || r->parameter == nullptr || r->parameter->getCurve().getNumPoints() > 0
             || ! r->stateProperty.isValid() || r->parameter->getPlugin() == nullptr)
            continue;

        // Written straight to the state with no UndoManager: anything recorded
        // now would start a new undo step and throw away Redo. The parameter
        // listens to its stored value and follows.
        r->parameter->getPlugin()->state.setProperty (r->stateProperty, a.value, nullptr);
    }
}

} // namespace ikaros
