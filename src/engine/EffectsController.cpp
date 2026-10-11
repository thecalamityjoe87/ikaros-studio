#include "engine/EffectsController.h"
#include "engine/AudioEngine.h"
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

        return p.getName();
    }

    // Effects are every plugin on the track except the instrument and the
    // track's own volume/meter plugins (which Tracktion keeps at the end).
    bool isEffect (te::Plugin& p)
    {
        return ! p.isSynth()
            && dynamic_cast<te::VolumeAndPanPlugin*> (&p) == nullptr
            && dynamic_cast<te::LevelMeterPlugin*> (&p) == nullptr;
    }

    std::vector<te::Plugin*> effectsOn (te::AudioTrack& t)
    {
        std::vector<te::Plugin*> result;

        for (auto* p : t.pluginList)
            if (isEffect (*p))
                result.push_back (p);

        return result;
    }

    te::Plugin* findEffect (te::AudioTrack& t, PluginId id)
    {
        for (auto* p : effectsOn (t))
            if (p->itemID.getRawID() == id)
                return p;

        return nullptr;
    }

    PluginSlot makeSlot (te::Plugin& p)
    {
        return { p.itemID.getRawID(), p.getPluginType(), displayNameFor (p), p.isEnabled() };
    }

    // Where in the track's plugin list effect number `position` should go:
    // before the existing effect at that position, or (for "the end") just
    // before the volume plugin.
    int pluginListIndexForPosition (te::AudioTrack& t, int position)
    {
        auto effects = effectsOn (t);

        if (juce::isPositiveAndBelow (position, (int) effects.size()))
            return t.pluginList.indexOf (effects[(size_t) position]);

        if (! effects.empty())
            return t.pluginList.indexOf (effects.back()) + 1;

        if (auto* vol = t.getVolumePlugin())
            return t.pluginList.indexOf (vol);

        return t.pluginList.size();
    }

    juce::String percent (float v)    { return juce::String (juce::roundToInt (v * 100.0f)) + "%"; }

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

    if (t != nullptr && isAudioTrack (*t))
        return result;   // audio tracks have no instrument to control
    auto* synth = t != nullptr ? findSynth (*t) : nullptr;

    for (auto& k : knobMappings)
    {
        SoundControl c;
        c.name = k.label;

        if (synth != nullptr)
        {
            if (auto param = synth->getAutomatableParameterByID (k.paramID))
            {
                c.value = param->getCurrentNormalisedValue();
                auto raw = param->getCurrentValue();
                juce::String id (k.paramID);

                if (id == "ampAttack" || id == "ampRelease")  c.valueText = seconds (raw);
                else if (id == "filterResonance")             c.valueText = juce::String (juce::roundToInt (raw)) + "%";
                else                                          c.valueText = percent (c.value);
            }
        }
        else
        {
            c.valueText = juce::String::fromUTF8 ("\xe2\x80\x94");   // em dash
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
            }
        }
}

//==============================================================================
// Plug-ins
std::optional<PluginSlot> EffectsController::getInstrument (TrackId id) const
{
    if (auto* t = findTrack (owner.getEdit(), id))
        for (auto* p : t->pluginList)
            if (p->isSynth())
                return makeSlot (*p);

    return std::nullopt;
}

std::vector<PluginSlot> EffectsController::getEffectChain (TrackId id) const
{
    std::vector<PluginSlot> result;

    if (auto* t = findTrack (owner.getEdit(), id))
        for (auto* p : effectsOn (*t))
            result.push_back (makeSlot (*p));

    return result;
}

std::vector<EffectType> EffectsController::getAvailableEffects()
{
    return builtInEffects();
}

PluginId EffectsController::addEffect (TrackId id, const juce::String& type, int position)
{
    auto* edit = owner.getEdit();
    auto* t = findTrack (edit, id);

    if (t == nullptr || (int) effectsOn (*t).size() >= maxEffectsPerTrack)
        return 0;

    owner.beginUndoStep ("Add Effect");

    auto plugin = edit->getPluginCache().createNewPlugin (type, {});

    if (plugin == nullptr)
        return 0;

    t->pluginList.insertPlugin (plugin, pluginListIndexForPosition (*t, position), nullptr);
    return plugin->itemID.getRawID();
}

PluginId EffectsController::replaceEffect (TrackId id, PluginId pluginId, const juce::String& newType)
{
    auto* edit = owner.getEdit();
    auto* t = findTrack (edit, id);
    auto* old = t != nullptr ? findEffect (*t, pluginId) : nullptr;

    if (old == nullptr)
        return 0;

    auto plugin = edit->getPluginCache().createNewPlugin (newType, {});

    if (plugin == nullptr)
        return 0;

    // Remove + insert in one undo step, at the same position.
    owner.beginUndoStep ("Replace Effect");
    const auto index = t->pluginList.indexOf (old);
    old->removeFromParent();
    t->pluginList.insertPlugin (plugin, index, nullptr);
    return plugin->itemID.getRawID();
}

void EffectsController::removeEffect (TrackId id, PluginId pluginId)
{
    if (auto* t = findTrack (owner.getEdit(), id))
    {
        if (auto* p = findEffect (*t, pluginId))
        {
            owner.beginUndoStep ("Remove Effect");
            p->removeFromParent();
        }
    }
}

void EffectsController::moveEffect (TrackId id, PluginId pluginId, int newPosition)
{
    auto* t = findTrack (owner.getEdit(), id);

    if (t == nullptr)
        return;

    // Keep a reference while it's briefly out of the list. (Plugin::Ptr is
    // ref-counted, so the plugin isn't deleted when it's removed.)
    te::Plugin::Ptr plugin = findEffect (*t, pluginId);

    if (plugin == nullptr)
        return;

    auto effects = effectsOn (*t);
    const auto oldPosition = (int) std::distance (effects.begin(), std::find (effects.begin(), effects.end(), plugin.get()));

    if (oldPosition == newPosition)
        return;

    owner.beginUndoStep ("Move Effect");
    plugin->removeFromParent();
    t->pluginList.insertPlugin (plugin, pluginListIndexForPosition (*t, newPosition), nullptr);
}

void EffectsController::setEffectEnabled (TrackId id, PluginId pluginId, bool enabled)
{
    if (auto* t = findTrack (owner.getEdit(), id))
    {
        if (auto* p = findEffect (*t, pluginId))
        {
            owner.beginUndoStep (enabled ? "Turn Effect On" : "Turn Effect Off");
            p->setEnabled (enabled);
        }
    }
}

std::vector<ParameterInfo> EffectsController::getEffectParameters (TrackId id, PluginId pluginId) const
{
    std::vector<ParameterInfo> result;

    if (auto* t = findTrack (owner.getEdit(), id))
        if (auto* p = findEffect (*t, pluginId))
            for (auto* param : p->getAutomatableParameters())
                result.push_back ({ param->getParameterName(),
                                    param->getCurrentNormalisedValue(),
                                    param->getCurrentValueAsStringWithLabel() });

    return result;
}

void EffectsController::setEffectParameter (TrackId id, PluginId pluginId, int index, float value)
{
    // No beginUndoStep() here: the UI starts one on knob drag start.
    if (auto* t = findTrack (owner.getEdit(), id))
    {
        if (auto* p = findEffect (*t, pluginId))
        {
            auto params = p->getAutomatableParameters();

            if (auto* param = params[index])
                setParameterUndoably (*p, *param, param->paramID,
                                      param->valueRange.convertFrom0to1 (juce::jlimit (0.0f, 1.0f, value)));
        }
    }
}

} // namespace ikaros
