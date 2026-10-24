#pragma once

#include "engine/BuiltInEffects.h"
#include <atomic>

namespace LV2AT { class Retuner; }   // external/fat1/retuner.h

namespace ikaros
{

//==============================================================================
/**
    Tune: real-time pitch correction for a vocal track, locked to the song's
    key. The work is done by fat1's retuner (Fons Adriaensen's zita-at1, see
    external/fat1): it finds the pitch of what's sung, works out the nearest
    note it's allowed to land on, and moves the sound there.

    Knobs (automatable):
      Correction  0..100%: how far each note is moved towards its target (fat1's corr)
      Speed       Natural .. Robotic: how quickly the correction follows the
                  voice (fat1's filter, inverted: 0.5 s down to 0.02 s), with
                  fat1's fast mode at the very top
      Fine Tune   -50..+50 cents on top of the target (fat1's offset)

    Settings (saved and undoable, not automatable):
      Key          the song's key (the default, -1), or one of its own
                   (root + 12 for minor)
      Lock to Key  on: the notes it can land on are the key's scale; off: all 12
      Note edits   notes switched on or off by hand, on top of that (a 12-bit
                   mask of the notes that differ from it). Changing the key
                   keeps them; switching Lock to Key clears them.
      Follow MIDI  a MIDI instrument track whose notes, while they sound,
                   are the targets instead (fat1's MIDI mode). Between its
                   notes the allowed notes are used.

    It processes one channel (the left and right mixed) and plays it on both.
    The retuner delays the sound by about 1024 samples at 44.1 and 48 kHz
    (2048 at 88.2 and 96 kHz), which is reported as latency so Tracktion
    lines it up with the other tracks. Fast mode reads the input earlier;
    here the difference is delayed back, so the latency never changes
    (Tracktion can't change a plug-in's latency while playing).

    Threading: settings and the MIDI notes are written on the message
    thread. applyToBuffer (the audio thread) reads the knobs through
    getCurrentValue(), the allowed notes through an atomic, and the MIDI
    notes under a try-lock (if the message thread is swapping them in that
    instant, the block keeps the previous mask: it never waits).
*/
class TunePlugin  : public BuiltInEffectPlugin
{
public:
    explicit TunePlugin (tracktion::PluginCreationInfo);
    ~TunePlugin() override;

    static const char* xmlTypeName;      // "ikarosTune"
    static const char* getPluginName()   { return "Tune"; }

    juce::String getName() const override                   { return getPluginName(); }
    juce::String getPluginType() override                   { return xmlTypeName; }
    juce::String getShortName (int) override                { return "Tune"; }
    double getLatencySeconds() override;
    bool noTail() override                                  { return false; }
    double getTailLength() const override                   { return 0.1; }

    void initialise (const tracktion::PluginInitialisationInfo&) override;
    void deinitialise() override;
    void reset() override;
    void applyToBuffer (const tracktion::PluginRenderContext&) override;
    void restorePluginStateFromValueTree (const juce::ValueTree&) override;

    //==============================================================================
    // Settings (message thread). The setters go into the current undo step
    // (EffectsController starts one for each).
    static constexpr int songKey = -1;

    int getKey() const                      { return key.get(); }
    void setKey (int keyOrSongKey);
    bool getLockToKey() const               { return lockToKey.get(); }
    void setLockToKey (bool);
    void toggleNote (int pitchClass);
    juce::int64 getFollowTrack() const      { return followTrack.get(); }
    void setFollowTrack (juce::int64 trackIdOrZero);

    /** The key in use: the song's or the effect's own (root 0..11, minor). */
    std::pair<int, bool> getEffectiveKey() const;

    /** The notes it can land on: bit 0 = C ... bit 11 = B. */
    int getAllowedNotes() const             { return allowedNotes.load(); }

    /** The notes of a major or minor scale (natural minor). */
    static int scaleNotes (int root, bool minor);

    //==============================================================================
    // Presets: Natural (Correction 60%, Speed 30%) and Hard Tune (100%, 100%).
    static juce::StringArray getPresetNames();
    void applyPreset (int index);           // into the current undo step

    //==============================================================================
    /** What the readout shows. Read from the audio thread's atomics. */
    struct Reading
    {
        bool voiced = false;
        float sungNote = 0.0f;   // fractional MIDI note
        int targetNote = -1;     // MIDI note
    };

    Reading getReading() const;

    /** Text for the Speed knob (Natural, a percentage, Robotic). */
    static juce::String speedText (float zeroToOne);

private:
    struct EditWatcher;

    tracktion::AutomatableParameter::Ptr correction, speed, fineTune;
    juce::CachedValue<int> key, noteEdits;
    juce::CachedValue<bool> lockToKey;
    juce::CachedValue<juce::int64> followTrack;

    // The retuner, made in initialise() for the sample rate (it allocates).
    std::unique_ptr<LV2AT::Retuner> retuner;
    double rate = 44100.0;
    double latencySamples = 0.0;

    // Audio-thread scratch and the fast-mode delay (see the class comment).
    std::vector<float> mono, tuned, delayLine;
    int delayWrite = 0, fastLead = 0;
    int currentDelay = 0, fadingFrom = 0, fadeLeft = 0;
    static constexpr int fadeLength = 128;

    // What the audio thread reads.
    std::atomic<int> allowedNotes { 0xfff };
    std::atomic<bool> followingMidi { false };   // Follow MIDI is set to a track
    std::atomic<bool> readingVoiced { false };
    std::atomic<float> readingSung { 0.0f };
    std::atomic<int> readingTarget { -1 };

    // The follow track's notes: start and end (edit seconds) and pitch class,
    // sorted by start. Swapped in under notesLock (see the class comment).
    struct MidiNote { double start, end; int pitchClass; };
    std::vector<MidiNote> midiNotes;
    juce::SpinLock notesLock;
    int lastMidiMask = 0;

    std::unique_ptr<EditWatcher> watcher;   // song key, follow track and tempo changes

    void updateAllowedNotes();
    void rebuildMidiNotes();
    int midiMaskAt (double editSeconds);
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TunePlugin)
};

} // namespace ikaros
