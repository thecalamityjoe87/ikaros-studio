#pragma once

#include <juce_core/juce_core.h>
#include <optional>
#include <vector>

namespace ikaros
{

//==============================================================================
/**
    Finds the pitch of a short stretch of a single note (a guitar string, a
    voice), for the tuner. No Tracktion and no threads here: Tuner feeds it,
    and the tests call it directly with generated tones.

    It uses the YIN method (de Cheveigné & Kawahara, 2002): for each possible
    period, measure how different the sound is from itself shifted by that
    period, and pick the shortest period where it's very nearly the same.
    Taking the *shortest* good period is what keeps it from reporting a low
    string an octave too low, and the "very nearly" threshold is how it tells
    a clear note from noise or a chord (then it reports nothing).

    It covers 50 Hz to 1500 Hz: below a 7-string's low B (62 Hz) up to the
    top fret of a guitar's high E string (1319 Hz).

    prepare() allocates; detect() doesn't. Neither may run on the audio thread
    (detect() does far too much work per call for that).
*/
class PitchDetector
{
public:
    static constexpr double minFrequency = 50.0, maxFrequency = 1500.0;

    /** Sets the sample rate of the audio that detect() will get. */
    void prepare (double sampleRate);

    /** How many of the latest samples detect() looks at (about 40 ms). */
    int getWindowSize() const noexcept      { return windowSize; }

    struct Result
    {
        double frequency = 0.0;     // Hz
        float clarity = 0.0f;       // 0..1: how sure it is (1 = a pure tone)
    };

    /** The pitch of the last getWindowSize() samples of `samples` (fewer
        than that gives nullopt), or nullopt if there's no clear pitch:
        silence, noise, or several notes at once. */
    std::optional<Result> detect (const float* samples, int numSamples);

private:
    double sampleRate = 0.0;    // of the input
    int decimation = 1;         // high sample rates are analysed at 1/2 or 1/4 of the rate
    int windowSize = 0;         // input samples looked at
    int minPeriod = 0, maxPeriod = 0;   // in analysis samples
    int integrationLength = 0;  // samples compared for each period

    std::vector<float> analysisBuffer;  // the window, after decimation
    std::vector<float> difference;      // YIN's d(tau), one per period
    std::vector<float> normalised;      // ...and d'(tau)
};

//==============================================================================
/** A detected frequency as a note: the nearest equal-tempered note to
    A4 = 440 Hz, and how far off it is. */
struct NotePitch
{
    int midiNote = 0;       // 69 = A4
    float cents = 0.0f;     // -50..50: flat is negative

    static NotePitch fromFrequency (double hz);

    /** "E", "F#"... (sharps, the way guitar tuners show them). */
    juce::String getNoteName() const;

    /** Scientific octave: the low E string is E2, A4 = 440 Hz. */
    int getOctave() const noexcept      { return midiNote / 12 - 1; }
};

} // namespace ikaros
