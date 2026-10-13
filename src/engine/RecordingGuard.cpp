#include "engine/RecordingGuard.h"
#include "engine/AudioEngine.h"
#include "engine/TrackHelpers.h"

namespace te = tracktion;

namespace ikaros
{

namespace
{
    double nowSeconds()     { return juce::Time::getMillisecondCounterHiRes() * 0.001; }

    // No audio callbacks for this long during a take means the device has stopped.
    constexpr double stallSeconds = 1.5;
}

//==============================================================================
struct RecordingGuard::Impl
{
    // Watches the take's input for clipping. WeakReference: input devices
    // are rebuilt when the audio device changes.
    juce::WeakReference<te::LevelMeasurer> measurer;
    te::LevelMeasurer::Client client;

    void detach()
    {
        if (auto* m = measurer.get())
            m->removeClient (client);

        measurer = nullptr;
    }

    ~Impl()     { detach(); }
};

//==============================================================================
RecordingGuard::RecordingGuard (AudioEngine& o)
    : owner (o), impl (std::make_unique<Impl>())
{
    owner.getTeEngine().getDeviceManager().deviceManager.addAudioCallback (this);
}

RecordingGuard::~RecordingGuard()
{
    stopTimer();
    owner.getTeEngine().getDeviceManager().deviceManager.removeAudioCallback (this);
}

void RecordingGuard::reattach()
{
    auto& adm = owner.getTeEngine().getDeviceManager().deviceManager;
    adm.removeAudioCallback (this);
    adm.addAudioCallback (this);
}

//==============================================================================
bool RecordingGuard::canStartTake()
{
    const auto free = owner.getWorkingCopyFolder().getBytesFreeOnVolume();

    // 0 means "couldn't tell": then let it record (Tracktion stops the take
    // itself if writing fails).
    if (free > 0 && free < minimumFreeSpace)
    {
        refuseTake (RecordingStopReason::notStarted);
        return false;
    }

    return true;
}

void RecordingGuard::refuseTake (RecordingStopReason reason)
{
    report = RecordingReport { .reason = reason };
    reportTime = nowSeconds();
}

void RecordingGuard::takeStarting (const juce::String& inputId)
{
    auto& dm = owner.getTeEngine().getDeviceManager();
    auto* device = dm.deviceManager.getCurrentAudioDevice();

    takeInProgress = true;
    audioTake = inputId.isNotEmpty();
    clippedThisTake = false;
    stopReason.reset();
    report.reset();
    ticks = 0;
    takeStartTime = nowSeconds();

    // Watch for the device going quiet only if it's running now; with no
    // device at all, nothing records anyway.
    watchForStall = device != nullptr && device->isPlaying();
    takeDeviceName = device != nullptr ? device->getName() : juce::String();
    takeSampleRate = device != nullptr ? device->getCurrentSampleRate() : 0.0;
    takeBufferSize = device != nullptr ? device->getCurrentBufferSizeSamples() : 0;

    // Count dropouts from here: restart the callback clock (so the wait
    // before the take isn't one) and note the driver's xrun count.
    restartClock = true;
    gapsAtStart = gaps;
    xrunsAtStart = getDeviceXruns();

    impl->detach();

    if (audioTake)
    {
        for (auto* in : dm.getWaveInputDevices())
        {
            if (in->getDeviceID() == inputId)
            {
                in->levelMeasurer.addClient (impl->client);
                impl->measurer = &in->levelMeasurer;
                break;
            }
        }
    }

    startTimer (250);
}

void RecordingGuard::takeEnded()
{
    if (! takeInProgress)
        return;

    checkClipping();
    impl->detach();
    stopTimer();
    takeInProgress = false;

    RecordingReport r;
    r.reason = stopReason.value_or (RecordingStopReason::user);
    r.audio = audioTake;
    r.clipped = clippedThisTake;
    r.dropouts = juce::jmax (0, getDeviceXruns() - xrunsAtStart, gaps - gapsAtStart);
    r.seconds = nowSeconds() - takeStartTime;

    report = r;
    reportTime = nowSeconds();
    stopReason.reset();
}

void RecordingGuard::stopTake (RecordingStopReason reason)
{
    if (! takeInProgress)
        return;

    stopReason = reason;

    // AudioEngine::stop() ends the take the usual way (keeping it), and
    // calls takeEnded(). If the transport had already stopped, finish the
    // take ourselves.
    if (auto* edit = owner.getEdit(); edit != nullptr && edit->getTransport().isRecording())
        owner.stop();

    if (takeInProgress)
        owner.finishTake();
}

void RecordingGuard::checkClipping()
{
    if (impl->measurer == nullptr)
        return;

    for (int chan = 0; chan < impl->client.getNumChannelsUsed(); ++chan)
        if (impl->client.getAndClearAudioLevel (chan).dB >= clipLevelDb)
            clippedThisTake = true;
}

int RecordingGuard::getDeviceXruns() const
{
    // -1 = the driver doesn't count them.
    if (auto* device = owner.getTeEngine().getDeviceManager().deviceManager.getCurrentAudioDevice())
        return juce::jmax (0, device->getXRunCount());

    return 0;
}

void RecordingGuard::timerCallback()
{
    if (! takeInProgress)
    {
        stopTimer();
        return;
    }

    // Tracktion stops every transport itself when a take can't be written
    // (or its own disk check fires). The take is kept; finish it our way.
    if (auto* edit = owner.getEdit(); edit == nullptr || ! edit->getTransport().isRecording())
    {
        stopReason = RecordingStopReason::diskWriteFailed;
        owner.finishTake();
        return;
    }

    checkClipping();

    if (watchForStall && nowSeconds() - juce::jmax (lastCallbackTime.load(), takeStartTime) > stallSeconds)
    {
        stopTake (RecordingStopReason::deviceStopped);
        return;
    }

    // Free space, once a second (a take uses ~0.3 MB a second per channel).
    if (++ticks % 4 == 0)
    {
        const auto free = owner.getWorkingCopyFolder().getBytesFreeOnVolume();

        if (free > 0 && free < minimumFreeSpace)
            stopTake (RecordingStopReason::diskFull);
    }
}

//==============================================================================
// The audio device
void RecordingGuard::audioDeviceIOCallbackWithContext (const float* const*, int,
                                                       float* const* outputs, int numOutputs, int numSamples,
                                                       const juce::AudioIODeviceCallbackContext&)
{
    // Every callback's output is mixed into what you hear, so ours must be silent.
    for (int i = 0; i < numOutputs; ++i)
        if (outputs[i] != nullptr)
            juce::FloatVectorOperations::clear (outputs[i], numSamples);

    const auto now = nowSeconds();
    lastCallbackTime = now;

    const auto rate = sampleRate.load();

    if (rate <= 0.0)
        return;

    // Dropouts: compare the audio that arrived with the time that passed.
    // "lag" (time passed minus audio received) jitters a little but stays
    // level; when the device drops audio it jumps up by the time lost.
    // (Faster than real time, as in the tests, it just keeps falling.)
    if (restartClock.exchange (false))
    {
        clockStart = now;
        samplesSinceStart = 0;
        minLag = 0.0;
    }

    const auto lag = (now - clockStart) - (double) samplesSinceStart / rate;
    samplesSinceStart += numSamples;

    if (lag - minLag > gapThreshold)
    {
        ++gaps;
        minLag = lag;
    }
    else
    {
        minLag = juce::jmin (minLag, lag);
    }
}

void RecordingGuard::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    const auto rate = device->getCurrentSampleRate();
    const auto block = device->getCurrentBufferSizeSamples();

