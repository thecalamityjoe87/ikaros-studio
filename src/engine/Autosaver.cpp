#include "engine/Autosaver.h"
#include "platform/FileLock.h"
#include <juce_gui_basics/juce_gui_basics.h>   // juce::ModifierKeys: is a mouse button held?
#include <tracktion_engine/tracktion_engine.h>

namespace ikaros
{

namespace te = tracktion;

// A take being recorded (or recorded since the last autosave).
struct Autosaver::LiveTake
{
    SessionRecovery::Take take;
    te::InputDeviceInstance* instance = nullptr;    // what it's recorded from...
    te::EditItemID target;                          // ...onto which track
    bool finished = false;
    bool noted = false;     // in Recovery.json yet? (MIDI only once something's played:
                            // every MIDI input records, most of them nothing)

    // MIDI: Tracktion's queue of the notes played so far (meant for drawing
    // them live; nothing else in the app reads it), and the file they go to.
    std::shared_ptr<choc::fifo::SingleReaderSingleWriterFIFO<juce::MidiMessage>> notes;
    juce::File journal;
};

Autosaver::Autosaver() = default;

Autosaver::~Autosaver()
{
    detach();
}

void Autosaver::attach (te::Edit& e, const juce::File& folder, std::function<SessionRecovery::Info()> describeSession)
{
    detach();

    edit = &e;
    workFolder = folder;
    describe = std::move (describeSession);

    // The lock is the "this session is open" marker: it's let go of when the
    // session closes, or by the OS if the app dies.
    lock = std::make_unique<platform::FileLock> (workFolder.getChildFile (SessionRecovery::lockFile));

    if (! workFolder.getChildFile (SessionRecovery::infoFile).existsAsFile())
        writeInfo();

    pending = hasAutosaved = recording = userChanged = false;
    lastAutosaveId = {};
    lastAutosaveTime = {};

    // Every undoable edit goes through the undo manager, which broadcasts a
    // change: that's "the user changed something". (Playback following
    // automation also changes the session, but not through the undo manager.)
    edit->getUndoManager().addChangeListener (this);
    startTimer (250);
}

void Autosaver::detach()
{
    stopTimer();

    if (edit == nullptr)
        return;

    edit->getUndoManager().removeChangeListener (this);
    waitUntilWritten();

    // In this order: if the app died in between, the working copy would
    // look crashed but have nothing to offer, and be quietly tidied away.
    SessionRecovery::clear (workFolder);
    lock.reset();

    takes.clear();
    recording = false;
    edit = nullptr;
}

void Autosaver::waitUntilWritten()
{
    // getNumJobs() counts the job being written as well as those waiting.
    while (writer.getNumJobs() > 0)
        juce::Thread::sleep (2);
}

SessionRecovery::Info Autosaver::currentInfo() const
{
    auto info = describe();

    // Takes go in until an autosave has them. The ones still recording,
    // and finished ones until the next autosave (they're not in the last one).
    for (auto& t : takes)
        if (t->noted)
            info.takes.push_back (t->take);

    return info;
}

void Autosaver::writeInfo()
{
    writer.addJob ([folder = workFolder, info = currentInfo()]
    {
        SessionRecovery::writeInfo (folder, info);
    });
}

//==============================================================================
void Autosaver::changeListenerCallback (juce::ChangeBroadcaster*)
{
    if (ignoringChanges)
        return;

    userChanged = true;

    const auto now = juce::Time::getMillisecondCounter();

    if (! pending)
        firstChange = now;

    pending = true;
    lastChange = now;
}

void Autosaver::timerCallback()
{
    if (edit == nullptr)
        return;

    const auto now = juce::Time::getMillisecondCounter();

    if (recording)
    {
        pollRecording();

        // In case recording never really started (no input to record from).
        if (! edit->getTransport().isRecording() && now - recordingSince > 1000)
            recordingStopping();

        return;
    }

    if (! pending)
        return;

    // Undone back to where it was saved, or just saved.
    if (! edit->hasChangedSinceSaved())
    {
        pending = false;
        return;
    }

    // Not in the middle of a drag: it's one undo step, and autosaving
    // starts a new one (see autosaveNow).
    if (juce::ModifierKeys::currentModifiers.isAnyMouseButtonDown())
        return;

    const bool quiet = now - lastChange >= (juce::uint32) timing.quietMs
                    || now - firstChange >= (juce::uint32) timing.maxWaitMs;
    const bool dueAgain = ! hasAutosaved || now - lastAutosave >= (juce::uint32) timing.minIntervalMs;

    if (quiet && dueAgain)
        autosaveNow();
}

void Autosaver::autosaveNow()
{
    if (edit == nullptr)
        return;

    // Plug-ins keep some settings to themselves until asked; flushState()
    // puts them in the session. Some (4OSC) do that through the undo
    // manager, so it gets a transaction of its own that's taken straight
    // back off (as in AudioEngine::beginSave), and its change messages are
    // delivered now, while we're ignoring them.
    {
        // ScopedValueSetter sets a variable and puts the old value back at the
        // end of the block, even if something throws.
        const juce::ScopedValueSetter<bool> ignore (ignoringChanges, true);
        auto& undoManager = edit->getUndoManager();
        undoManager.beginNewTransaction();
        edit->flushState();
        undoManager.undoCurrentTransactionOnly();
        undoManager.dispatchPendingMessages();
    }

    // A copy of the session, stamped with this autosave's ID. Copying is
    // quick; making XML of it and writing it happens on the writer thread.
    // (The copy shares nothing with the live session, so that's safe.)
    lastAutosaveId = juce::Uuid().toString();
    auto copy = edit->state.createCopy();
    copy.setProperty (SessionRecovery::autosaveIdProperty, lastAutosaveId, nullptr);

    // Takes that have finished are in this copy now (or the user undid them).
    takes.erase (std::remove_if (takes.begin(), takes.end(), [] (auto& t) { return t->finished; }), takes.end());

    writer.addJob ([folder = workFolder, copy, info = currentInfo()]
    {
        if (auto xml = copy.createXml())
            xml->writeTo (folder.getChildFile (SessionRecovery::autosaveFile));   // writes a temporary file, then swaps it in

        SessionRecovery::writeInfo (folder, info);
    });

    pending = false;
    hasAutosaved = true;
    lastAutosave = juce::Time::getMillisecondCounter();
    lastAutosaveTime = juce::Time::getCurrentTime();
}

void Autosaver::sessionSaved()
{
    if (edit == nullptr)
        return;

    userChanged = false;

    // Mid-take, the take isn't in the saved session yet, and the recovery
    // copy written when recording started is still the session as it is.
    if (recording)
    {
        writeInfo();   // (Save As changes the name)
        return;
    }

    waitUntilWritten();
    SessionRecovery::clear (workFolder);
    takes.clear();
    pending = false;
    lastAutosaveId = {};
    writeInfo();
}

//==============================================================================
void Autosaver::recordingStarting (double startBeats, double loopEndBeats, bool midiTakes)
{
    if (edit == nullptr)
        return;

    autosaveNow();
    recording = true;
    recordStartBeats = startBeats;
    recordLoopEndBeats = loopEndBeats;
    recordMidiTakes = midiTakes;
    recordingSince = juce::Time::getMillisecondCounter();
    startTimer (100);   // often enough that Tracktion's queue of notes never fills up
}

void Autosaver::recordingStopping()
{
    if (! recording)
        return;

    pollRecording();   // the last notes, while Tracktion still has them

    for (auto& t : takes)
        t->finished = true;

    recording = false;
    startTimer (250);
}

void Autosaver::pollRecording()
{
    bool newTakes = false;

    for (auto* instance : edit->getAllInputDevices())
    {
        for (auto target : instance->getTargets())
        {
            if (! instance->isRecording (target))
                continue;

            auto existing = std::find_if (takes.begin(), takes.end(), [&] (auto& t)
            {
                return ! t->finished && t->instance == instance && t->target == target;
            });

            LiveTake* live = existing != takes.end() ? existing->get() : nullptr;

            if (live == nullptr)
            {
                auto t = std::make_unique<LiveTake>();
                t->instance = instance;
                t->target = target;
                t->take.track = target.getRawID();
                t->take.startBeats = recordStartBeats;
                t->take.loopEndBeats = recordLoopEndBeats;
                t->take.midiTakes = recordMidiTakes;
                t->take.recordedAfter = lastAutosaveId;

                if (auto file = instance->getRecordingFile (target); file != juce::File())
                {
                    // Audio: Tracktion is already writing it into the working copy.
                    t->noted = true;
                    t->take.path = file.isAChildOf (workFolder) ? file.getRelativePathFrom (workFolder).replaceCharacter ('\\', '/')
                                                                : file.getFullPathName();
                }
                else if (auto notes = instance->getRecordingNotes (target))
                {
                    t->take.midi = true;
                    t->notes = notes;
                    t->take.path = juce::String (SessionRecovery::takesFolder) + "/" + juce::Uuid().toString() + ".txt";
                    t->journal = workFolder.getChildFile (t->take.path);
                    t->journal.getParentDirectory().createDirectory();
                }
                else
                {
                    continue;   // not something we can keep
                }

                live = t.get();
                newTakes = newTakes || t->noted;
                takes.push_back (std::move (t));
            }

            // MIDI played since last time: append it to the take's file.
            if (live->notes != nullptr)
            {
                juce::Array<juce::MidiMessage> played;
                juce::MidiMessage m;

                while (live->notes->pop (m))
                    played.add (m);

                if (! played.isEmpty())
                {
                    newTakes = newTakes || ! std::exchange (live->noted, true);

                    writer.addJob ([file = live->journal, text = SessionRecovery::toJournalLines (played)]
                    {
                        juce::FileOutputStream out (file);   // (appends to an existing file)
                        out.writeText (text, false, false, nullptr);
                        out.flush();
                    });
                }
            }
        }
    }

    if (newTakes)
        writeInfo();
}

} // namespace ikaros
