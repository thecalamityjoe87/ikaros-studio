#pragma once

#include "engine/AutomationEditor.h"
#include "engine/EffectsController.h"
#include "engine/EqController.h"
#include "engine/InputRouter.h"
#include "engine/NoteEditor.h"
#include "engine/RegionEditor.h"
#include "engine/SendBuses.h"
#include <memory>
#include <optional>

namespace ikaros
{

class AudioEngine;

//==============================================================================
/**
    Track-level operations: creating and deleting tracks, the selected track,
    and the mixer controls (mute, solo, volume, pan, meters).

    It owns the other track-related controllers and hands them out:
        inputs()   - live MIDI/audio input routing, arming, input meters
        regions()  - regions, the region clipboard, audio file import
        notes()    - notes in a MIDI region (the piano roll)
        effects()  - the Sound tab's knobs, the instrument and the effect chain
        sends()    - each track's sends to the shared Reverb and Delay buses
        eq()       - the EQ tab's 4-band EQ, on a track or the master
        automation() - automation curves and the lanes that show them

    Every method runs on the message thread. Mutating methods record into the
    session's UndoManager.
*/
class TrackController
{
public:
    explicit TrackController (AudioEngine&);
    ~TrackController();

    InputRouter& inputs() noexcept                          { return *inputRouter; }
    const InputRouter& inputs() const noexcept              { return *inputRouter; }
    RegionEditor& regions() noexcept                        { return *regionEditor; }
    const RegionEditor& regions() const noexcept            { return *regionEditor; }
    NoteEditor& notes() noexcept                            { return *noteEditor; }
    EffectsController& effects() noexcept                   { return *effectsController; }
    const EffectsController& effects() const noexcept       { return *effectsController; }
    SendBuses& sends() noexcept                             { return *sendBuses; }
    const SendBuses& sends() const noexcept                 { return *sendBuses; }
    EqController& eq() noexcept                             { return *eqController; }
    const EqController& eq() const noexcept                 { return *eqController; }
    AutomationEditor& automation() noexcept                 { return *automationEditor; }
    const AutomationEditor& automation() const noexcept     { return *automationEditor; }

    //==============================================================================
    // Tracks
    std::vector<TrackInfo> getTracks() const;
    std::optional<TrackInfo> getTrack (TrackId) const;

    /** Adds an instrument track after the last track, with the library's default sound. */
    TrackId addInstrumentTrack();

    /** Adds an audio track, for recording a microphone, guitar or other input. */
    TrackId addAudioTrack();
    void deleteTrack (TrackId);

    /** Adds a copy of a track just below it: same kind, sound, effects, mixer
        settings and colour. With withRegions, its regions are copied too
        (GarageBand's "Duplicate Track"); without, it starts empty ("New Track
        with Duplicate Settings"). Returns the new track, or 0. */
    TrackId duplicateTrack (TrackId, bool withRegions);

    void renameTrack (TrackId, const juce::String& newName);
    void setTrackColour (TrackId, juce::Colour);

    /** The icon shown in the track header, as a key such as "guitar" (the UI
        maps keys to pictures). Empty = automatic: follow the sound's category. */
    void setTrackIcon (TrackId, const juce::String& iconKey);

    /** The colours offered for tracks, in the order new tracks use them. */
    static std::vector<juce::Colour> getTrackColours();

    /** Moves a track one place up (-1) or down (+1) the list. */
    void moveTrack (TrackId, int direction);
    bool canMoveTrack (TrackId, int direction) const;

    /** The selected track is the one the on-screen and hardware keyboards play. */
    void setSelectedTrack (TrackId);
    TrackId getSelectedTrack() const noexcept   { return selectedTrack; }

    void setMute (TrackId, bool);
    void setSolo (TrackId, bool);
    void setVolume (TrackId, float sliderPosition0to1);

    /** -1 = left, 0 = centre, 1 = right. Like setVolume, the caller starts
        the undo step (once per drag). */
    void setPan (TrackId, float pan);

    /** The master fader (fader position 0..1, as for tracks). */
    float getMasterVolume() const;
    float getMasterVolumeDb() const;
    void setMasterVolume (float sliderPosition0to1);

    /** Where a volume fader sits for a level in dB (0 dB = unity gain), for
        "double-click to reset". Faders use Tracktion's curve, not dB. */
    static float getFaderPositionForDb (float db);

    StereoLevel getTrackLevel (TrackId);

    /** The "Show details" box: plugin, output and inputs. */
    TrackDetails getDetails (TrackId) const;

    //==============================================================================
    /** @internal Called by AudioEngine after the Edit has been replaced.
        Resets every controller's per-session state. */
    void sessionChanged();

private:
    struct Impl;                    // Tracktion-typed members (meter taps) live in the .cpp
    std::unique_ptr<Impl> impl;
    AudioEngine& owner;
    TrackId selectedTrack = 0;

    std::unique_ptr<InputRouter> inputRouter;
    std::unique_ptr<RegionEditor> regionEditor;
    std::unique_ptr<NoteEditor> noteEditor;
    std::unique_ptr<EffectsController> effectsController;
    std::unique_ptr<SendBuses> sendBuses;
    std::unique_ptr<EqController> eqController;
    std::unique_ptr<AutomationEditor> automationEditor;   // after sendBuses: it uses them

    TrackId createAudioTrack (const juce::String& name);
};

} // namespace ikaros
