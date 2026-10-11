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

    Change notification: AudioEngine is a juce::ChangeBroadcaster. It sends a
    change message (always on the message thread, coalesced) whenever the
    session is replaced or the session model changes (tracks, regions, mute,
    undo, ...). Fast-moving state (playhead, meters) is *not* broadcast; the UI
    polls it on a Timer instead.
*/
class AudioEngine  : public juce::ChangeBroadcaster
{
public:
    /** settingsName picks where Tracktion keeps its settings (devices, the
        plug-in list...). Tests use their own, so they don't touch the app's. */
    explicit AudioEngine (const juce::String& settingsName = "IkarosStudio");
    ~AudioEngine() override;

    //==============================================================================
    // Sessions ("Sessions" in the UI, .tracktionedit files on disk)
    static constexpr const char* sessionFileExtension = ".tracktionedit";

    void newSession();
    bool openSession (const juce::File&);
    bool saveSession();                         // returns false if the session is untitled
    bool saveSessionAs (const juce::File&);

    /** Where a saved session keeps its recordings: a folder next to the
        session file, e.g. "My Song Recordings" beside "My Song.tracktionedit". */
    static juce::File getRecordingsFolderFor (const juce::File& sessionFile);

    /** Where new recordings are written: Documents/Ikaros Studio/Recordings.
        Saving copies a session's recordings into its own folder. */
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
    friend class SendBuses;
    friend class PluginLibrary;

    tracktion::Engine& getTeEngine() noexcept;
    tracktion::Edit* getEdit() const noexcept;

    void loadEdit (const juce::ValueTree& editState, const juce::File&, bool untitled);
    void notifyModelChanged();

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
