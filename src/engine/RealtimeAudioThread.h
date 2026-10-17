#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <atomic>

namespace ikaros
{

/*  Keeps the audio device's thread at real-time priority (see
    platform::makeThreadRealtime for why and how).

    The thread only shows itself once the device is running, and a new one
    starts whenever the device is reopened (a different buffer size, say).
    So this listens to the device like any other audio callback (silently),
    notes which thread calls it, and asks for the priority from the message
    thread, as asking means waiting on D-Bus, which the audio thread mustn't.
*/
class RealtimeAudioThread  : private juce::AudioIODeviceCallback,
                             private juce::Timer
{
public:
    explicit RealtimeAudioThread (juce::AudioDeviceManager&);
    ~RealtimeAudioThread() override;

    /** Moves this callback after the others again (see AudioEngine::reattachAudioCallbacks). */
    void reattach();

    /** Whether the audio thread is real-time right now (see platform::isThreadRealtime). */
    std::optional<bool> isRealtime() const;

private:
    juce::AudioDeviceManager& deviceManager;

    // Written on the audio thread, read on the message thread.
    std::atomic<juce::Thread::ThreadID> audioThread { nullptr };
    std::atomic<std::int64_t> audioThreadId { 0 };

    std::int64_t promotedId = 0;   // message thread only

    void audioDeviceIOCallbackWithContext (const float* const* inputs, int numInputs,
                                           float* const* outputs, int numOutputs, int numSamples,
                                           const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart (juce::AudioIODevice*) override;
    void audioDeviceStopped() override;

    void timerCallback() override;
};

} // namespace ikaros
