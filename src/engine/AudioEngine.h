#pragma once

#include "engine/EngineTypes.h"
#include "engine/Preferences.h"
#include "engine/RecordingTypes.h"
#include "engine/SessionBundle.h"
#include "engine/SessionRecovery.h"
#include <juce_events/juce_events.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>

// Forward declarations only: UI code that includes this header can't reach
// Tracktion types, which keeps the "only src/engine touches te::" rule enforced
// by the compiler. `inline namespace` mirrors how Tracktion declares
// tracktion::engine, so tracktion::Edit and tracktion::engine::Edit both work.
namespace tracktion { inline namespace engine { class Engine; class Edit; } }

namespace ikaros
{

class TrackController;
class SoundLibrary;
class PluginLibrary;
class PackStore;
class AudioPreview;
class SongExporter;
class RecordingGuard;
class AudioDeviceWatcher;

//==============================================================================
/** What the engine hands the UI when a third-party plug-in's window should
    open. The UI puts `content` (the plug-in's own editor) in a window frame
    and returns that window; the engine keeps it, and deletes it when the
    plug-in goes away. */
struct PluginWindowRequest
{
    std::unique_ptr<juce::Component> content;
    juce::String title;                                     // e.g. "Ikaros Test Gain - Lead"
    juce::Point<int> position;                              // where to put the window's top-left
    std::function<void()> close;                            // call when the user closes the window
    std::function<void (juce::Rectangle<int>)> moved;       // call when the user moves it (remembered for next time)
};

using PluginWindowFactory = std::function<std::unique_ptr<juce::Component> (PluginWindowRequest)>;

//==============================================================================
/**
    Owns the Tracktion Engine and the current Session (a te::Edit).

    Responsibilities: session lifecycle (new/open/save), the transport, tempo,
    undo/redo, and global meters/stats. Track-level work lives in TrackController.
    It also owns the app's Preferences, and acts on the ones that change what it does.

    Change notification: listeners hear (always on the message thread, a
    moment after the edit, with bursts of changes collected into one call)
    whenever the session is replaced or its model changes, along with what
    kind of thing changed (see ModelChanges). Fast-moving state (playhead,
    meters) is *not* broadcast; the UI polls it on a Timer instead.
*/
class AudioEngine  : private Preferences::Listener
{
public:
    /** Implement this to hear about session changes. */
    struct Listener
    {
        virtual ~Listener() = default;
        virtual void sessionChanged (const ModelChanges&) = 0;
    };

    void addListener (Listener* l)        { listeners.add (l); }
    void removeListener (Listener* l)     { listeners.remove (l); }

    /** settingsName picks where Tracktion keeps its settings (devices, the
        plug-in list...). Tests use their own, so they don't touch the app's.
        Tests that drive a TestAudioDevice pass openAudioDevice = false, so
        the computer's own audio device is never opened. */
    explicit AudioEngine (const juce::String& settingsName = "IkarosStudio", bool openAudioDevice = true);
    ~AudioEngine();

    //==============================================================================
    // Sessions ("Sessions" in the UI). A saved session is one file, a
    // .iksession bundle (see SessionBundle), holding the session and every
    // audio file it uses (recordings, imported files, loops), so it can be
    // moved, backed up, shared, and opened on Linux, Windows or macOS.
    // Instruments' samples aren't in it: they belong to the sound packs, and
    // a session finds them by pack id on whichever computer opens it.
    //
    // While a session is open, the app works on an unpacked copy of it (a
    // working folder in the computer's local app data, see
    // platform::getLocalDataFolder). Recordings go straight into that copy;
    // saving packs it back into the bundle.
    static constexpr const char* sessionFileExtension = SessionBundle::extension;

    /** Sessions saved by earlier versions: a folder with a .tracktionedit file
        and an Audio folder. They still open; saving one writes a bundle next
        to its folder and leaves the folder as it was. */
    static constexpr const char* legacySessionFileExtension = ".tracktionedit";

    /** Replaces the session with a new, untitled one built from a template. */
    void newSession (const NewSessionSettings& = {});

