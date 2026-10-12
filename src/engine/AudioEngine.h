#pragma once

#include "engine/EngineTypes.h"
#include "engine/SessionBundle.h"
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

    Change notification: listeners hear (always on the message thread, a
    moment after the edit, with bursts of changes collected into one call)
    whenever the session is replaced or its model changes, along with what
    kind of thing changed (see ModelChanges). Fast-moving state (playhead,
    meters) is *not* broadcast; the UI polls it on a Timer instead.
*/
class AudioEngine
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
        plug-in list...). Tests use their own, so they don't touch the app's. */
    explicit AudioEngine (const juce::String& settingsName = "IkarosStudio");
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

    /** Sessions opened or saved lately, newest first (missing files left out). */
    std::vector<RecentSession> getRecentSessions() const;

    bool isUntitled() const;

    /** The session's file: its bundle, or for a session from an earlier
        version that hasn't been saved yet, the bundle it will be saved as.
        Empty while untitled. */
    juce::File getSessionFile() const;
    juce::String getSessionName() const;
    bool hasUnsavedChanges() const;

    //==============================================================================
    // Transport
    void play();
    void stop();
    void togglePlay();

    /** Starts recording onto the armed track (arming the selected track for
        this take if none is armed), or stops recording. The take becomes a
        region when recording stops. With the count-in on, one bar of clicks
        plays first. */
    void toggleRecord();
    void goToStart();
    void setPositionBeats (double beats);
    void setLooping (bool shouldLoop);
    void setLoopRangeBeats (double startBeat, double endBeat);
    void setMetronomeEnabled (bool);

    /** One bar of metronome clicks before recording starts. This is an app
        preference (Tracktion stores it in its settings), not part of a session. */
    void setCountInEnabled (bool);
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

    /** Renders part of the session offline (not through the speakers) and
        returns its loudest sample, 0..1. The smoke test uses it to check
        that instruments really play; an Export command can build on the
        same rendering later. */
    float renderPeak (double startBeat, double endBeat);

    //==============================================================================
    // Meters and stats, polled by the UI at ~30 Hz
    StereoLevel getMasterLevel();
    SystemStats getSystemStats() const;

    /** JUCE's standard audio/MIDI device picker, wired to Tracktion's device manager. */
    std::unique_ptr<juce::Component> createAudioSettingsComponent();

    //==============================================================================
    TrackController& getTracks() noexcept       { return *trackController; }

    /** The library's sounds and loops. */
    SoundLibrary& getSounds() noexcept          { return *sounds; }

    /** Small app settings that aren't part of a session (e.g. whether the
        starter sounds were offered), kept with the engine's own settings. */
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
    friend class EffectsController;
    friend class EqController;
    friend class SendBuses;
    friend class PluginLibrary;

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

    /** After a sound pack was installed, updated or removed: rescans the
        library and points tracks using that pack at its files. */
    void soundPacksChanged (const juce::String& packId);

    // ListenerList is safe even if a listener removes itself while being called.
    juce::ListenerList<Listener> listeners;

    struct Impl;                                // Tracktion-typed members live in the .cpp
    std::unique_ptr<Impl> impl;

    // Declared after impl, so they're destroyed before it (members are
    // destroyed in reverse order), while Tracktion's Engine still exists.
    std::unique_ptr<SoundLibrary> sounds;
    std::unique_ptr<PackStore> packStore;
    std::unique_ptr<PluginLibrary> plugins;
    std::unique_ptr<AudioPreview> preview;
    std::unique_ptr<TrackController> trackController;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioEngine)
};

} // namespace ikaros
