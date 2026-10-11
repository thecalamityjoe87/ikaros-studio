#pragma once

#include "engine/EngineTypes.h"
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
    // Sessions ("Sessions" in the UI). Like a GarageBand project, a saved
    // session is self-contained: a folder named after it holding the session
    // file and an Audio folder with every audio file its regions use
    // (imported files, library loops and recordings):
    //
    //     My Song/My Song.tracktionedit
    //     My Song/Audio/...
    //
    // Instruments' samples aren't copied: they belong to the sound library.
    static constexpr const char* sessionFileExtension = ".tracktionedit";

    /** Replaces the session with a new, untitled one built from a template. */
    void newSession (const NewSessionSettings& = {});
    bool openSession (const juce::File&);
    bool saveSession();                         // returns false if the session is untitled
    /** Saves to getSessionFileFor (file), copying the audio the session uses
        into its Audio folder. */
    bool saveSessionAs (const juce::File&);

    /** Where a session saved as "Dir/My Song" goes: "Dir/My Song/My
        Song.tracktionedit" (or just the file, if it's already in a folder of
        its own name). */
    static juce::File getSessionFileFor (const juce::File& chosenFile);

    /** A saved session's own audio: the Audio folder beside its file. */
    static juce::File getAudioFolderFor (const juce::File& sessionFile);

    /** Where the session chooser's picture of a session is kept: Preview.png
        beside its file. The UI writes it when saving (see MainComponent). */
    static juce::File getPreviewFileFor (const juce::File& sessionFile);

    /** Sessions opened or saved lately, newest first (missing files left out). */
    std::vector<RecentSession> getRecentSessions() const;

    /** Where an untitled session's recordings are written:
        Documents/Ikaros Studio/Recordings. A saved session records straight
        into its Audio folder; saving copies an untitled one's takes there. */
    static juce::File getRecordingsFolder();

    bool isUntitled() const;
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

    void loadEdit (const juce::ValueTree& editState, const juce::File&, bool untitled);

    /** Tells listeners (soon, on the message thread) that this changed. Most
        changes are spotted automatically by watching the session's ValueTree;
        call this for changes that don't live there (arming, plug-in scans). */
    void notifyModelChanged (const ModelChanges& = ModelChanges::everything());

    // ListenerList is safe even if a listener removes itself while being called.
    juce::ListenerList<Listener> listeners;

    struct Impl;                                // Tracktion-typed members live in the .cpp
    std::unique_ptr<Impl> impl;

    // Declared after impl, so they're destroyed before it (members are
    // destroyed in reverse order), while Tracktion's Engine still exists.
    std::unique_ptr<SoundLibrary> sounds;
    std::unique_ptr<PluginLibrary> plugins;
    std::unique_ptr<AudioPreview> preview;
    std::unique_ptr<TrackController> trackController;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioEngine)
};

} // namespace ikaros
