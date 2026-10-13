#pragma once

#include "engine/EngineTypes.h"
#include <juce_audio_basics/juce_audio_basics.h>   // juce::MidiMessage
#include <optional>
#include <vector>

namespace ikaros
{

//==============================================================================
/** A session the app was working on when it crashed, as offered at launch. */
struct RecoverableSession
{
    juce::String name;          // "My Song", or "Untitled"
    juce::Time savedAt;         // when its work last reached the disk (the autosave, or a take)
    bool untitled = true;
    juce::File folder;          // its working copy; hand it back to AudioEngine to recover or discard it
};

//==============================================================================
/**
    The files that let a session survive a crash. They all live in the
    session's working copy (see AudioEngine), never next to the user's saved
    .iksession, so nothing here can overwrite the user's work:

        Open.lock               held (see platform::FileLock) while the session
                                is open. There but not held = the app crashed.
        Recovery.json           the session's name, where it's saved, when the
                                autosave was written, and takes being recorded
        Autosave.tracktionedit  the session as it was a few seconds after the
                                last change (Session.tracktionedit is the
                                session as it was last opened or saved)
        Takes/...               MIDI being recorded, written as it's played
        Audio/...               audio being recorded (Tracktion writes it there
                                as it goes)

    Like SessionBundle, this class only deals with files: no Tracktion, so
    it can run on any thread and be tested on its own. Autosaver writes
    these files; AudioEngine reads them back.
*/
class SessionRecovery
{
public:
    static constexpr const char* lockFile = "Open.lock";
    static constexpr const char* infoFile = "Recovery.json";
    static constexpr const char* autosaveFile = "Autosave.tracktionedit";
    static constexpr const char* takesFolder = "Takes";

    /** On the autosave's root: which autosave this is (see Take::recordedAfter). */
    static constexpr const char* autosaveIdProperty = "ikarosAutosaveId";

    /** A take being recorded, or recorded since the last autosave. */
    struct Take
    {
        bool midi = false;
        TrackId track = 0;
        juce::String path;          // in the working copy: "Audio/....wav", or "Takes/....txt" for MIDI
        double startBeats = 0.0;    // where the take starts on the timeline

        /** The ID of the autosave written just before recording started. If
            the autosave found after a crash is still that one, the take
            isn't in it, and recovery adds it. A later autosave already has
            it (or the user undid it). */
        juce::String recordedAfter;
    };

    struct Info
    {
        juce::String name;
        juce::File bundle;          // where the session is saved (empty while untitled)
        juce::File legacySession;   // a session from an earlier version, still in its old folder
        std::vector<Take> takes;
    };

    /** Writes Recovery.json, replacing the old one only once the new one is
        complete. */
    static bool writeInfo (const juce::File& workFolder, const Info&);
    static std::optional<Info> readInfo (const juce::File& workFolder);

    /** Removes the autosave, Recovery.json and the MIDI takes: what's left
        is just the working copy, with nothing to recover. */
    static void clear (const juce::File& workFolder);

    /** The working copies under root whose session was open when the app
        crashed: Open.lock is there, but nobody holds it. */
    static std::vector<juce::File> findCrashedWorkFolders (const juce::File& root);

    /** True if a crashed working copy has something to offer: a
        Recovery.json and an autosave or a take. */
    static bool hasWorkToRecover (const juce::File& workFolder);

    /** When the work was last written: the autosave or the newest take. */
    static juce::Time getLastWriteTime (const juce::File& workFolder, const Info&);

    //==============================================================================
    /** A WAV file whose recording was cut off has a header that says it holds
        no audio (the lengths are only filled in when recording stops). This
        fills them in from the file's size, and drops a half-written sample
        frame at the end. Files that are fine are left alone. Returns false
        if it isn't a WAV file it understands. */
    static bool repairWavFile (const juce::File&);

    /** MIDI takes are written as text, one message per line: the time in
        seconds, then the bytes in hex ("1.250000 90 3c 64"). Appending lines
        is cheap and safe; a line cut off by a crash is just skipped. */
    static juce::String toJournalLines (const juce::Array<juce::MidiMessage>&);
    static juce::Array<juce::MidiMessage> readJournal (const juce::File&);
};

} // namespace ikaros
