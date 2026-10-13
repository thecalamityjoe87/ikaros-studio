#pragma once

#include "engine/RecordingTypes.h"
#include <juce_audio_devices/juce_audio_devices.h>
#include <atomic>
#include <memory>
#include <optional>

namespace ikaros
{

class AudioEngine;

//==============================================================================
/**
    Looks after takes while they record, and stops them cleanly (keeping
    what was recorded) when something goes wrong:

      - the audio device stops: closed, or it just goes quiet. (On Linux,
        JUCE's ALSA driver gives up silently when a device disappears, so
        "no audio callbacks for a while" is the only sign.)
      - a different device, buffer size or sample rate is picked mid-take;
      - free disk space runs low (checked every second);
      - Tracktion stopped the take by itself (it couldn't write to disk).

    It also counts dropouts (the driver's xrun count, plus gaps where the
    audio callbacks fell behind the clock) and notices when the input clips.
    After each take it leaves a RecordingReport for the UI.

    It listens to the audio device as one more juce::AudioIODeviceCallback
    (with silent output), which is how it sees blocks arrive and the device
    stop and start. Owned by AudioEngine; everything but the audio callback
    runs on the message thread.

    Lining takes up: Tracktion moves each take back by the input + output
    latency the driver reports, plus one block (the loopback test confirms
    it to the sample). Whether that extra block is right on real hardware
    depends on the driver: on Linux, JUCE's ALSA driver doesn't ask the
    hardware, it guesses (period size x (periods - 1) each way), and
    through PipeWire's ALSA plug-in the PipeWire graph adds its own buffer
    on top, unreported. So on a real interface takes can be a few
    milliseconds off either way; only a cable loopback on that interface
    can tell.
*/
class RecordingGuard  : private juce::AudioIODeviceCallback,
                        private juce::Timer
{
public:
    explicit RecordingGuard (AudioEngine&);
    ~RecordingGuard() override;

    /** Before a take: false if there's too little disk space to record.
        A report saying so is then waiting for the UI. */
    bool canStartTake();

    /** Record was refused for another reason (e.g. loopTooShort): leaves a
        report saying so. */
    void refuseTake (RecordingStopReason);

    /** A take is starting. inputId is the audio input it records from
        (empty for a MIDI take). */
    void takeStarting (const juce::String& inputId);

    /** The take has ended and its region is in place: makes its report. */
    void takeEnded();

    bool isTakeInProgress() const noexcept      { return takeInProgress; }

    /** Each report once, for the UI to show. */
    std::optional<RecordingReport> popReport()  { return std::exchange (report, std::nullopt); }

    AudioDeviceStatus getDeviceStatus() const;

    /** Moves its device callback to the back of the queue (see
        AudioEngine::reattachAudioCallbacks). */
    void reattach();

    /** Recording stops (and won't start) with less free disk space than this. */
    void setMinimumFreeSpace (juce::int64 bytes)    { minimumFreeSpace = bytes; }
    static constexpr juce::int64 defaultMinimumFreeSpace = 200 * 1024 * 1024;

private:
    AudioEngine& owner;
    struct Impl;                    // Tracktion-typed members live in the .cpp
    std::unique_ptr<Impl> impl;

    juce::int64 minimumFreeSpace = defaultMinimumFreeSpace;

    // The take in progress
    bool takeInProgress = false;
    bool audioTake = false;
    bool clippedThisTake = false;
    bool watchForStall = false;     // the device was running when the take started
    double takeStartTime = 0.0;     // seconds, Time::getMillisecondCounterHiRes() / 1000
    int xrunsAtStart = 0, gapsAtStart = 0;
    int ticks = 0;
    std::optional<RecordingStopReason> stopReason;   // set when we stop the take ourselves

    // The device the take started on, to notice a change
    juce::String takeDeviceName;
    double takeSampleRate = 0.0;
    int takeBufferSize = 0;

    std::optional<RecordingReport> report;
    double reportTime = 0.0;

    // Written on the audio thread, read on the message thread. std::atomic
    // makes that safe without locks (the audio thread must never wait).
    std::atomic<double> lastCallbackTime { 0.0 };
    std::atomic<int> gaps { 0 };
    std::atomic<bool> restartClock { true };
    std::atomic<double> sampleRate { 0.0 };
    std::atomic<double> gapThreshold { 0.1 };

    // Audio thread only: the clock the callbacks are measured against.
    double clockStart = 0.0, minLag = 0.0;
    juce::int64 samplesSinceStart = 0;

    void stopTake (RecordingStopReason);
    void checkClipping();
    int getDeviceXruns() const;

    // juce::AudioIODeviceCallback
    void audioDeviceIOCallbackWithContext (const float* const* inputs, int numInputs,
                                           float* const* outputs, int numOutputs, int numSamples,
                                           const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart (juce::AudioIODevice*) override;
    void audioDeviceStopped() override;

    void timerCallback() override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RecordingGuard)
};

} // namespace ikaros