    // A gap must be well over the normal jitter: several blocks (PipeWire
    // can deliver them in bursts) and at least 80 ms.
    gapThreshold = juce::jmax (0.08, 4.0 * block / juce::jmax (1.0, rate));
    sampleRate = rate;
    restartClock = true;
    lastCallbackTime = nowSeconds();
    gaps = 0;

    auto reasonFor = [&]
    {
        return device->getName() == takeDeviceName && rate != takeSampleRate ? RecordingStopReason::sampleRateChanged
                                                                              : RecordingStopReason::deviceChanged;
    };

    // Takes and reports belong to the message thread (where settings change).
    if (! juce::MessageManager::existsAndIsCurrentThread())
        return;

    if (takeInProgress)
    {
        // Some devices change settings without stopping first.
        if (device->getName() != takeDeviceName || rate != takeSampleRate || block != takeBufferSize)
            stopTake (reasonFor());
    }
    else if (report && report->reason == RecordingStopReason::deviceStopped && nowSeconds() - reportTime < 2.0)
    {
        // The take stopped when the device closed; now we know why it closed.
        report->reason = reasonFor();
    }
}

void RecordingGuard::audioDeviceStopped()
{
    sampleRate = 0.0;

    // Stop the take now, while its input is still there, so it's kept.
    // (This is called on the message thread when the settings change; the
    // take can't be stopped from any other thread.)
    if (juce::MessageManager::existsAndIsCurrentThread() && takeInProgress)
        stopTake (RecordingStopReason::deviceStopped);
}

//==============================================================================
AudioDeviceStatus RecordingGuard::getDeviceStatus() const
{
    AudioDeviceStatus s;
    auto& dm = owner.getTeEngine().getDeviceManager();
    auto* device = dm.deviceManager.getCurrentAudioDevice();

    if (device == nullptr || device->getCurrentSampleRate() <= 0.0)
        return s;

    const auto rate = device->getCurrentSampleRate();
    auto toMs = [rate] (int samples) { return samples * 1000.0 / rate; };

    s.running = device->isPlaying() && sampleRate > 0.0;
    s.deviceName = device->getName();
    s.sampleRate = rate;
    s.bufferSize = device->getCurrentBufferSizeSamples();
    s.bufferMs = toMs (s.bufferSize);
    s.inputLatencyMs = toMs (device->getInputLatencyInSamples());
    s.outputLatencyMs = toMs (device->getOutputLatencyInSamples());

    // Plug-ins that look ahead (e.g. some compressors) delay everything,
    // the monitored input included; recordings are moved back by it too.
    double pluginMs = 0.0;

    if (auto* edit = owner.getEdit())
        if (auto* context = edit->getCurrentPlaybackContext())
            pluginMs = toMs (context->getLatencySamples());

    s.monitoringLatencyMs = s.inputLatencyMs + s.outputLatencyMs + pluginMs;
    s.recordingCompensationMs = dm.getRecordAdjustmentMs() + pluginMs;
    s.dropouts = juce::jmax (getDeviceXruns(), gaps.load());
    return s;
}

} // namespace ikaros
