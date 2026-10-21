#include "engine/Tuner.h"
#include <algorithm>
#include <cmath>

namespace ikaros
{

namespace
{
    constexpr int analysisIntervalMs = 50;      // 20 readings a second

    // Steadying (see Tuner::analyse).
    constexpr double sameNoteRange = 0.6;       // semitones: within this, it's the same note moving
    constexpr double smoothing = 0.3;           // how far each reading moves the needle toward a new one
    constexpr double noteSwitchMargin = 0.55;   // semitones from the shown note before it shows the next
    constexpr int confirmCount = 2;             // readings that must agree before a new note shows
    constexpr int missesBeforeClearing = 6;     // ~300 ms of nothing usable before it goes blank
}

Tuner::Tuner (juce::AudioDeviceManager& dm)
    : juce::Thread ("Tuner"), deviceManager (dm), fifoBuffer ((size_t) fifoSize, 0.0f)
{
    // Our callback is always attached (it does nothing while the tuner is
    // off); the background thread only runs while it's on.
    deviceManager.addAudioCallback (this);
}

Tuner::~Tuner()
{
    deviceManager.removeAudioCallback (this);
    stopThread (1000);
}

void Tuner::reattach()
{
    deviceManager.removeAudioCallback (this);
    deviceManager.addAudioCallback (this);
}

void Tuner::setActive (bool shouldBeActive)
{
    if (shouldBeActive == active.load())
        return;

    if (shouldBeActive)
    {
        resetSteadying();
        publish ({});
        active = true;           // the audio callback starts filling the FIFO
        startThread (juce::Thread::Priority::normal);
    }
    else
    {
        active = false;
        stopThread (1000);       // asks run() to finish and waits for it
        publish ({});
    }

    if (onActiveChanged)
        onActiveChanged();
}

void Tuner::setInputChannels (int first, int second)
{
    firstChannel = first;
    secondChannel = first >= 0 ? second : -1;
}

TunerReading Tuner::getReading() const
{
    const juce::SpinLock::ScopedLockType lock (readingLock);
    return reading;
}

void Tuner::publish (const TunerReading& r)
{
    const juce::SpinLock::ScopedLockType lock (readingLock);
    reading = r;
}

void Tuner::prepare (double sampleRate)
{
    deviceSampleRate = sampleRate;
}

//==============================================================================
// Audio thread
void Tuner::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    if (device != nullptr)
        prepare (device->getCurrentSampleRate());
}

void Tuner::audioDeviceIOCallbackWithContext (const float* const* inputs, int numInputs,
                                              float* const* outputs, int numOutputs, int numSamples,
                                              const juce::AudioIODeviceCallbackContext&)
{
    // We only listen: whatever we write to `outputs` is mixed into what plays
    // (see juce::AudioDeviceManager), so it must be silence.
    for (int i = 0; i < numOutputs; ++i)
        if (outputs[i] != nullptr)
            juce::FloatVectorOperations::clear (outputs[i], numSamples);

    if (active.load (std::memory_order_relaxed))
        pushInput (inputs, numInputs, numSamples);
}

void Tuner::pushInput (const float* const* inputs, int numInputs, int numSamples) noexcept
{
    const int first = firstChannel.load (std::memory_order_relaxed);
    int second = secondChannel.load (std::memory_order_relaxed);

    if (! juce::isPositiveAndBelow (first, numInputs) || inputs[first] == nullptr)
        return;

    if (! juce::isPositiveAndBelow (second, numInputs) || inputs[second] == nullptr)
        second = -1;

    // AbstractFifo hands out up to two stretches of the buffer to write
    // (two when the free space wraps round its end). If the background
    // thread has fallen behind and it's full, the rest of this block is
    // dropped: losing a few milliseconds only delays the next reading.
    int start1, size1, start2, size2;
    fifo.prepareToWrite (numSamples, start1, size1, start2, size2);

    auto copy = [&] (int destStart, int count, int sourceOffset)
    {
        auto* dest = fifoBuffer.data() + destStart;

        if (second < 0)
        {
            juce::FloatVectorOperations::copy (dest, inputs[first] + sourceOffset, count);
        }
        else
        {
            // A stereo input: listen to both sides, mixed.
            juce::FloatVectorOperations::copyWithMultiply (dest, inputs[first] + sourceOffset, 0.5f, count);
            juce::FloatVectorOperations::addWithMultiply (dest, inputs[second] + sourceOffset, 0.5f, count);
        }
    };

    if (size1 > 0)  copy (start1, size1, 0);
    if (size2 > 0)  copy (start2, size2, size1);

    fifo.finishedWrite (size1 + size2);
}

