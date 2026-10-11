#include "engine/EffectsController.h"
#include "engine/AudioEngine.h"
#include "engine/PluginLibrary.h"
#include "engine/SfzPlugin.h"
#include "engine/SoundLibrary.h"
#include "engine/TrackHelpers.h"
#include <array>

namespace te = tracktion;

namespace ikaros
{

using namespace detail;

namespace
{
    // The built-in effects offered in the "+ Add effect" menu, with the names
    // and groups the user sees. Order here = order in the menu.
    const std::vector<EffectType>& builtInEffects()
    {
        static const std::vector<EffectType> effects {
            { te::CompressorPlugin::xmlTypeName, "Compressor",            "Dynamics" },
            { te::EqualiserPlugin::xmlTypeName,  "EQ",                    "EQ & Filter" },
            { te::LowPassPlugin::xmlTypeName,    "Low/High-Pass Filter",  "EQ & Filter" },
            { te::ReverbPlugin::xmlTypeName,     "Reverb",                "Space" },
            { te::DelayPlugin::xmlTypeName,      "Echo",                  "Space" },
            { te::ChorusPlugin::xmlTypeName,     "Chorus",                "Modulation" },
            { te::PhaserPlugin::xmlTypeName,     "Phaser",                "Modulation" },
            { te::PitchShiftPlugin::xmlTypeName, "Pitch Shift",           "Pitch" },
        };
        return effects;
    }

    juce::String displayNameFor (te::Plugin& p)
    {
        for (auto& e : builtInEffects())
            if (e.type == p.getPluginType())
                return e.name;

        if (dynamic_cast<te::FourOscPlugin*> (&p) != nullptr)
            return "4OSC Synth";

        if (dynamic_cast<te::SamplerPlugin*> (&p) != nullptr || dynamic_cast<SfzPlugin*> (&p) != nullptr)
            return "Sampler";

        return p.getName();
    }

    // Effects are every plugin on the track except the instrument, the
    // track's own volume/meter plugins (which Tracktion keeps at the end),
    // the sends to the shared buses (see SendBuses) and the EQ tab's
    // equaliser (see EqController).
    bool isEffect (te::Plugin& p)
    {
        return ! isInstrumentPlugin (p)
            && ! isEqPlugin (p)
            && dynamic_cast<te::VolumeAndPanPlugin*> (&p) == nullptr
            && dynamic_cast<te::LevelMeterPlugin*> (&p) == nullptr
            && dynamic_cast<te::AuxSendPlugin*> (&p) == nullptr
            && dynamic_cast<te::AuxReturnPlugin*> (&p) == nullptr;
    }

    // A track's effects, or the master's (see findPluginList).
    std::vector<te::Plugin*> effectsOn (te::PluginList& list)
    {
        std::vector<te::Plugin*> result;

        for (auto* p : list)
            if (isEffect (*p))
                result.push_back (p);

        return result;
    }

    std::vector<te::Plugin*> instrumentsOn (te::AudioTrack& t)
    {
        std::vector<te::Plugin*> result;

        for (auto* p : t.pluginList)
            if (isInstrumentPlugin (*p))
                result.push_back (p);

        return result;
    }

    te::Plugin* findEffect (te::PluginList& list, PluginId id)
    {
        for (auto* p : effectsOn (list))
            if (p->itemID.getRawID() == id)
                return p;

        return nullptr;
    }

    PluginSlot makeSlot (te::Plugin& p)
    {
        PluginSlot slot { p.itemID.getRawID(), getPluginTypeString (p), displayNameFor (p), p.isEnabled() };

        if (auto* external = dynamic_cast<te::ExternalPlugin*> (&p))
        {
            slot.isThirdParty = true;
            slot.missing = external->isMissing() || external->isDisabled();
            slot.manufacturer = external->getVendor();

            // A plug-in that never loaded still has the name it was saved with.
            if (slot.name.isEmpty())
                slot.name = p.state.getProperty (te::IDs::name).toString();
        }

        return slot;
    }

    // A 4OSC synth with a patch's settings. They go into its state before it's
    // created, so building it isn't a pile of separate undo steps.
    te::Plugin::Ptr createSynth (te::Edit& edit, const SoundLibrary::SynthPatch& patch)
    {
        auto state = te::createValueTree (te::IDs::PLUGIN, te::IDs::type, te::FourOscPlugin::xmlTypeName);

        for (auto& [property, value] : patch.settings)
        {
            // Whole numbers (wave shapes, on/off switches) are stored as ints,
            // the way the synth stores them itself.
            if (value == std::floor (value))
                state.setProperty (property, (int) value, nullptr);
            else
                state.setProperty (property, value, nullptr);
        }

        return edit.getPluginCache().createNewPlugin (state);
    }

