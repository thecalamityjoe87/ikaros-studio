#pragma once

#include <juce_core/juce_core.h>
#include <vector>

namespace ikaros
{

/** From `beat` on, the song plays at `bpm` (until the next change). Tempo
    changes are steps: there are no ramps. */
struct TempoChange
{
    double beat = 0.0;
    double bpm = 120.0;

    bool operator== (const TempoChange&) const = default;
};

/** From `beat` on, bars have `numerator` beats. `beat` is always the start
    of a bar, and `bar` is that bar's number (0 = the song's first bar). */
struct TimeSignatureChange
{
    double beat = 0.0;
    int bar = 0;
    int numerator = 4, denominator = 4;

    bool operator== (const TimeSignatureChange&) const = default;
};

//==============================================================================
/**
    The song's tempo and time-signature changes, as plain data the UI can
    hold on to (AudioEngine::getTempoMap makes one). The first of each is
    always at beat 0.

    Positions are in beats, the way Tracktion counts them: one beat is one
    count of the time signature (a quarter note in 4/4, an eighth in 6/8),
    so a bar is always `numerator` beats long, and the tempo counts those
    beats per minute.

    Bars are numbered from 0 here; the ruler shows bar 0 as "1".
*/
class TempoMap
{
public:
    /** 120 BPM in 4/4 all the way through. */
    TempoMap()  : TempoMap ({ TempoChange {} }, { TimeSignatureChange {} }) {}

    /** Both lists must be sorted by beat. A time signature that doesn't
        start on a bar line takes effect at the next one (as in Tracktion). */
    TempoMap (std::vector<TempoChange> tempoChanges, std::vector<TimeSignatureChange> signatureChanges)
        : tempos (std::move (tempoChanges)), signatures (std::move (signatureChanges))
    {
        if (tempos.empty())
            tempos.push_back ({});

        if (signatures.empty())
            signatures.push_back ({});

        tempos.front().beat = 0.0;
        signatures.front().beat = 0.0;
        signatures.front().bar = 0;

        // Number the bars: each signature starts on the first bar line of the
        // previous signature's grid at or after its own beat.
        for (size_t i = 1; i < signatures.size(); ++i)
        {
            const auto& prev = signatures[i - 1];
            auto& sig = signatures[i];
            sig.numerator = juce::jmax (1, sig.numerator);
            const int bars = juce::jmax (1, (int) std::ceil ((sig.beat - prev.beat) / prev.numerator - 1.0e-5));
            sig.bar = prev.bar + bars;
            sig.beat = prev.beat + bars * prev.numerator;
        }
    }

    const std::vector<TempoChange>& getTempoChanges() const noexcept               { return tempos; }
    const std::vector<TimeSignatureChange>& getTimeSignatureChanges() const noexcept { return signatures; }

    /** The tempo change in effect at a beat (the first one before beat 0). */
    const TempoChange& tempoAt (double beat) const
    {
        return tempos[(size_t) indexOfTempoAt (beat)];
    }

    int indexOfTempoAt (double beat) const
    {
        int i = 0;

        while (i + 1 < (int) tempos.size() && tempos[(size_t) i + 1].beat <= beat + 1.0e-9)
            ++i;

        return i;
    }

    /** The time signature in effect at a beat. */
    const TimeSignatureChange& signatureAt (double beat) const
    {
        return signatures[(size_t) indexOfSignatureAt (beat)];
    }

    int indexOfSignatureAt (double beat) const
    {
        int i = 0;

        while (i + 1 < (int) signatures.size() && signatures[(size_t) i + 1].beat <= beat + 1.0e-9)
            ++i;

        return i;
    }

    double beatsPerBarAt (double beat) const     { return signatureAt (beat).numerator; }

    /** The shortest bar in the song, in beats. The ruler uses it to decide
        how many bar numbers fit. */
    double shortestBar() const
    {
        int shortest = signatures.front().numerator;

        for (auto& s : signatures)
            shortest = juce::jmin (shortest, s.numerator);

        return shortest;
    }

    /** The beat a bar starts on. Bars before 0 continue the first time
        signature backwards (for the count-in). */
    double barToBeat (int bar) const
    {
        const auto& sig = signatureOfBar (bar);
        return sig.beat + (bar - sig.bar) * (double) sig.numerator;
    }

    /** The bar a beat is in. */
    int barAt (double beat) const
    {
        const auto& sig = signatureAt (beat);
        return sig.bar + (int) std::floor ((beat - sig.beat) / sig.numerator + 1.0e-9);
    }

    /** The start of the bar a beat is in. */
    double barStartAt (double beat) const     { return barToBeat (barAt (beat)); }

    /** The bar line nearest to a beat. */
    double nearestBarLine (double beat) const
    {
        const int bar = barAt (beat);
        const double start = barToBeat (bar), end = barToBeat (bar + 1);
        return beat - start < end - beat ? start : end;
    }

    /** The bar nearest to a beat (the one whose bar line is closest). */
    int nearestBar (double beat) const        { return barAt (nearestBarLine (beat) + 1.0e-6); }

    /** Calls fn (bar, beat) for every bar line from the bar containing
        `fromBeat` up to `toBeat`, skipping to every stride-th bar (bars
        whose number is a multiple of stride). */
    template <typename Fn>
    void forEachBar (double fromBeat, double toBeat, int stride, Fn&& fn) const
    {
        stride = juce::jmax (1, stride);
        int bar = juce::jmax (0, barAt (fromBeat));
        bar -= bar % stride;

        for (double beat = barToBeat (bar); beat <= toBeat; beat = barToBeat (bar))
        {
            fn (bar, beat);
            bar += stride;
        }
    }

    /** The time signatures the app offers (numerator, denominator). */
    static const std::vector<std::pair<int, int>>& commonSignatures()
    {
        static const std::vector<std::pair<int, int>> sigs { { 2, 4 }, { 3, 4 }, { 4, 4 }, { 5, 4 }, { 6, 8 }, { 7, 8 }, { 12, 8 } };
        return sigs;
    }

    bool operator== (const TempoMap&) const = default;

private:
    std::vector<TempoChange> tempos;
    std::vector<TimeSignatureChange> signatures;

    const TimeSignatureChange& signatureOfBar (int bar) const
    {
        size_t i = 0;

        while (i + 1 < signatures.size() && signatures[i + 1].bar <= bar)
            ++i;

        return signatures[i];
    }
};

} // namespace ikaros
