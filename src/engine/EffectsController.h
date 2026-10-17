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
    // The instrument

    /** Swaps an instrument track's instrument for a library sound (a
        SoundInfo::id, or "plugin:<id>" for a plug-in instrument) in one undo
        step. If the track still has the name the app gave it, it takes the
        sound's name; a name the user typed is kept. Returns false if the
        sound couldn't be loaded (nothing changes then).
        With newUndoStep false, it's added to the current undo step. */
    bool setSound (TrackId, const juce::String& soundId, bool newUndoStep = true);

    /** The library sound the track uses (SoundInfo::id), or empty if unknown. */
    juce::String getSoundId (TrackId) const;

    /** False while the track's instrument is still loading its samples in
        the background (big SFZ instruments take a moment); true otherwise. */
    bool isInstrumentReady (TrackId) const;

    /** The lowest and highest keys a sampled instrument plays, once it has
        loaded (nothing for the built-in synth or plug-ins). */
    std::optional<std::pair<int, int>> getInstrumentKeyRange (TrackId) const;

    //==============================================================================
    // Plug-ins: the track's instrument and its chain of effects (the
    // "Plug-ins" inspector). Effects are processed top to bottom. They can be
    // built in, or third-party (VST3/AU/LV2) plug-ins. The effect functions
    // also take masterTrackId, for the master channel's effects.
    std::optional<PluginSlot> getInstrument (TrackId) const;
    std::vector<PluginSlot> getEffectChain (TrackId) const;

    /** The effects that can be added to a track: the built-in ones, then
        third-party ones (type "plugin:<id>", grouped by maker). */
    std::vector<EffectType> getAvailableEffects() const;
    static constexpr int maxEffectsPerTrack = 8;

    /** Opens a third-party plug-in's own window (instrument or effect). */
    void showPluginWindow (TrackId, PluginId);

    /** Adds an effect at a position in the chain (-1 = at the end). Returns 0 if it couldn't. */
    PluginId addEffect (TrackId, const juce::String& type, int position = -1);
    PluginId replaceEffect (TrackId, PluginId, const juce::String& newType);
    void removeEffect (TrackId, PluginId);
    void moveEffect (TrackId, PluginId, int newPosition);
    void setEffectEnabled (TrackId, PluginId, bool enabled);

    /** A built-in effect's settings (third-party plug-ins have their own window). */
    std::vector<ParameterInfo> getEffectParameters (TrackId, PluginId) const;
    void setEffectParameter (TrackId, PluginId, int parameterIndex, float normalised0to1);

    //==============================================================================
    // The track inspector's "Noise Gate" section: a shortcut to the first
    // Noise Gate effect in a track's chain.
    struct NoiseGateSettings
    {
        bool exists = false;            // the track has a Noise Gate effect
        bool on = false;                // ...and it isn't bypassed
        PluginId pluginId = 0;
        float threshold = 0.0f;         // 0..1 of the knob's range
        juce::String thresholdText;     // e.g. "-50.0 dB"
    };

    NoiseGateSettings getNoiseGate (TrackId) const;

    /** Turning it on adds a Noise Gate at the top of the chain if there
        isn't one (false if the chain is full); turning it off bypasses it,
        keeping its settings. One undo step. */
    bool setNoiseGateOn (TrackId, bool on);

    /** Doesn't start an undo step: the UI starts one when a drag begins. */
    void setNoiseGateThreshold (TrackId, float normalised0to1);

private:
    AudioEngine& owner;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EffectsController)
};

} // namespace ikaros