    // Tracktion's sampler, with one SOUND per sample zone.
    te::Plugin::Ptr createSampler (te::Edit& edit, const SoundLibrary::SampledInstrument& instrument)
    {
        auto state = te::createValueTree (te::IDs::PLUGIN, te::IDs::type, te::SamplerPlugin::xmlTypeName);

        for (auto& zone : instrument.zones)
            state.appendChild (te::createValueTree (te::IDs::SOUND,
                                                    te::IDs::source, zone.file.getFullPathName(),
                                                    te::IDs::name, zone.file.getFileNameWithoutExtension(),
                                                    te::IDs::startTime, 0.0,
                                                    te::IDs::length, 0.0,
                                                    te::IDs::keyNote, zone.root,
                                                    te::IDs::minNote, zone.low,
                                                    te::IDs::maxNote, zone.high,
                                                    te::IDs::gainDb, zone.gainDb,
                                                    te::IDs::pan, 0.0f,
                                                    te::IDs::openEnded, instrument.oneShot),
                               nullptr);

        auto plugin = edit.getPluginCache().createNewPlugin (state);

        // The sampler loads its samples on the next message-loop turn; load them now.
        if (auto* sampler = dynamic_cast<te::SamplerPlugin*> (plugin.get()))
            sampler->flushPendingUpdates();

        return plugin;
    }

    // Our sfizz instrument, playing an SFZ file (see SfzPlugin). It loads
    // the file in the background, so this returns straight away.
    te::Plugin::Ptr createSfzInstrument (te::Edit& edit, const juce::File& sfzFile)
    {
        auto state = te::createValueTree (te::IDs::PLUGIN, te::IDs::type, SfzPlugin::xmlTypeName);
        state.setProperty (SfzPlugin::sfzFileProp, sfzFile.getFullPathName(), nullptr);
        return edit.getPluginCache().createNewPlugin (state);
    }

    // Where in the plugin list effect number `position` should go: before
    // the existing effect at that position, or (for "the end") just after
    // the last effect, or else before the EQ and the volume plugin.
    int pluginListIndexForPosition (te::PluginList& list, int position)
    {
        auto effects = effectsOn (list);

        if (juce::isPositiveAndBelow (position, (int) effects.size()))
            return list.indexOf (effects[(size_t) position]);

        if (! effects.empty())
            return list.indexOf (effects.back()) + 1;

        for (int i = 0; i < list.size(); ++i)
            if (isEqPlugin (*list[i]) || dynamic_cast<te::VolumeAndPanPlugin*> (list[i]) != nullptr)
                return i;

        return list.size();
    }

    juce::String percent (float v)    { return juce::String (juce::roundToInt (v * 100.0f)) + "%"; }

    // A few of Tracktion's built-in effects (Chorus, Phaser) keep their
    // settings as plain properties on their state rather than as
    // parameters, so they'd show no knobs. These are their settings, with
    // the ranges the knobs cover and the defaults Tracktion uses.
    struct StoredSetting
    {
        const char* pluginType;
        juce::Identifier property;
        const char* name;
        float minimum, maximum, defaultValue;
        const char* unit;   // "%" shows 0..1 as a percentage
    };

    const std::vector<StoredSetting>& storedSettings()
    {
        static const std::vector<StoredSetting> settings {
            { te::ChorusPlugin::xmlTypeName, te::IDs::depthMs,       "Depth",    0.5f, 10.0f, 3.0f, "ms" },
            { te::ChorusPlugin::xmlTypeName, te::IDs::speedHz,       "Speed",    0.1f,  5.0f, 1.0f, "Hz" },
            { te::ChorusPlugin::xmlTypeName, te::IDs::width,         "Width",    0.0f,  1.0f, 0.5f, "%" },
            { te::ChorusPlugin::xmlTypeName, te::IDs::mixProportion, "Mix",      0.0f,  1.0f, 0.5f, "%" },
            { te::PhaserPlugin::xmlTypeName, te::IDs::depth,         "Depth",    0.0f, 10.0f, 5.0f, "" },
            { te::PhaserPlugin::xmlTypeName, te::IDs::rate,          "Rate",     0.05f, 4.0f, 0.4f, "Hz" },
            { te::PhaserPlugin::xmlTypeName, te::IDs::feedback,      "Feedback", 0.0f,  0.95f, 0.7f, "%" },
        };
        return settings;
    }