    /** Opens a .iksession bundle, or a session folder from an earlier version.
        Returns false (and keeps the current session) if it can't be read. */
    bool openSession (const juce::File&);

    /** Saving, in two steps, so the slow part (packing the audio into the
        bundle) can run without blocking the UI:

          1. beginSave (on the message thread) writes the session into its
             working copy and returns a job, or nullptr if that failed;
          2. the job's run() packs the bundle, on any thread, updating
             `progress`;
          3. finishSave (on the message thread) makes the bundle the session's
             file. It returns false if packing failed (the old bundle, if
             any, is untouched).

        saveSession and saveSessionAs do all three in one go. */
    struct SaveJob
    {
        juce::File bundle;
        std::vector<SessionBundle::Entry> entries;
        juce::var info;
        std::atomic<float> progress { 0.0f };
        juce::Result result = juce::Result::ok();

        void run()      { result = SessionBundle::pack (bundle, entries, info, &progress); }
    };

    std::unique_ptr<SaveJob> beginSave (const juce::File& bundle);
    bool finishSave (SaveJob&);

    bool saveSession();                         // returns false if the session is untitled
    bool saveSessionAs (const juce::File&);

    /** Where a session saved as "Dir/My Song" goes: "Dir/My Song.iksession". */
    static juce::File getSessionFileFor (const juce::File& chosenFile);

    /** Where the session chooser's picture of the session goes before saving
        (in the working copy). The UI writes it, then saves. */
    juce::File getPreviewFile() const;

    /** Where the file dialogs for sessions start: the folder chosen in the
        Preferences, or else Documents/Ikaros Studio. Made if it's missing. */
    juce::File getSessionFolder() const;
    static juce::File getStandardSessionFolder();

    /** Sessions opened or saved lately, newest first (missing files left out). */
    std::vector<RecentSession> getRecentSessions() const;
    void clearRecentSessions();

    bool isUntitled() const;

    /** The session's file: its bundle, or for a session from an earlier
        version that hasn't been saved yet, the bundle it will be saved as.
        Empty while untitled. */
    juce::File getSessionFile() const;
    juce::String getSessionName() const;
    bool hasUnsavedChanges() const;

    //==============================================================================
    // Crash recovery (see Autosaver and SessionRecovery). A few seconds after
    // changes stop (at most once a minute), a recovery copy of the session is
    // written into its working copy; the user's saved file is only ever
    // written by Save. Takes go to disk as they're recorded.

    /** Sessions that were open when the app crashed and have unsaved work,
        newest first. Crashed ones with nothing unsaved are tidied away.
        Sessions open in another copy of the app are never listed. */
    std::vector<RecoverableSession> findRecoverableSessions();

    /** Replaces the current session with a crashed one, as it was at its
        last autosave, with any take that was being recorded back on its
        track. It's marked as having unsaved changes. Returns false (and keeps
        the current session) if it can't be read. */
    bool recoverSession (const RecoverableSession&);

    /** Deletes a crashed session's unsaved work for good. */
    void discardRecoverableSession (const RecoverableSession&);

    /** For tests: write the recovery copy now / change when it's written /
        wait until it's on disk / see when it was last written, and where the
        session's working copy is. */
    void autosaveNow();
    void setAutosaveTiming (int quietMs, int minIntervalMs, int maxWaitMs);
    void waitForAutosave();
    juce::Time getLastAutosaveTime() const;
    juce::File getWorkingCopyFolder() const;

    //==============================================================================
    // Transport
    void play();
    void stop();
    void togglePlay();

    /** Starts recording onto the armed track (arming the selected track for
        this take if none is armed, unless the preference to do so is off:
        then with no armed track nothing starts, and the report says
        noTrackArmed), or stops recording. The take becomes a
        region when recording stops. With the count-in on, one bar of clicks
        plays first. With the loop on (cycle recording) it starts at the
        loop's start and each pass becomes a take (see TakeEditor); a loop
        under 2 seconds is refused (the report says loopTooShort).

        Takes are lined up with the music automatically: Tracktion moves each
        one back by the latency the audio device reports. If the device stops,
        changes, or the disk fills up mid-take, recording stops by itself and
        what was recorded is kept (see takeRecordingReport). */
    void toggleRecord();
    void goToStart();
    void setPositionBeats (double beats);
    void setLooping (bool shouldLoop);
    void setLoopRangeBeats (double startBeat, double endBeat);
    void setMetronomeEnabled (bool);

