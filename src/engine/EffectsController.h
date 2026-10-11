#pragma once

#include "engine/EngineTypes.h"
#include <optional>

namespace ikaros
{

class AudioEngine;

//==============================================================================
/**
    A track's sound: the five knobs on the "Sound" tab, its instrument, and
    its chain of effects (the "Plug-ins" inspector).

    Every method runs on the message thread. Mutating methods record into the
    session's UndoManager.
*/
class EffectsController
{
public:
    explicit EffectsController (AudioEngine&);

    //==============================================================================
    // "Sound" tab
    std::vector<SoundControl> getSoundControls (TrackId) const;
    void setSoundControl (TrackId, int index, float normalised0to1);

    //==============================================================================
    // Plug-ins: the track's instrument and its chain of effects (the
    // "Plug-ins" inspector). Effects are processed top to bottom.
    // TODO(milestone-3): third-party VST3/AU/LV2 effects and instruments.
    std::optional<PluginSlot> getInstrument (TrackId) const;
    std::vector<PluginSlot> getEffectChain (TrackId) const;

    /** The built-in effects that can be added to a track. */
    static std::vector<EffectType> getAvailableEffects();
    static constexpr int maxEffectsPerTrack = 8;

    /** Adds an effect at a position in the chain (-1 = at the end). Returns 0 if it couldn't. */
    PluginId addEffect (TrackId, const juce::String& type, int position = -1);
    PluginId replaceEffect (TrackId, PluginId, const juce::String& newType);
    void removeEffect (TrackId, PluginId);
    void moveEffect (TrackId, PluginId, int newPosition);
    void setEffectEnabled (TrackId, PluginId, bool enabled);

    std::vector<ParameterInfo> getEffectParameters (TrackId, PluginId) const;
    void setEffectParameter (TrackId, PluginId, int parameterIndex, float normalised0to1);

private:
    AudioEngine& owner;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EffectsController)
};

} // namespace ikaros