    std::vector<const StoredSetting*> storedSettingsFor (te::Plugin& p)
    {
        std::vector<const StoredSetting*> result;

        for (auto& setting : storedSettings())
            if (p.getPluginType() == setting.pluginType)
                result.push_back (&setting);

        return result;
    }

    juce::String describeStored (const StoredSetting& s, float value)
    {
        if (juce::String (s.unit) == "%")
            return percent (value);

        return juce::String (value, 1) + (juce::String (s.unit).isEmpty() ? "" : " " + juce::String (s.unit));
    }

    juce::String seconds (float s)
    {
        return s < 1.0f ? juce::String (juce::roundToInt (s * 1000.0f)) + " ms"
                        : juce::String (s, 1) + " s";
    }

    // The knobs on the Sound tab, mapped onto 4OSC parameters.
    struct KnobMapping { const char* label; const char* paramID; };
    const std::array<KnobMapping, 5> knobMappings {{
        { "Brightness", "filterFreq" },
        { "Resonance",  "filterResonance" },
        { "Attack",     "ampAttack" },
        { "Release",    "ampRelease" },
        { "Room",       "reverbMix" },
    }};
}

//==============================================================================
EffectsController::EffectsController (AudioEngine& o) : owner (o)
{
}

//==============================================================================
std::vector<SoundControl> EffectsController::getSoundControls (TrackId id) const
{
    std::vector<SoundControl> result;
    auto* t = findTrack (owner.getEdit(), id);
    auto* synth = t != nullptr ? findSynth (*t) : nullptr;

    // The knobs control the built-in synth. Audio tracks, sampled instruments
    // and plug-in instruments have none.
    if (synth == nullptr)
        return result;

    for (auto& k : knobMappings)
    {
        SoundControl c;
        c.name = k.label;

        if (auto param = synth->getAutomatableParameterByID (k.paramID))
        {
            c.value = param->getCurrentNormalisedValue();
            auto raw = param->getCurrentValue();
            juce::String paramID (k.paramID);

            if (paramID == "ampAttack" || paramID == "ampRelease")  c.valueText = seconds (raw);
            else if (paramID == "filterResonance")                  c.valueText = juce::String (juce::roundToInt (raw)) + "%";
            else if (paramID == "reverbMix" && ! synth->reverbOnValue.get())  { c.value = 0.0f; c.valueText = "0%"; }
            else                                                    c.valueText = percent (c.value);
        }

        result.push_back (c);
    }

    return result;
}

void EffectsController::setSoundControl (TrackId id, int index, float value)
{
    if (! juce::isPositiveAndBelow (index, (int) knobMappings.size()))
        return;

    if (auto* t = findTrack (owner.getEdit(), id))
        if (auto* synth = findSynth (*t))
        {
            if (auto param = synth->getAutomatableParameterByID (knobMappings[(size_t) index].paramID))
            {
                // valueRange maps 0..1 onto the parameter's own (possibly skewed) range.
                auto raw = param->valueRange.convertFrom0to1 (juce::jlimit (0.0f, 1.0f, value));
                setParameterUndoably (*synth, *param, knobMappings[(size_t) index].paramID, raw);

                // Some patches have the synth's reverb switched off; "Room" switches it on.
                if (param == synth->reverbMix && raw > 0.0f && ! synth->reverbOnValue.get())
                    synth->reverbOnValue = true;
            }
        }
}

//==============================================================================
// The instrument
bool EffectsController::setSound (TrackId id, const juce::String& soundId, bool newUndoStep)
{
    auto* edit = owner.getEdit();
    auto* t = findTrack (edit, id);

    if (t == nullptr || isAudioTrack (*t))
        return false;

    // Make the new instrument first: if it can't be made, nothing changes.
    te::Plugin::Ptr instrument;
    juce::String name, category;

    if (auto* patch = SoundLibrary::findSynthPatch (soundId))
    {
        instrument = createSynth (*edit, *patch);
        name = patch->name;
        category = patch->category;
    }
    else if (auto* sampled = owner.getSounds().findSampledInstrument (soundId))
    {
        instrument = sampled->sfzFile.existsAsFile() ? createSfzInstrument (*edit, sampled->sfzFile)
                                                     : createSampler (*edit, *sampled);
        name = sampled->info.name;
        category = sampled->info.category;
    }
    else if (auto info = owner.getPlugins().findPlugin (soundId.fromFirstOccurrenceOf (thirdPartyPrefix, false, false)))
    {
        if (soundId.startsWith (thirdPartyPrefix) && info->isInstrument)
        {
            instrument = createPlugin (*edit, soundId);
            name = info->name;
            category = "Plug-ins";
        }
    }

    if (instrument == nullptr)
        return false;

    instrument->state.setProperty (instrumentProp, true, nullptr);

    if (newUndoStep)
        owner.beginUndoStep ("Change Sound");

    auto* um = &edit->getUndoManager();

    // Keep a name the user typed. The name is the app's if it's the one we
    // last gave the track, or (older sessions, which didn't store that) the
    // old sound's name or "Instrument N".
    // (Renaming a track clears the stored name.)
    const auto currentName = t->getName();
    const bool nameIsAutomatic = t->state.hasProperty (autoNameProp)
                                    ? currentName == t->state.getProperty (autoNameProp).toString()
                                    : currentName == t->state.getProperty (soundNameProp).toString()
                                        || currentName.startsWith ("Instrument ");

    for (auto* old : instrumentsOn (*t))
        old->removeFromParent();

    t->pluginList.insertPlugin (instrument, 0, nullptr);
    t->state.setProperty (soundIdProp, soundId, um);
    t->state.setProperty (soundNameProp, name, um);
    t->state.setProperty (categoryProp, category, um);

    if (nameIsAutomatic)
    {
        t->setName (name);
        t->state.setProperty (autoNameProp, name, um);
    }

    return true;
}

juce::String EffectsController::getSoundId (TrackId id) const
{
    if (auto* t = findTrack (owner.getEdit(), id))
    {
        auto soundId = t->state.getProperty (soundIdProp).toString();

        // Sessions from before the sound library only stored the sound's name.
        if (soundId.isEmpty() && ! isAudioTrack (*t))
            for (auto& s : owner.getSounds().getSounds())
                if (s.name == t->state.getProperty (soundNameProp, "Basic Synth").toString())
                    return s.id;

        return soundId;
    }

    return {};
}

//==============================================================================
// Plug-ins
bool EffectsController::isInstrumentReady (TrackId id) const
{
    if (auto* t = findTrack (owner.getEdit(), id))
        for (auto* p : t->pluginList)
            if (auto* sfz = dynamic_cast<SfzPlugin*> (p))
                return sfz->isLoaded();

    return true;
}

std::optional<PluginSlot> EffectsController::getInstrument (TrackId id) const
{
    if (auto* t = findTrack (owner.getEdit(), id))
        for (auto* p : t->pluginList)
            if (isInstrumentPlugin (*p))
                return makeSlot (*p);

    return std::nullopt;
}

std::vector<PluginSlot> EffectsController::getEffectChain (TrackId id) const
{
    std::vector<PluginSlot> result;

    if (auto* list = findPluginList (owner.getEdit(), id))
        for (auto* p : effectsOn (*list))
            result.push_back (makeSlot (*p));

    return result;
}

std::vector<EffectType> EffectsController::getAvailableEffects() const
{
    auto effects = builtInEffects();
    auto plugins = owner.getPlugins().getEffects();

    // Grouped by maker, then by name.
    std::stable_sort (plugins.begin(), plugins.end(), [] (const PluginInfo& a, const PluginInfo& b)
    {
        return a.manufacturer.compareNatural (b.manufacturer) < 0;
    });

    for (auto& p : plugins)
        effects.push_back ({ thirdPartyPrefix + p.id, p.name,
                             p.manufacturer.isNotEmpty() ? p.manufacturer : juce::String ("Plug-ins"), true });

    return effects;
}

void EffectsController::showPluginWindow (TrackId id, PluginId pluginId)
{
    if (auto* list = findPluginList (owner.getEdit(), id))
        for (auto* p : *list)
            if (p->itemID.getRawID() == pluginId)
                if (auto* external = dynamic_cast<te::ExternalPlugin*> (p))
                    if (! external->isMissing())
                        external->showWindowExplicitly();
}

PluginId EffectsController::addEffect (TrackId id, const juce::String& type, int position)
{
    auto* edit = owner.getEdit();
    auto* list = findPluginList (edit, id);

    if (list == nullptr || (int) effectsOn (*list).size() >= maxEffectsPerTrack)
        return 0;

    // A third-party plug-in can take a moment to load, so do it before the
    // undo step starts.
    auto plugin = createPlugin (*edit, type);

    if (plugin == nullptr)
        return 0;

    owner.beginUndoStep ("Add Effect");

    list->insertPlugin (plugin, pluginListIndexForPosition (*list, position), nullptr);
    return plugin->itemID.getRawID();
}

PluginId EffectsController::replaceEffect (TrackId id, PluginId pluginId, const juce::String& newType)
{
    auto* edit = owner.getEdit();
    auto* list = findPluginList (edit, id);
    auto* old = list != nullptr ? findEffect (*list, pluginId) : nullptr;

    if (old == nullptr)
        return 0;

    auto plugin = createPlugin (*edit, newType);

    if (plugin == nullptr)
        return 0;

    // Remove + insert in one undo step, at the same position.
    owner.beginUndoStep ("Replace Effect");
    const auto index = list->indexOf (old);
    old->removeFromParent();
    list->insertPlugin (plugin, index, nullptr);
    return plugin->itemID.getRawID();
}

void EffectsController::removeEffect (TrackId id, PluginId pluginId)
{
    if (auto* list = findPluginList (owner.getEdit(), id))
    {
        if (auto* p = findEffect (*list, pluginId))
        {
            owner.beginUndoStep ("Remove Effect");
            p->removeFromParent();
        }
    }
}

void EffectsController::moveEffect (TrackId id, PluginId pluginId, int newPosition)
{
    auto* list = findPluginList (owner.getEdit(), id);

    if (list == nullptr)
        return;

    // Keep a reference while it's briefly out of the list. (Plugin::Ptr is
    // ref-counted, so the plugin isn't deleted when it's removed.)
    te::Plugin::Ptr plugin = findEffect (*list, pluginId);

    if (plugin == nullptr)
        return;

    auto effects = effectsOn (*list);
    const auto oldPosition = (int) std::distance (effects.begin(), std::find (effects.begin(), effects.end(), plugin.get()));

    if (oldPosition == newPosition)
        return;

    owner.beginUndoStep ("Move Effect");
    plugin->removeFromParent();
    list->insertPlugin (plugin, pluginListIndexForPosition (*list, newPosition), nullptr);
}

void EffectsController::setEffectEnabled (TrackId id, PluginId pluginId, bool enabled)
{
    if (auto* list = findPluginList (owner.getEdit(), id))
    {
        if (auto* p = findEffect (*list, pluginId))
        {
            owner.beginUndoStep (enabled ? "Turn Effect On" : "Turn Effect Off");
            p->setEnabled (enabled);
        }
    }
}

std::vector<ParameterInfo> EffectsController::getEffectParameters (TrackId id, PluginId pluginId) const
{
    std::vector<ParameterInfo> result;

    if (auto* list = findPluginList (owner.getEdit(), id))
    {
        if (auto* p = findEffect (*list, pluginId))
        {
            if (dynamic_cast<te::ExternalPlugin*> (p) == nullptr)
                for (auto* param : p->getAutomatableParameters())
                    result.push_back ({ param->getParameterName(),
                                        param->getCurrentNormalisedValue(),
                                        param->getCurrentValueAsStringWithLabel() });

            // Settings kept as properties come after the real parameters.
            for (auto* setting : storedSettingsFor (*p))
            {
                const auto value = (float) p->state.getProperty (setting->property, setting->defaultValue);
                result.push_back ({ setting->name,
                                    juce::jmap (value, setting->minimum, setting->maximum, 0.0f, 1.0f),
                                    describeStored (*setting, value) });
            }
        }
    }

    return result;
}

void EffectsController::setEffectParameter (TrackId id, PluginId pluginId, int index, float value)
{
    // No beginUndoStep() here: the UI starts one on knob drag start.
    if (auto* list = findPluginList (owner.getEdit(), id))
    {
        if (auto* p = findEffect (*list, pluginId))
        {
            auto params = p->getAutomatableParameters();
            value = juce::jlimit (0.0f, 1.0f, value);

            if (auto* param = params[index])
            {
                setParameterUndoably (*p, *param, param->paramID, param->valueRange.convertFrom0to1 (value));
            }
            else
            {
                // A setting kept as a property (see storedSettings). Setting
                // it through the undo manager is all it takes: the effect
                // listens to its own state.
                auto stored = storedSettingsFor (*p);
                const auto storedIndex = index - params.size();

                if (juce::isPositiveAndBelow (storedIndex, (int) stored.size()))
                {
                    auto* setting = stored[(size_t) storedIndex];
                    p->state.setProperty (setting->property, juce::jmap (value, setting->minimum, setting->maximum),
                                          &owner.getEdit()->getUndoManager());
                }
            }
        }
    }
}

} // namespace ikaros
