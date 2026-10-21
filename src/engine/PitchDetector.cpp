#include "engine/PitchDetector.h"
#include <cmath>

namespace ikaros
{

void PitchDetector::prepare (double newSampleRate)
{
    sampleRate = newSampleRate;

    // Comparing every period with every other sample costs
    // integrationLength * maxPeriod operations a call, which grows with the
    // square of the sample rate. A guitar's fundamentals are all below
    // 1.5 kHz, so 88.2/96 kHz audio is analysed at half the rate and
    // 176.4/192 kHz at a quarter, which keeps the cost the same as 48 kHz.
    decimation = juce::jmax (1, (int) (sampleRate / 40000.0));
    const auto rate = sampleRate / decimation;

    minPeriod = juce::jmax (2, (int) std::floor (rate / maxFrequency));
    maxPeriod = (int) std::ceil (rate / minFrequency);

    // Compare one period of the lowest note: long enough for the lowest
    // string, short enough (about 20 ms) to follow a note as it settles.
    integrationLength = maxPeriod;

    // +2: room for the interpolation either side of the last period.
    const auto analysisLength = integrationLength + maxPeriod + 2;
    windowSize = analysisLength * decimation;

    analysisBuffer.assign ((size_t) analysisLength, 0.0f);
    difference.assign ((size_t) maxPeriod + 2, 0.0f);
    normalised.assign ((size_t) maxPeriod + 2, 0.0f);
}

std::optional<PitchDetector::Result> PitchDetector::detect (const float* samples, int numSamples)
{
    if (windowSize == 0 || numSamples < windowSize)
        return std::nullopt;

    // Take the newest windowSize samples, averaging each group of
    // `decimation` (a rough low-pass filter, plenty for finding a pitch).
    samples += numSamples - windowSize;
    double energy = 0.0;

    for (size_t i = 0; i < analysisBuffer.size(); ++i)
    {
        float sum = 0.0f;

        for (int j = 0; j < decimation; ++j)
            sum += samples[(int) i * decimation + j];

        analysisBuffer[i] = sum / (float) decimation;
        energy += analysisBuffer[i] * analysisBuffer[i];
    }

    // Too quiet to be a note being played (below about -60 dBFS RMS).
    const auto rms = std::sqrt (energy / (double) analysisBuffer.size());

    if (rms < 0.001)
        return std::nullopt;

    // Step 1: the difference function. d(tau) = how much the sound differs
    // from itself shifted by tau samples (0 = identical).
    // Step 2: normalise each by the average of the shorter ones ("cumulative
    // mean normalised difference"), so a period scores near 0 and the
    // threshold below means the same thing at every pitch and level.
    const auto* x = analysisBuffer.data();
    difference[0] = 0.0f;
    normalised[0] = 1.0f;
    double runningSum = 0.0;

    for (int tau = 1; tau <= maxPeriod + 1; ++tau)
    {
        double d = 0.0;

        for (int j = 0; j < integrationLength; ++j)
        {
            const auto delta = x[j] - x[j + tau];
            d += delta * delta;
        }

        runningSum += d;
        difference[(size_t) tau] = (float) d;
        normalised[(size_t) tau] = runningSum > 0.0 ? (float) (d * tau / runningSum) : 1.0f;
    }

    // Step 3: the first dip below the threshold, followed down to its lowest
    // point. Taking the first (shortest) period that repeats well avoids
    // octave errors; a longer one would be a multiple of it.
    constexpr float threshold = 0.15f;
    int tau = -1;

    for (int t = minPeriod; t <= maxPeriod; ++t)
    {
        if (normalised[(size_t) t] < threshold)
        {
            while (t + 1 <= maxPeriod && normalised[(size_t) t + 1] < normalised[(size_t) t])
                ++t;

            tau = t;
            break;
        }
    }

    if (tau < 0)
        return std::nullopt;   // nothing repeats clearly: noise, or a chord

    // Step 4: the true period is usually between two samples. Fit a parabola
    // through the dip and its neighbours and take its lowest point. (On the
    // plain difference: the normalising leans the curve slightly, which
    // would pull short periods, the highest notes, off by a cent or so.)
    auto refined = (double) tau;
    const auto a = difference[(size_t) tau - 1], b = difference[(size_t) tau], c = difference[(size_t) tau + 1];
    const auto curve = a - 2.0f * b + c;

    if (curve > 0.0f)
        refined += 0.5 * (a - c) / curve;

    Result result;
    result.frequency = sampleRate / decimation / refined;
    result.clarity = juce::jlimit (0.0f, 1.0f, 1.0f - normalised[(size_t) tau]);

    if (result.frequency < minFrequency * 0.97 || result.frequency > maxFrequency * 1.03)
        return std::nullopt;

    return result;
}

//==============================================================================
NotePitch NotePitch::fromFrequency (double hz)
{
    // MIDI note numbers are semitones: note 69 is A4 (440 Hz), and each
    // octave (twice the frequency) is 12 notes.
    const auto exactNote = 69.0 + 12.0 * std::log2 (hz / 440.0);

    NotePitch p;
    p.midiNote = (int) std::lround (exactNote);
    p.cents = (float) ((exactNote - p.midiNote) * 100.0);
    return p;
}

juce::String NotePitch::getNoteName() const
{
    static const char* const names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return names[((midiNote % 12) + 12) % 12];
}

} // namespace ikaros
