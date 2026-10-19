#pragma once

#include "engine/PitchDetector.h"
#include <juce_audio_devices/juce_audio_devices.h>
#include <atomic>
#include <cmath>
#include <functional>

namespace ikaros
{

//==============================================================================
/** What the tuner hears right now. */
struct TunerReading
{
    bool hasPitch = false;      // false: nothing to show (silence, noise, a chord)
    double frequency = 0.0;     // Hz
    NotePitch note;             // the nearest note and how far off it is

    /** Within a few cents: close enough that nobody can hear the difference. */
    static constexpr float inTuneCents = 3.0f;
    bool isInTune() const noexcept      { return hasPitch && std::abs (note.cents) <= inTuneCents; }
};

//==============================================================================
/**
    The guitar tuner's ears. While it's on, it listens to the selected
    track's audio input and works out which note is being played.

    Three threads are involved, and each does only its own part:

      - The audio thread (the sound card's callback): Tuner is one more
        callback on the audio device, beside Tracktion's, so it hears the
        input whether or not the track is monitoring. There it only copies
        samples into a lock-free FIFO (juce::AbstractFifo): no allocation,
        no locks, no waiting, so it can never make the audio drop out.
      - Its own background thread, while the tuner is on: about 20 times a
        second it takes what's in the FIFO and runs PitchDetector on the
        latest 40 ms, then steadies the result (see analyse()).
      - The message thread: the UI turns it on and off and polls
        getReading() on its timer.

    AudioEngine owns it. InputRouter tells it which input channels the
    selected track uses, and leaves that track's monitoring off while the
    tuner is on if "Mute while tuning" is set.
*/
class Tuner  : private juce::AudioIODeviceCallback,
               private juce::Thread
{
public:
    explicit Tuner (juce::AudioDeviceManager&);
    ~Tuner() override;

    /** Turns the tuner on or off (message thread). It only uses CPU while on. */
    void setActive (bool);
    bool isActive() const noexcept      { return active.load(); }

    /** Called (on the message thread) after the tuner turns on or off. */
    std::function<void()> onActiveChanged;

    /** The latest steadied reading. Message thread. */
    TunerReading getReading() const;

    /** The device input channels to listen to (two for a stereo input,
        which are mixed), or -1, -1 for none: the selected track isn't an
        audio track, or has no input. Set by InputRouter. */
    void setInputChannels (int first, int second);
    bool hasInput() const noexcept      { return firstChannel.load() >= 0; }

    /** After the device changes, so our callback runs after Tracktion's (see
        AudioEngine::reattachAudioCallbacks). */
    void reattach();

    //==============================================================================
    // The steps, public so the tests can drive them one at a time with
    // generated tones (they don't turn the tuner on, so no thread runs).

    /** The sample rate of the input (normally from the audio device). */
    void prepare (double sampleRate);

    /** What the audio callback does with each block: copy the chosen input
        channels into the FIFO. Real-time safe. */
    void pushInput (const float* const* inputs, int numInputs, int numSamples) noexcept;

    /** One step of the background thread: read the FIFO, detect, steady the
        result and publish it for getReading(). */
    void analyse();

private:
    juce::AudioDeviceManager& deviceManager;

    std::atomic<bool> active { false };
    std::atomic<int> firstChannel { -1 }, secondChannel { -1 };
    std::atomic<double> deviceSampleRate { 0.0 };

    // Audio thread -> background thread. AbstractFifo only does the
    // book-keeping (which part of the buffer to write or read next, kept in
    // atomics); the samples live in fifoBuffer, allocated once up front.
    static constexpr int fifoSize = 1 << 16;    // over 300 ms even at 192 kHz
    juce::AbstractFifo fifo { fifoSize };
    std::vector<float> fifoBuffer;

    // Only touched by whoever calls analyse() (the background thread).
    PitchDetector detector;
    double preparedRate = 0.0;
    std::vector<float> history;         // the latest samples, oldest first
    std::vector<float> incoming;        // scratch for reading the FIFO

    // Steadying (see analyse): where the note is (in semitones, as a
    // fractional MIDI note), a possible new note waiting to be confirmed,
    // and how many analyses in a row have found nothing usable.
    bool showing = false;
    double smoothedNote = 0.0;
    int shownMidiNote = 0;
    double candidateNote = 0.0;
    int candidateCount = 0;
    int missCount = 0;

    // Background thread -> message thread. A SpinLock is fine here: neither
    // of them is the audio thread, and the copy under it is tiny.
    mutable juce::SpinLock readingLock;
    TunerReading reading;

    void publish (const TunerReading&);
    void resetSteadying();

    // juce::Thread
    void run() override;

    // juce::AudioIODeviceCallback
    void audioDeviceIOCallbackWithContext (const float* const* inputs, int numInputs,
                                           float* const* outputs, int numOutputs, int numSamples,
                                           const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart (juce::AudioIODevice*) override;
    void audioDeviceStopped() override {}

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Tuner)
};

} // namespace ikaros
