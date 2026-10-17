#pragma once

// Quantize's maths, with no Tracktion in it: where the grid lines are, and
// how far Quantize moves something towards the nearest one. Shared by the
// piano roll's Quantize (NoteEditor), Flex Time's (FlexEditor, for the hits
// in a recording) and the editors' snapping (ui/bottompanel/EditorGrid.h),
// so all three agree on where the grid is.

#include "engine/TempoMap.h"
#include <cmath>

namespace ikaros
{

/** How Quantize lines things up. */
struct QuantizeSettings
{
    // The grid, as a note length in whole notes: 1.0 / 16.0 is sixteenth
    // notes. 0 means bar lines. (A note length rather than a number of
    // beats, because what a beat is depends on the time signature: a
    // quarter note in 4/4, an eighth in 6/8. See TempoMap.)
    double note = 1.0 / 16.0;

    // How far each note or hit moves towards its grid line: 1 = all the way,
    // 0.5 = halfway (it tightens the timing but keeps some of the feel).
    double strength = 1.0;

    // How late every second grid line is: 0 = straight, 1 = a full triplet
    // shuffle (the second line of each pair moved a third of a step later).
    // Pairs are counted from each bar's start. Not used with bar lines.
    double swing = 0.0;
};

namespace quantize
{
    /** At full swing, how far the second grid line of each pair moves, in steps. */
    inline constexpr double maxSwingSteps = 1.0 / 3.0;

    /** How many beats one grid step of `note` lasts in the bar that holds
        `beat`. (One beat is one count of the time signature: 1/den of a
        whole note.) */
    inline double stepBeatsAt (double note, const TempoMap& tempo, double beat)
    {
        return note * tempo.signatureAt (beat).denominator;
    }

    /** The grid line nearest `beat` (or, with roundDown, the last one at or
        before it), on the song's timeline. Grid lines are counted from the
        start of each bar, so they follow time-signature changes: a grid that
        doesn't divide a bar evenly (quarter notes in 7/8) starts again at
        the next bar line. */
    inline double gridLine (double beat, const QuantizeSettings& settings, const TempoMap& tempo, bool roundDown = false)
    {
        // A tiny nudge, so a beat exactly on a bar line counts as that bar's.
        constexpr double nudge = 1.0e-9;

        if (settings.note <= 0.0)
            return roundDown ? tempo.barStartAt (beat + nudge) : tempo.nearestBarLine (beat);

        const int bar = tempo.barAt (beat + nudge);
        const double barStart = tempo.barToBeat (bar);
        const double barEnd = tempo.barToBeat (bar + 1);
        const double step = stepBeatsAt (settings.note, tempo, barStart);

        if (step <= 0.0)
            return beat;

        const double late = juce::jlimit (0.0, 1.0, settings.swing) * maxSwingSteps * step;

        // Grid line k of the bar: odd ones are late with swing. The bar's
        // end is the next bar's first line.
        auto lineAt = [&] (int k)
        {
            return juce::jmin (barEnd, barStart + k * step + (k % 2 != 0 ? late : 0.0));
        };

        // Swing only moves lines later, so the answer is the line at or
        // before `beat` without swing (k), or the one after it.
        const int k = juce::jmax (0, (int) std::floor ((beat - barStart) / step + nudge));

        if (roundDown)
            return lineAt (k) <= beat + nudge ? lineAt (k) : lineAt (juce::jmax (0, k - 1));

        double best = barEnd;

        for (int candidate : { k - 1, k, k + 1 })
            if (candidate >= 0 && std::abs (lineAt (candidate) - beat) < std::abs (best - beat))
                best = lineAt (candidate);

        return best;
    }

    /** The first grid line at or after `beat`. */
    inline double gridLineAtOrAfter (double beat, const QuantizeSettings& settings, const TempoMap& tempo)
    {
        constexpr double nudge = 1.0e-9;

        if (settings.note <= 0.0)
        {
            const double start = tempo.barStartAt (beat + nudge);
            return start >= beat - nudge ? start : tempo.barToBeat (tempo.barAt (beat + nudge) + 1);
        }

        // Look a little further on until the line at or before is far enough.
        // (Lines are at least two thirds of a step apart, even with swing.)
        double probe = beat;

        for (int i = 0; i < 4; ++i, probe += stepBeatsAt (settings.note, tempo, probe) / 2.0)
            if (const double line = gridLine (probe, settings, tempo, true); line >= beat - nudge)
                return line;

        return gridLine (probe, settings, tempo, true);
    }

    /** Where Quantize moves something at `beat`: towards its nearest grid
        line, as far as the strength says. */
    inline double quantizedBeat (double beat, const QuantizeSettings& settings, const TempoMap& tempo)
    {
        const double target = gridLine (beat, settings, tempo);
        return beat + juce::jlimit (0.0, 1.0, settings.strength) * (target - beat);
    }
}

} // namespace ikaros
