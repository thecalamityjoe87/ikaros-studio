#include "engine/LiveWaveform.h"

namespace ikaros
{

LiveWaveform::LiveWaveform (juce::AudioDeviceManager& dm)
    : deviceManager (dm), fifoBuffer ((size_t) fifoSize, 0.0f)
{
    // Always attached; it does nothing unless a take is being recorded.
    deviceManager.addAudioCallback (this);
}

LiveWaveform::~LiveWaveform()
{
    deviceManager.removeAudioCallback (this);
}

void LiveWaveform::reattach()
{
    deviceManager.removeAudioCallback (this);
    deviceManager.addAudioCallback (this);
}

void LiveWaveform::setInputChannels (int first, int second)
{
    firstChannel = first;
    secondChannel = second;
}

void LiveWaveform::prepare (double sampleRate)
{
    if (sampleRate > 0.0)
        samplesPerSlice = juce::jmax (1, juce::roundToInt (sampleRate * sliceSeconds));
}

//==============================================================================
// Message thread
void LiveWaveform::start (TrackId track, double takeStartSeconds)
{
    stop();
    startSeconds = takeStartSeconds;
    recording.track = track;

    // Throw away whatever the audio thread left in the FIFO last time. Only
    // the reading side moves here (we're the only reader), so this is safe
    // even if the audio thread is in the middle of a block.
    fifo.finishedRead (fifo.getNumReady());
    resetRequested = true;
    running = true;
}

void LiveWaveform::stop()
{
    running = false;
    recording = {};
}

void LiveWaveform::collect (double nowSeconds, const std::function<double (double)>& toBeats)
{
    if (! running.load())
        return;

    const int ready = fifo.getNumReady();

    if (ready <= 0)
        return;

    // AbstractFifo hands back up to two stretches of the buffer (two when
    // what's ready wraps round its end).
    incoming.resize ((size_t) ready);
    int start1, size1, start2, size2;
    fifo.prepareToRead (ready, start1, size1, start2, size2);
    std::copy_n (fifoBuffer.data() + start1, size1, incoming.data());
    std::copy_n (fifoBuffer.data() + start2, size2, incoming.data() + size1);
    fifo.finishedRead (size1 + size2);

    // The newest slice ends now. Each slice goes in the place for its time
    // on the timeline; slices from before the take started (the count-in)
    // are left out, and on a cycle recording a later pass replaces what's
    // there.
    const auto count = (int) incoming.size();

    for (int i = 0; i < count; ++i)
    {
        const auto sliceStart = nowSeconds - (count - i) * sliceSeconds;
        const auto index = (int) std::floor ((sliceStart - startSeconds) / sliceSeconds + 0.5);

        if (index < 0)
            continue;

        // A gap (e.g. the timer was late and the FIFO filled up) stays silent.
        while ((int) recording.peaks.size() <= index)
        {
            recording.beats.push_back (toBeats (startSeconds + (double) recording.peaks.size() * sliceSeconds));
            recording.peaks.push_back (0.0f);
        }

        recording.peaks[(size_t) index] = incoming[(size_t) i];
    }
}

//==============================================================================
// Audio thread
void LiveWaveform::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    if (device != nullptr)
        prepare (device->getCurrentSampleRate());
}

void LiveWaveform::audioDeviceIOCallbackWithContext (const float* const* inputs, int numInputs,
                                                     float* const* outputs, int numOutputs, int numSamples,
                                                     const juce::AudioIODeviceCallbackContext&)
{
    // We only listen: whatever we write to `outputs` is mixed into what plays
    // (see juce::AudioDeviceManager), so it must be silence.
    for (int i = 0; i < numOutputs; ++i)
        if (outputs[i] != nullptr)
            juce::FloatVectorOperations::clear (outputs[i], numSamples);

    if (running.load (std::memory_order_relaxed))
        pushInput (inputs, numInputs, numSamples);
}

void LiveWaveform::pushInput (const float* const* inputs, int numInputs, int numSamples) noexcept
{
    if (resetRequested.exchange (false))
    {
        slicePeak = 0.0f;
        sliceSamples = 0;
    }

    const int first = firstChannel.load (std::memory_order_relaxed);
    const int second = secondChannel.load (std::memory_order_relaxed);
    const float* a = juce::isPositiveAndBelow (first, numInputs) ? inputs[first] : nullptr;
    const float* b = juce::isPositiveAndBelow (second, numInputs) ? inputs[second] : nullptr;
    const int perSlice = samplesPerSlice.load (std::memory_order_relaxed);

    for (int i = 0; i < numSamples; ++i)
    {
        // A stereo input: the louder side. No input at all: silence, so the
        // slices still keep time.
        const auto sample = juce::jmax (a != nullptr ? std::abs (a[i]) : 0.0f,
                                        b != nullptr ? std::abs (b[i]) : 0.0f);
        slicePeak = juce::jmax (slicePeak, sample);

        if (++sliceSamples < perSlice)
            continue;

        // A finished slice: one number into the FIFO. If the message thread
        // has fallen far behind and it's full, the slice is dropped (it's
        // drawn silent).
        int start1, size1, start2, size2;
        fifo.prepareToWrite (1, start1, size1, start2, size2);

        if (size1 > 0)
            fifoBuffer[(size_t) start1] = juce::jmin (1.0f, slicePeak);

        fifo.finishedWrite (size1);
        slicePeak = 0.0f;
        sliceSamples = 0;
    }
}

} // namespace ikaros
