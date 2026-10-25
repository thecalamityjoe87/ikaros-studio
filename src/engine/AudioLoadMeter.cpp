#include "engine/AudioLoadMeter.h"

namespace ikaros
{

AudioLoadMeter::AudioLoadMeter (juce::AudioDeviceManager& dm, bool shouldTimeBlocks)
    : deviceManager (dm), timeBlocks (shouldTimeBlocks)
{
    // Added before Tracktion's, these become JUCE's first two callbacks, so
    // `start` runs before everything and `end` after everything (see the
    // header). The start one is also how device starts are noticed, so it's
    // added either way; without the end one it times nothing.
    deviceManager.addAudioCallback (&startCallback);

    if (timeBlocks)
        deviceManager.addAudioCallback (&endCallback);
}

AudioLoadMeter::~AudioLoadMeter()
{
    deviceManager.removeAudioCallback (&endCallback);
    deviceManager.removeAudioCallback (&startCallback);
}

float AudioLoadMeter::takePeak()
{
    return peak.exchange (0.0f);
}

int AudioLoadMeter::getXruns()
{
    return addXruns (deviceManager.getXRunCount());
}

void AudioLoadMeter::deviceStarting (double rate)
{
    sampleRate = rate;
    startedMs = juce::Time::getMillisecondCounter();
}

void AudioLoadMeter::blockDone (double seconds, int numSamples)
{
    const auto rate = sampleRate.load (std::memory_order_relaxed);

    if (rate <= 0.0 || numSamples <= 0)
        return;

    const auto share = (float) (seconds * rate / numSamples);

    // A maximum the message thread may reset at any moment: compare-and-swap,
    // so a reset in between can't be undone by a stale value (and nothing waits).
    auto old = peak.load (std::memory_order_relaxed);

    while (share > old && ! peak.compare_exchange_weak (old, share, std::memory_order_relaxed))
    {
    }
}

int AudioLoadMeter::addXruns (int juceCount)
{
    // Unsigned, so this is right across the counter wrapping (every 49 days).
    const bool settling = juce::Time::getMillisecondCounter() - startedMs.load() < 1000;

    // JUCE counts from zero again when the device restarts.
    if (! settling)
        xruns += juceCount >= lastJuceCount ? juceCount - lastJuceCount : juceCount;

    lastJuceCount = juceCount;
    return xruns;
}

//==============================================================================
// Audio thread.

void AudioLoadMeter::StartCallback::audioDeviceIOCallbackWithContext (const float* const*, int,
                                                                     float* const* outputs, int numOutputs, int numSamples,
                                                                     const juce::AudioIODeviceCallbackContext&)
{
    owner.blockStart = juce::Time::getHighResolutionTicks();

    // JUCE adds every later callback's output to the first one's, so this is
    // the starting point of the mix: silence. (A real device's input and
    // output buffers are separate, so this doesn't wipe the input. The tests'
    // pretend device works in place, which is why the engine only makes one
    // of these when it opens a real device.)
    for (int i = 0; i < numOutputs; ++i)
        if (outputs[i] != nullptr)
            juce::FloatVectorOperations::clear (outputs[i], numSamples);
}

void AudioLoadMeter::StartCallback::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    owner.deviceStarting (device != nullptr ? device->getCurrentSampleRate() : 0.0);
}

void AudioLoadMeter::EndCallback::audioDeviceIOCallbackWithContext (const float* const*, int,
                                                                   float* const* outputs, int numOutputs, int numSamples,
                                                                   const juce::AudioIODeviceCallbackContext&)
{
    const auto ticks = juce::Time::getHighResolutionTicks() - owner.blockStart;

    // This one's output is added to the mix too, so it must be silent.
    for (int i = 0; i < numOutputs; ++i)
        if (outputs[i] != nullptr)
            juce::FloatVectorOperations::clear (outputs[i], numSamples);

    owner.blockDone (juce::Time::highResolutionTicksToSeconds (ticks), numSamples);
}

} // namespace ikaros
