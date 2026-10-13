#pragma once

#include "engine/EngineTypes.h"
#include "engine/RecordingTypes.h"
#include <memory>

namespace ikaros
{

class AudioEngine;

//==============================================================================
/**
    Live input: which track the MIDI keyboards and audio inputs feed, record
    arming, input meters, and the on-screen keyboard's notes.

    Input goes to the armed track, or else the selected track. TrackController
    owns the selection and passes it on with setSelectedTrack(), so this class
    doesn't depend on TrackController.

    Like GarageBand, selecting a track arms it (and disarms the previous one),
    except during a take, which stays on the track it started on. Disarming
    the selected track sticks until another track is selected.

    Every method runs on the message thread.
*/
class InputRouter
{
public:
    explicit InputRouter (AudioEngine&);
    ~InputRouter();

    /** Called by TrackController whenever the selected track changes (or
        input devices change). Re-routes the inputs, and arms a newly
        selected track. */
    void setSelectedTrack (TrackId);

    /** Arms a track for recording (one at a time). An armed track also gets
        the keyboards' input, even when another track is selected. */
    void setArmed (TrackId, bool);
    TrackId getArmedTrack() const noexcept      { return armedTrack; }

    /** Used by the Record button: if no track is armed, arms the selected one
        and returns it (so it can be disarmed after the take); otherwise 0. */
    TrackId armForRecording();

    /** Called by TrackController when a track is deleted: forgets it if it was armed. */
    void trackDeleted (TrackId);

    //==============================================================================
    // Audio inputs (audio tracks)
    std::vector<AudioInput> getAudioInputs() const;
    RecordingSettings getRecordingSettings (TrackId) const;
    void setTrackInput (TrackId, const juce::String& inputId);
    void setMonitoring (TrackId, bool);

    /** Level of the input feeding an audio track, and whether it has
        clipped (for the input meters). Several meters can show the same
        input in one timer tick. */
    InputMeter getInputMeter (TrackId);

    /** Turns off the clip light of the input feeding a track. (A new take
        on that input turns it off too.) */
    void clearClipIndicator (TrackId);

    //==============================================================================
    // Live MIDI (on-screen keyboard). Hardware keyboards are routed
    // automatically to the armed track, or else the selected track.
    void noteOn (int noteNumber, float velocity0to1);
    void noteOff (int noteNumber);
    void allNotesOff();

    void routeMidiInputs();
    void routeAudioInputs();

    /** @internal Called (via TrackController) after the Edit has been replaced. */
    void sessionChanged();

private:
    struct Impl;                    // Tracktion-typed members live in the .cpp
    std::unique_ptr<Impl> impl;
    AudioEngine& owner;
    TrackId selectedTrack = 0;      // a copy of TrackController's, for routing
    TrackId armedTrack = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (InputRouter)
};

} // namespace ikaros
