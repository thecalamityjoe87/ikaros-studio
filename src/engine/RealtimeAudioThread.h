#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <array>
#include <atomic>

namespace ikaros
{

/*  Keeps the audio device's thread, and Tracktion's render workers, at
    real-time priority (see platform::makeThreadRealtime for why and how).

    The audio thread only shows itself once the device is running, and a new
    one starts whenever the device is reopened (a different buffer size, say).
    So this listens to the device like any other audio callback (silently),
    notes which thread calls it, and asks for the priority from the message
    thread, as asking means waiting on D-Bus, which the audio thread mustn't.

    The render workers share each block's work with the audio thread, which
    waits for them at the end of it: a worker at ordinary priority that the
    desktop keeps waiting holds up the audio thread too (the dropouts that
    made Ikaros render on one thread in October 2026). Tracktion can't make
    them real-time on Linux, so they're noted as they start (see
    trackRenderThreads) and get the same treatment as the audio thread.
*/
class RealtimeAudioThread  : private juce::AudioIODeviceCallback,
                             private juce::Timer
{
public:
    explicit RealtimeAudioThread (juce::AudioDeviceManager&);
    ~RealtimeAudioThread() override;

    /** Moves this callback after the others again (see AudioEngine::reattachAudioCallbacks). */
    void reattach();

    /** Whether the audio thread and all the render workers are real-time right
        now (see platform::isThreadRealtime).
    */
    std::optional<bool> isRealtime() const;

    /** Starts noting Tracktion's render workers as they start and stop. Call
        before any Edit plays; it's for every engine, with or without a
        RealtimeAudioThread.
    */
    static void trackRenderThreads();

    /** The kernel IDs of the render workers running now. */
    static std::vector<std::int64_t> getRenderThreadIds();

    static constexpr size_t maxRenderThreads = 15;   // + the audio thread = platform's 16 slots

private:
    juce::AudioDeviceManager& deviceManager;

    // Written on the audio thread, read on the message thread.
    std::atomic<juce::Thread::ThreadID> audioThread { nullptr };
    std::atomic<std::int64_t> audioThreadId { 0 };

    // One thread kept real-time. Message thread only.
    struct Promotion
    {
        std::int64_t id = 0;
        bool promoted = false;         // whether id was made real-time
        int calmTicks = 0;             // timer ticks in a row with a light load (see keepRealtime)
        int calmTicksNeeded = 0;       // before asking again; doubles each time (see keepRealtime)
    };

    Promotion audio;
    std::array<Promotion, maxRenderThreads> workers;

    void keepRealtime (Promotion&, std::int64_t id, const char* what);

    void audioDeviceIOCallbackWithContext (const float* const* inputs, int numInputs,
                                           float* const* outputs, int numOutputs, int numSamples,
                                           const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart (juce::AudioIODevice*) override;
    void audioDeviceStopped() override;

    void timerCallback() override;
};

} // namespace ikaros