    /** MIDI cycle recording: keep each pass as a take of one region (true),
        or merge every pass's notes into the region (false, the default).
        An app preference (Preferences, settings::midiCycleTakes). Audio
        passes are always takes. */
    void setMidiCycleTakes (bool keepAsTakes);
    bool getMidiCycleTakes() const;

    /** Metronome clicks before recording starts (one or two bars, see
        setCountInBars). This is an app preference (settings::countIn), not
        part of a session. */
    void setCountInEnabled (bool);

    /** How many bars the count-in plays: 0 (off), 1 or 2. Turning the
        count-in on with setCountInEnabled uses the last length chosen here. */
    void setCountInBars (int bars);
    int getCountInBars() const;

    void setBpm (double bpm);
    void setTimeSignature (int numerator, int denominator);

    /** The session's key: root 0 = C ... 11 = B, major or minor. */
    void setKey (int root, bool minor);

    TransportState getTransportState() const;
    juce::String getKeyName() const;

    double beatsPerBar() const;

    //==============================================================================
    // Undo / redo (Tracktion's UndoManager, shared by every edit operation)
    void undo();
    void redo();
    bool canUndo() const;
    bool canRedo() const;
    juce::String getUndoDescription() const;
    juce::String getRedoDescription() const;

    /** Starts a new undo step. Call once at the start of each user gesture
        (a click, or the mouse-down of a drag) so a whole drag undoes in one go.
        The name appears in the Edit menu, e.g. "Undo Add Track". */
    void beginUndoStep (const juce::String& name = {});

    /** The whole session as XML, the way it would be saved. The smoke test
        compares these to check undo, redo and save/reopen restore a session
        exactly. */
    juce::String getStateSnapshot() const;

    /** Renders part of the session offline (not through the speakers) and
        returns its loudest sample, 0..1. The smoke test uses it to check
        that instruments really play (exporting is getExporter()'s job). */
    float renderPeak (double startBeat, double endBeat);

    //==============================================================================
    // Meters and stats, polled by the UI at ~30 Hz
    StereoLevel getMasterLevel();
    SystemStats getSystemStats() const;

    /** After a take ends: why it stopped, whether the input clipped, and how
        many dropouts it had. Each report is returned once (then nullopt).
        A report with reason notStarted means Record was refused. */
    std::optional<RecordingReport> takeRecordingReport();

    /** The audio device's buffer, latencies and dropouts (for the audio settings). */
    AudioDeviceStatus getAudioDeviceStatus() const;

    /** A device was plugged in, or the one in use unplugged, and the user
        should be asked about it (unless the "Audio device changes" setting
        says otherwise). Each prompt is returned once. */
    std::optional<AudioDevicePrompt> takeAudioDevicePrompt();

    /** Switches to the devices a prompt names. Returns what went wrong, or
        an empty string. */
    juce::String switchAudioDevice (const AudioDevicePrompt&);

    /** For tests, to act out devices coming and going. */
    AudioDeviceWatcher& getDeviceWatcher() noexcept     { return *deviceWatcher; }

    /** Recording needs at least this much free disk space (default 200 MB).
        Tests raise it to act out a full disk. */
    void setMinimumFreeDiskSpace (juce::int64 bytes);

    /** JUCE's standard audio/MIDI device picker, wired to Tracktion's device manager. */
    std::unique_ptr<juce::Component> createAudioSettingsComponent();

    //==============================================================================
    TrackController& getTracks() noexcept       { return *trackController; }

    /** The library's sounds and loops. */
    SoundLibrary& getSounds() noexcept          { return *sounds; }

    /** The app's settings (see Preferences). */
    Preferences& getPreferences() noexcept      { return *preferences; }

    /** Small bits of app state that aren't choices (e.g. whether the starter
        sounds were offered), kept with the engine's own settings. */
    juce::var getAppSetting (const juce::String& name, const juce::var& defaultValue = {}) const;
    void setAppSetting (const juce::String& name, const juce::var& value);

