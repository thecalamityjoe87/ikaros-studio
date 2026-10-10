#pragma once

#include "engine/EngineTypes.h"
#include <memory>
#include <optional>

namespace ikaros
{

class AudioEngine;

//==============================================================================
/**
    Track-level operations: creating tracks and regions, the mixer controls,
    the instrument's sound controls, and live MIDI input.

    Every method runs on the message thread. Mutating methods record into the
    session's UndoManager.
*/
class TrackController
{
public:
    explicit TrackController (AudioEngine&);
    ~TrackController();

    //==============================================================================
    // Tracks
    std::vector<TrackInfo> getTracks() const;
    std::optional<TrackInfo> getTrack (TrackId) const;

    /** Adds an instrument track (Tracktion's built-in 4OSC synth) after the last track. */
    TrackId addInstrumentTrack();

    /** Adds an audio track, for recording a microphone, guitar or other input. */
    TrackId addAudioTrack();
    void deleteTrack (TrackId);

    /** The selected track is the one the on-screen and hardware keyboards play. */
    void setSelectedTrack (TrackId);
    TrackId getSelectedTrack() const noexcept   { return selectedTrack; }

    /** Arms a track for recording (one at a time). An armed track also gets
        the keyboards' input, even when another track is selected. */
    void setArmed (TrackId, bool);
    TrackId getArmedTrack() const noexcept      { return armedTrack; }

    /** Used by the Record button: if no track is armed, arms the selected one
        and returns it (so it can be disarmed after the take); otherwise 0. */
    TrackId armForRecording();

    //==============================================================================
    // Audio inputs (audio tracks)
    std::vector<AudioInput> getAudioInputs() const;
    RecordingSettings getRecordingSettings (TrackId) const;
    void setTrackInput (TrackId, const juce::String& inputId);
    void setMonitoring (TrackId, bool);

    /** Level of the input feeding an audio track (for the recording meter). */
    StereoLevel getInputLevel (TrackId);

    void setMute (TrackId, bool);
    void setSolo (TrackId, bool);
    void setVolume (TrackId, float sliderPosition0to1);

    /** Assigns a library preset to a track. Updates the name shown in the UI.
        TODO(milestone-3): load real sampler/plugin presets from the sound library. */
    void setSound (TrackId, const juce::String& category, const juce::String& presetName);

    StereoLevel getTrackLevel (TrackId);

    //==============================================================================
    // Regions (Tracktion clips)
    std::vector<RegionInfo> getRegions (TrackId) const;
    std::optional<RegionInfo> getRegion (RegionId) const;

    /** Creates a 4-bar MIDI region with a simple chord progression, starting at
        the bar that contains startBeat. Returns 0 if the track has no room. */
    RegionId createChordRegion (TrackId, double startBeat);
    void deleteRegion (RegionId);

    //==============================================================================
    // "Sound" tab
    std::vector<SoundControl> getSoundControls (TrackId) const;
    void setSoundControl (TrackId, int index, float normalised0to1);
    TrackDetails getDetails (TrackId) const;

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

    //==============================================================================
    // Live MIDI (on-screen keyboard). Hardware keyboards are routed
    // automatically to the armed track, or else the selected track.
    void noteOn (int noteNumber, float velocity0to1);
    void noteOff (int noteNumber);
    void allNotesOff();

    //==============================================================================
    /** @internal Called by AudioEngine after the Edit has been replaced. */
    void sessionChanged();

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
    AudioEngine& owner;
    TrackId selectedTrack = 0;
    TrackId armedTrack = 0;

    void routeMidiInputs();
    void routeAudioInputs();
};

} // namespace ikaros