//==============================================================================
// Background thread
void Tuner::run()
{
    // Anything left in the FIFO from the last time is stale.
    fifo.finishedRead (fifo.getNumReady());

    while (! threadShouldExit())
    {
        analyse();

        // wait() returns early when stopThread() is called, so turning the
        // tuner off doesn't have to wait out the interval.
        wait (analysisIntervalMs);
    }
}

void Tuner::resetSteadying()
{
    showing = false;
    candidateCount = 0;
    missCount = 0;
}

void Tuner::analyse()
{
    // The device's sample rate can change while the tuner is on: start over.
    const auto rate = deviceSampleRate.load();

    if (rate <= 0.0)
        return;

    if (rate != preparedRate)
    {
        preparedRate = rate;
        detector.prepare (rate);
        history.assign ((size_t) detector.getWindowSize(), 0.0f);
        incoming.resize ((size_t) fifoSize);
        resetSteadying();
    }

    // Take everything the audio thread has written since last time and
    // append it to `history`, keeping only the newest window's worth.
    const int numReady = fifo.getNumReady();
    int start1, size1, start2, size2;
    fifo.prepareToRead (numReady, start1, size1, start2, size2);
    std::copy_n (fifoBuffer.data() + start1, size1, incoming.data());
    std::copy_n (fifoBuffer.data() + start2, size2, incoming.data() + size1);
    fifo.finishedRead (size1 + size2);

    const auto numNew = (size_t) juce::jmin (size1 + size2, (int) history.size());

    if (numNew > 0)
    {
        // Slide the old samples left, then put the new ones on the end.
        std::move (history.begin() + (std::ptrdiff_t) numNew, history.end(), history.begin());
        std::copy_n (incoming.data() + (size1 + size2 - (int) numNew), numNew, history.end() - (std::ptrdiff_t) numNew);
    }

    // Steadying. A raw detection now and then is wrong (the scratch of a
    // pick, an octave glitch as a note dies away), and the needle would
    // twitch if it showed every one. So:
    //   - while the note stays (within sameNoteRange), the needle eases
    //     toward each new reading instead of jumping;
    //   - a different note only shows once confirmCount readings in a row
    //     agree on it;
    //   - after missesBeforeClearing readings with nothing usable, the
    //     display goes blank rather than guessing.
    const auto detected = numNew > 0 ? detector.detect (history.data(), (int) history.size())
                                     : std::nullopt;   // no input arriving (device stopped?)
    bool usable = false;

    if (detected)
    {
        const auto exactNote = 69.0 + 12.0 * std::log2 (detected->frequency / 440.0);

        if (showing && std::abs (exactNote - smoothedNote) < sameNoteRange)
        {
            smoothedNote += smoothing * (exactNote - smoothedNote);
            candidateCount = 0;
            usable = true;
        }
        else
        {
            if (candidateCount > 0 && std::abs (exactNote - candidateNote) < sameNoteRange)
                ++candidateCount;
            else
                candidateCount = 1;

            candidateNote = exactNote;

            if (candidateCount >= confirmCount)
            {
                smoothedNote = exactNote;
                shownMidiNote = (int) std::lround (exactNote);
                showing = true;
                candidateCount = 0;
                usable = true;
            }
        }
    }

    if (usable)
        missCount = 0;
    else if (++missCount >= missesBeforeClearing)
        resetSteadying();

    if (! showing)
    {
        publish ({});
        return;
    }

    // Keep showing the same note name until the pitch is clearly nearer
    // the next one, so a string that's a quarter-tone out doesn't flicker
    // between two names.
    if (std::abs (smoothedNote - shownMidiNote) > noteSwitchMargin)
        shownMidiNote = (int) std::lround (smoothedNote);

    TunerReading r;
    r.hasPitch = true;
    r.frequency = 440.0 * std::pow (2.0, (smoothedNote - 69.0) / 12.0);
    r.note.midiNote = shownMidiNote;
    r.note.cents = juce::jlimit (-50.0f, 50.0f, (float) ((smoothedNote - shownMidiNote) * 100.0));
    publish (r);
}

} // namespace ikaros
