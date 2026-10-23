#pragma once

#include "engine/EngineTypes.h"
#include "engine/AmpStyles.h"
#include <array>
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
        With newUndoStep false, it's added to the current undo step; with
        recordUndo false, it isn't undoable at all (for a Drummer track
        changing to its downloaded kit, which isn't an edit).
        A Drummer track keeps its name, and its notes move to the new kit's
        keys (see DrummerEditor). */
    bool setSound (TrackId, const juce::String& soundId, bool newUndoStep = true, bool recordUndo = true);

    /** The library sound the track uses (SoundInfo::id), or empty if unknown.
        A track playing its own sound (setOwnSound) has ownSoundId. */
    juce::String getSoundId (TrackId) const;

    //==============================================================================
    // Own sounds: an instrument track that plays an audio file (New Track's
    // "Sampler") or part of a recording (Region > Make Instrument from
    // Region) across the keys, with Tracktion's sampler. Its recorded pitch is on the key it sounds like (found with
    // the tuner's PitchDetector; middle C for claps and other sounds without
    // a clear note), and higher and lower keys play it faster and slower.
    // The Sound tab has the general knobs (see SoundLibrary::getDefaultKnobs).
    // Saving copies the file into the session (see AudioEngine::beginSave).

    static constexpr const char* ownSoundId = "own";
    static constexpr double maxOwnSoundSeconds = 30.0;   // longer files are cut short: the sampler holds the sound in memory

    /** Makes an instrument track play `lengthSeconds` of an audio file from
        `startSeconds` (0 = to the end), in one undo step ("Use Own Sound"),
        or with newUndoStep false, in the current one. The sound is named
        `name`, or after the file. Like setSound, a track with the app's
        name takes the sound's. Returns false (nothing changes) if the file
        can't be read, or the track isn't an instrument track (Drummer
        tracks play drum kits only). */
    bool setOwnSound (TrackId, const juce::File&, double startSeconds = 0.0, double lengthSeconds = 0.0,
                      const juce::String& name = {}, bool newUndoStep = true);

    /** True if the track plays its own sound (see setOwnSound). */
    bool usesOwnSound (TrackId) const;

    /** Off (the default), a note stops when its key is let go, like a held
        sung note; on, every note plays the whole sound, like a clap or a
        word. One undo step ("Play to the End On"/"Play to the End Off"). */
    void setOwnSoundPlaysToEnd (TrackId, bool);
    bool getOwnSoundPlaysToEnd (TrackId) const;

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
    static constexpr int maxEffectsPerTrack = 12;

    /** Opens a third-party plug-in's own window (instrument or effect). */
    void showPluginWindow (TrackId, PluginId);

    // Pedals (effects with a PedalStyle, see PedalStyles.h) stay at the
    // start of an audio track's chain, then the Amp (see AmpPlugin.h), then
    // its other effects: adding, moving and replacing keep that order,
    // whatever position is asked for. A pedal added at the end (-1) goes
    // after the last pedal. Instrument tracks and the master keep any
    // order. Older sessions open in the order they were saved in. A track
    // has at most one Amp: adding (or replacing an effect with) a second
    // one does nothing.

    /** True for an audio track that shows a pedalboard (the bottom panel's
        Pedals tab). Any audio track can (setPedalboardShown); until it's set,
        guitar and bass tracks do (their icon is "guitar" or "bass", see
        TrackController::addGuitarTrack), and so do tracks that already have
        pedals or an Amp. Instrument tracks and the master never do. */
    bool hasPedalboard (TrackId) const;

    /** Shows or hides an audio track's pedalboard, in one undo step (View >
        Pedalboard shows it). Hiding it doesn't remove the pedals: they still
        play, and the Plug-ins inspector still lists them. Saved with the
        session. */
    void setPedalboardShown (TrackId, bool shown);

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
    // The Amp at the end of the pedalboard (see AmpPlugin.h). Turning it
    // off (bypass) and removing it are setEffectEnabled and removeEffect.
    struct AmpSettings
    {
        bool exists = false;            // the track has an Amp
        PluginId pluginId = 0;
        bool on = false;                // ...and it isn't bypassed
        int voice = 0;                  // getAmpVoices() index
        int cab = 0;                    // getAmpCabs() index, or getAmpCabs().size() for Off
        bool bright = false;
        std::array<float, ampKnobCount> knobs {};           // 0..1, in ampKnobIds' order
        std::array<juce::String, ampKnobCount> knobText;    // e.g. "62%"
    };

    AmpSettings getAmp (TrackId) const;

    /** Adds an Amp after the track's pedals, with a voice's starting
        settings (-1: the track's own, see getDefaultAmpVoiceFor), in an
        undo step of its own ("Add Amp") or, with newUndoStep false, in the
        current one. Returns 0 if the track already has one, or no room. */
    PluginId addAmp (TrackId, int voice = -1, bool newUndoStep = true);

    /** Picks a voice: its amp model and tone stack, and its cab, Bright and
        knob settings, in one undo step ("Amp Voice"). */
    void setAmpVoice (TrackId, int voice);

    /** Picks a cab (or Off), changing nothing else. One undo step ("Amp Cab"). */
    void setAmpCab (TrackId, int cab);

    /** One undo step ("Amp Bright On"/"Amp Bright Off"). */
    void setAmpBright (TrackId, bool on);

    /** Turns one of the six knobs (ampKnobIds' order). Doesn't start an undo
        step: the UI starts one when a drag begins. */
    void setAmpKnob (TrackId, int knob, float normalised0to1);

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
