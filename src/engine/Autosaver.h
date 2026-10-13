#pragma once

#include "engine/SessionRecovery.h"
#include <juce_events/juce_events.h>
#include <functional>
#include <memory>
#include <vector>

namespace tracktion { inline namespace engine { class Edit; } }

namespace ikaros
{

namespace platform { class FileLock; }

//==============================================================================
/**
    Keeps the open session safe from crashes, in its working copy (see
    SessionRecovery for the files):

      - It marks the working copy as open (a held lock file), so after a
        crash the next launch can tell it apart from a session that is
        open in another copy of the app.
      - A few seconds after the user stops making changes (and at most
        once a minute), it writes a recovery copy of the session. It never
        touches the user's saved .iksession: only Save does that.
      - While recording, it notes which files the takes go into, and writes
        MIDI to disk as it's played (audio already goes to disk as it's
        recorded), so a crash mid-take keeps what was played.

    Writing never blocks the UI or the audio: the session is copied on the
    message thread (quick), and turned into XML and written on a background
    thread. AudioEngine owns one and tells it when sessions come and go.
*/
class Autosaver  : private juce::Timer,
                   private juce::ChangeListener
{
public:
    struct Timing
    {
        int quietMs = 4000;          // autosave once the session has been left alone this long,
        int minIntervalMs = 60000;   // but no more often than this,
        int maxWaitMs = 120000;      // and without waiting longer than this for a quiet moment
    };

    Autosaver();
    ~Autosaver() override;

    /** Starts looking after a session: marks its working copy as open and
        notes what it's called. `describe` is asked for the name and where
        it's saved whenever they're written (they change on Save As).
        A working copy that's being recovered keeps its Recovery.json until
        the first autosave. */
    void attach (tracktion::Edit&, const juce::File& workFolder, std::function<SessionRecovery::Info()> describe);

    /** Stops looking after the session (before its Edit goes): waits for
        writes in progress, removes the recovery files, then lets go of the
        lock, so a closed session never looks like a crashed one. */
    void detach();

    void setTiming (Timing t)        { timing = t; }

    /** Writes the recovery copy now, whatever the timing. */
    void autosaveNow();

    /** Blocks until everything queued has been written. */
    void waitUntilWritten();

    /** After a save: the saved session has everything, so the recovery copy
        is removed (it's out of date, and there's nothing to recover). */
    void sessionSaved();

    /** Recording: call just before it starts (it autosaves first, so a
        take always has a recovery copy to go back into) and just before it
        stops (to write the last of the MIDI). For a cycle recording,
        loopEndBeats is where the loop ends (it starts at startBeats);
        otherwise 0. midiTakes: MIDI passes are kept as takes. */
    void recordingStarting (double startBeats, double loopEndBeats = 0.0, bool midiTakes = false);
    void recordingStopping();

    juce::Time getLastAutosaveTime() const     { return lastAutosaveTime; }

private:
    struct LiveTake;   // defined in the .cpp (it holds Tracktion types)

    tracktion::Edit* edit = nullptr;
    juce::File workFolder;
    std::function<SessionRecovery::Info()> describe;
    std::unique_ptr<platform::FileLock> lock;

    Timing timing;
    bool pending = false;              // changes since the last autosave
    bool ignoringChanges = false;      // while autosaveNow itself touches the undo manager
    juce::uint32 firstChange = 0, lastChange = 0, lastAutosave = 0;
    bool hasAutosaved = false;
    juce::Time lastAutosaveTime;
    juce::String lastAutosaveId;

    bool recording = false;
    double recordStartBeats = 0.0;
    double recordLoopEndBeats = 0.0;
    bool recordMidiTakes = false;
    juce::uint32 recordingSince = 0;
    std::vector<std::unique_ptr<LiveTake>> takes;

    // One background thread does all the writing, in the order it's asked.
    juce::ThreadPool writer { 1 };

    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void pollRecording();
    void writeInfo();
    SessionRecovery::Info currentInfo() const;

    JUCE_DECLARE_NON_COPYABLE (Autosaver)
};

} // namespace ikaros
