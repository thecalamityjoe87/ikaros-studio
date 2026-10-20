#pragma once

#include "engine/EngineTypes.h"
#include <juce_audio_devices/juce_audio_devices.h>
#include <atomic>
#include <functional>

namespace ikaros
{

//==============================================================================
/** The waveform of an audio take while it's being recorded (see
    AudioEngine::getLiveRecording). Empty when nothing is being recorded. */
struct LiveRecording
{
    TrackId track = 0;              // the track being recorded onto
    std::vector<double> beats;      // where each slice starts on the timeline, rising
    std::vector<float> peaks;       // the loudest sample (0..1) of each slice

    bool isEmpty() const noexcept   { return peaks.empty(); }
};

//==============================================================================
/**
    Draws a take's waveform while it's still being recorded: the take's file
    can't be read until recording stops, so this listens to the input itself.

    Like the Tuner, it's one more callback on the audio device, beside
    Tracktion's. Two threads are involved:

      - The audio thread: while a take is being recorded, it finds the
        loudest sample in each 10 ms slice of the armed track's input
        channels and pushes that one number into a lock-free FIFO
        (juce::AbstractFifo). No allocation, no locks, no waiting.

      - The message thread: collect() (from the UI's timer, through
        AudioEngine) takes what's in the FIFO and places it on the timeline.
        The newest slice ends where the transport is now, so the waveform
        lines up with the red recording region however late the timer runs.
        On a cycle (loop) recording the playhead jumps back, and each pass
        draws over the last.

    AudioEngine owns it; InputRouter tells it which input channels the armed
    track records from.
*/
class LiveWaveform  : private juce::AudioIODeviceCallback
{
public:
    explicit LiveWaveform (juce::AudioDeviceManager&);
    ~LiveWaveform() override;

    /** The device input channels the armed track records from (two for a
        stereo input), or -1, -1 for none. Set by InputRouter. */
    void setInputChannels (int first, int second);

    /** Starts a new waveform for a take on `track` that starts at
        `startSeconds` on the timeline (message thread). */
    void start (TrackId track, double startSeconds);

    /** Stops listening and forgets the waveform (message thread). */
    void stop();

    bool isRunning() const noexcept     { return running.load(); }

    /** Takes the slices the audio thread has found since the last call and
        places them on the timeline: the newest one ends at `nowSeconds`.
        `toBeats` turns a time on the timeline into a beat (message thread). */
    void collect (double nowSeconds, const std::function<double (double seconds)>& toBeats);

    /** The waveform so far (message thread). */
    const LiveRecording& get() const noexcept    { return recording; }

    /** After the device changes, so our callback runs after Tracktion's (see
        AudioEngine::reattachAudioCallbacks). */
    void reattach();

    static constexpr double sliceSeconds = 0.01;

    //==============================================================================
    // The audio thread's steps, public so the tests can drive them without a
    // real device.

    /** The sample rate of the input (normally from the audio device). */
    void prepare (double sampleRate);

    /** What the audio callback does with each block. Real-time safe. */
    void pushInput (const float* const* inputs, int numInputs, int numSamples) noexcept;

private:
    juce::AudioDeviceManager& deviceManager;
    std::atomic<bool> running { false };
    std::atomic<bool> resetRequested { false };   // start() asks the audio thread to begin a fresh slice
    std::atomic<int> firstChannel { -1 }, secondChannel { -1 };
    std::atomic<int> samplesPerSlice { 441 };

    // Audio thread only: the slice being measured.
    float slicePeak = 0.0f;
    int sliceSamples = 0;

    // Audio thread -> message thread: one float per finished slice. The
    // buffer is allocated once, up front; AbstractFifo only keeps track of
    // which part to write or read next.
    static constexpr int fifoSize = 1 << 12;    // 40 s of slices: far more than a timer tick
    juce::AbstractFifo fifo { fifoSize };
    std::vector<float> fifoBuffer;

    // Message thread only.
    double startSeconds = 0.0;
    LiveRecording recording;
    std::vector<float> incoming;   // scratch for reading the FIFO

    // juce::AudioIODeviceCallback
    void audioDeviceIOCallbackWithContext (const float* const* inputs, int numInputs,
                                           float* const* outputs, int numOutputs, int numSamples,
                                           const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart (juce::AudioIODevice*) override;
    void audioDeviceStopped() override {}

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LiveWaveform)
};

} // namespace ikaros
