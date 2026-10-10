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
    AudioEngine();
    ~AudioEngine() override;

    //==============================================================================
    // Sessions ("Sessions" in the UI, .tracktionedit files on disk)
    static constexpr const char* sessionFileExtension = ".tracktionedit";

    void newSession();
    bool openSession (const juce::File&);
    bool saveSession();                         // returns false if the session is untitled
    bool saveSessionAs (const juce::File&);

    bool isUntitled() const;
    juce::File getSessionFile() const;
    juce::String getSessionName() const;
    bool hasUnsavedChanges() const;

    //==============================================================================
    // Transport
    void play();
    void stop();
    void togglePlay();
    void goToStart();
    void setPositionBeats (double beats);
    void setLooping (bool shouldLoop);
    void setLoopRangeBeats (double startBeat, double endBeat);
    void setMetronomeEnabled (bool);
    void setBpm (double bpm);
    void setTimeSignature (int numerator, int denominator);

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

private:
    friend class TrackController;               // needs the Edit to do its job

    tracktion::Engine& getTeEngine() noexcept;
    tracktion::Edit* getEdit() const noexcept;

    void loadEdit (std::unique_ptr<tracktion::Edit>, const juce::File&, bool untitled);
    void notifyModelChanged();

    struct Impl;                                // Tracktion-typed members live in the .cpp
    std::unique_ptr<Impl> impl;
    std::unique_ptr<TrackController> trackController;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioEngine)
};

} // namespace ikaros
