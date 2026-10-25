#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <atomic>

namespace ikaros
{

/*  What the toolbar warns about (see Toolbar::update): the busiest audio
    block lately, and xruns.

    JUCE's CPU figure (AudioDeviceManager::getCpuUsage) is a smoothed
    average, so one block that takes too long hardly moves it. This times
    every block on its own, over the same span JUCE times: a callback added
    before Tracktion's runs first, and one added straight after it runs last
    (JUCE calls the first callback first, then the others from the last added
    backwards; PerfMonitor brackets the callback the same way).

    On the audio thread it only reads the clock and updates one atomic: no
    locks, no allocation, no system calls.

    xruns are JUCE's count (blocks that took longer than the buffer lasts,
    plus what the device reports), without each device start's first second:
    opening a device often costs a block or two, and that isn't the
    session's fault.
*/
class AudioLoadMeter
{
public:
    /** Call before Tracktion's DeviceManager::initialise. With timeBlocks
        false (PerfMonitor already brackets the callback, and two pairs can't
        both run first and last) blocks aren't timed: takePeak stays at 0. */
    AudioLoadMeter (juce::AudioDeviceManager&, bool timeBlocks);
    ~AudioLoadMeter();

    /** The busiest block since the last call, as a share of the block's
        length (1 = it took as long as the buffer lasts). Message thread. */
    float takePeak();

    /** xruns since the app started. Message thread, called regularly (it
        notices new ones by comparing with JUCE's count). */
    int getXruns();

    // What the callbacks do, public for the smoke test, which has no real device.
    void deviceStarting (double sampleRate);            // any thread
    void blockDone (double seconds, int numSamples);    // audio thread
    int addXruns (int juceCount);                       // message thread; returns getXruns' total

private:
    juce::AudioDeviceManager& deviceManager;
    const bool timeBlocks;

    struct StartCallback  : juce::AudioIODeviceCallback
    {
        explicit StartCallback (AudioLoadMeter& o) : owner (o) {}
        void audioDeviceIOCallbackWithContext (const float* const*, int, float* const*, int, int,
                                               const juce::AudioIODeviceCallbackContext&) override;
        void audioDeviceAboutToStart (juce::AudioIODevice*) override;
        void audioDeviceStopped() override {}
        AudioLoadMeter& owner;
    };

    struct EndCallback  : juce::AudioIODeviceCallback
    {
        explicit EndCallback (AudioLoadMeter& o) : owner (o) {}
        void audioDeviceIOCallbackWithContext (const float* const*, int, float* const*, int, int,
                                               const juce::AudioIODeviceCallbackContext&) override;
        void audioDeviceAboutToStart (juce::AudioIODevice*) override {}
        void audioDeviceStopped() override {}
        AudioLoadMeter& owner;
    };

    StartCallback startCallback { *this };
    EndCallback endCallback { *this };

    // Audio thread only (set in StartCallback, read in EndCallback).
    juce::int64 blockStart = 0;

    // Shared between threads.
    std::atomic<double> sampleRate { 0.0 };
    std::atomic<float> peak { 0.0f };
    std::atomic<juce::uint32> startedMs { 0 };   // juce::Time::getMillisecondCounter at the last device start

    // Message thread only.
    int lastJuceCount = 0, xruns = 0;

    JUCE_DECLARE_NON_COPYABLE (AudioLoadMeter)
};

} // namespace ikaros