    /** Downloadable sound packs (the "More Sounds" window). After a pack is
        installed or removed, the library is rescanned, tracks that were
        waiting for it start playing, and listeners hear about it. */
    PackStore& getPackStore() noexcept          { return *packStore; }

    /** Third-party plug-ins and the plug-in scan. */
    PluginLibrary& getPlugins() noexcept        { return *plugins; }

    /** Plays library files on their own, for previewing. */
    AudioPreview& getPreview() noexcept         { return *preview; }

    /** Renders the song (or one file per track) to WAV/FLAC/OGG. */
    SongExporter& getExporter() noexcept        { return *exporter; }

    /** Sets how plug-in windows are framed (see PluginWindowRequest). Without
        one, plug-in windows don't open. */
    void setPluginWindowFactory (PluginWindowFactory);

    /** Call first thing at startup. If this process was started to scan
        plug-ins (see PluginScanner), it becomes the scan worker and this
        returns true: then don't create the engine or any windows. */
    static bool runPluginScanWorkerIfRequested (const juce::String& commandLine);

private:
    // The track-level controllers need the Edit (and Tracktion's Engine) to do their job.
    friend class TrackController;
    friend class InputRouter;
    friend class RegionEditor;
    friend class NoteEditor;
    friend class TakeEditor;
    friend class EffectsController;
    friend class EqController;
    friend class SendBuses;
    friend class AutomationEditor;
    friend class PluginLibrary;
    friend class SongExporter;
    friend class RecordingGuard;
    friend class AudioDeviceWatcher;
    friend class TestAudioDevice;

    tracktion::Engine& getTeEngine() noexcept;
    tracktion::Edit* getEdit() const noexcept;

    /** Replaces the session. workFolder is its working copy; bundle the file
        it's saved as (empty while untitled); legacySessionFile, for a session
        from an earlier version, where its audio still is. */
    void loadEdit (const juce::ValueTree& editState, const juce::File& workFolder,
                   const juce::File& bundle, const juce::File& legacySessionFile);

    /** Tells listeners (soon, on the message thread) that this changed. Most
        changes are spotted automatically by watching the session's ValueTree;
        call this for changes that don't live there (arming, plug-in scans). */
    void notifyModelChanged (const ModelChanges& = ModelChanges::everything());

    /** For TestAudioDevice. The device calls its callbacks in the order they
        were added, and the test device works in place (input and output
        share buffers), so Tracktion's must come first, before ours that
        write output (which would wipe the input). Tracktion adds its own
        when the device manager is initialised, which without a real device
        happens after ours: this moves ours behind it. */
    void reattachAudioCallbacks();

    /** A take has ended (the transport has stopped): turns it into regions,
        disarms a track armed just for it, and has its report made. */
    void finishTake();

    /** After a sound pack was installed, updated or removed: rescans the
        library and points tracks using that pack at its files. */
    void soundPacksChanged (const juce::String& packId);

    // Preferences that change what the engine does.
    void preferenceChanged (const juce::String& key) override;
    void applyCountIn();
    void applySoundsFolder();
    juce::File getUserSoundsFolder() const;   // the preference, or the default

    // ListenerList is safe even if a listener removes itself while being called.
    juce::ListenerList<Listener> listeners;

    struct Impl;                                // Tracktion-typed members live in the .cpp
    std::unique_ptr<Impl> impl;

    // Declared after impl, so they're destroyed before it (members are
    // destroyed in reverse order), while Tracktion's Engine still exists.
    std::unique_ptr<Preferences> preferences;
    std::unique_ptr<SoundLibrary> sounds;
    std::unique_ptr<PackStore> packStore;
    std::unique_ptr<PluginLibrary> plugins;
    std::unique_ptr<AudioPreview> preview;
    std::unique_ptr<TrackController> trackController;
    std::unique_ptr<SongExporter> exporter;
    std::unique_ptr<RecordingGuard> recordingGuard;
    std::unique_ptr<AudioDeviceWatcher> deviceWatcher;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioEngine)
};

} // namespace ikaros
