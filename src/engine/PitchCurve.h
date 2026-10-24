#pragma once

// Pitch corrections, and the pitch curve they make: Tracktion-free maths
// shared by the engine (PitchEditor, PitchRender) and the audio editor's
// Pitch mode (which draws notes where they'll end up while dragging).

#include "engine/PitchAnalysis.h"
#include <cmath>

namespace ikaros
{

//==============================================================================
/** What the user changed about one note. All zero = untouched. */
struct NoteCorrection
{
    float shift = 0.0f;     // semitones, up or down (whole ones from a drag; fractions
                            // from an Alt-drag or centring)
    float snap = 0.0f;      // Snap to Key, 0..1: how far it then moves to the nearest key note
    float flatten = 0.0f;   // Vibrato, 0 (keep) .. 1 (flatten): how much of its wavering is taken out

    bool isUntouched() const noexcept   { return shift == 0.0f && snap == 0.0f && flatten == 0.0f; }
    bool operator== (const NoteCorrection&) const = default;
};

/** How quickly the pitch glides between corrected notes (the Region
    inspector's Note transitions). */
enum class NoteTransitions { natural = 0, tight = 1, instant = 2 };

namespace pitchcurve
{
    /** The glide's length, in seconds: about 120, 40 and 0 ms. */
    inline double transitionSeconds (NoteTransitions t)
    {
        return t == NoteTransitions::natural ? 0.12 : t == NoteTransitions::tight ? 0.04 : 0.0;
    }

    /** The key note (a whole MIDI note whose pitch class is in keyNotes,
        bit 0 = C) nearest a pitch. */
    inline int nearestKeyNote (float note, int keyNotes)
    {
        if ((keyNotes & 0xfff) == 0)
            return (int) std::lround (note);

        const int around = (int) std::lround (note);
        int best = around;
        float bestDistance = 1.0e9f;

        for (int n = around - 6; n <= around + 6; ++n)
        {
            if ((keyNotes & (1 << (((n % 12) + 12) % 12))) == 0)
                continue;

            if (const float d = std::abs ((float) n - note); d < bestDistance)
            {
                bestDistance = d;
                best = n;
            }
        }

        return best;
    }

    /** Where a note's centre ends up: moved by its shift, then Snap to Key
        of the way to the nearest key note. `transpose` is the region's own
        transposition (whole semitones), so the key note is the one heard. */
    inline float correctedCentre (const PitchNote& note, const NoteCorrection& c, int keyNotes, int transpose)
    {
        const float moved = note.centre + c.shift;

        if (c.snap <= 0.0f)
            return moved;

        const auto key = (float) (nearestKeyNote (moved + (float) transpose, keyNotes) - transpose);
        return moved + c.snap * (key - moved);
    }

    /** The shift that puts a note's centre on the nearest semitone, or on
        the nearest key note when `toKey`. */
    inline float centringShift (const PitchNote& note, bool toKey, int keyNotes, int transpose)
    {
        const auto target = toKey ? (float) (nearestKeyNote (note.centre + (float) transpose, keyNotes) - transpose)
                                  : (float) std::lround (note.centre);
        return target - note.centre;
    }

    /** The correction curve: for every analysis frame, how many semitones
        the recording is moved there (0 outside notes, so unpitched sounds
        are never touched). `corrections` has one entry per analysis note.

        Inside a note it's the move of its centre, minus Vibrato's share of
        the wavering around it. Between two notes that touch, it glides from
        one note's move to the next over the transition time; at a note's
        start or end next to silence or a consonant, it eases in and out over
        half that (at most a quarter of the note), so the edges don't click. */
    inline std::vector<float> offsets (const PitchAnalysis& a, const std::vector<NoteCorrection>& corrections,
                                       int keyNotes, int transpose, NoteTransitions transitions)
    {
        std::vector<float> result (a.pitch.size(), 0.0f);
        const auto& notes = a.notes;
        const auto frame = a.frameSeconds();
        const int glide = (int) std::lround (transitionSeconds (transitions) / frame);
        std::vector<float> moves (notes.size(), 0.0f);

        for (size_t i = 0; i < notes.size() && i < corrections.size(); ++i)
            moves[i] = correctedCentre (notes[i], corrections[i], keyNotes, transpose) - notes[i].centre;

        for (size_t i = 0; i < notes.size() && i < corrections.size(); ++i)
        {
            const auto& n = notes[i];
            const auto& c = corrections[i];
            const bool joinedBefore = i > 0 && notes[i - 1].firstFrame + notes[i - 1].numFrames == n.firstFrame;
            const bool joinedAfter = i + 1 < notes.size() && n.firstFrame + n.numFrames == notes[i + 1].firstFrame;
            const int ease = std::max (1, std::min (glide / 2, n.numFrames / 4));

            for (int k = 0; k < n.numFrames; ++k)
            {
                const int f = n.firstFrame + k;
                float move = moves[i];

                // Gliding from the note before (the second half of the glide;
                // the note before does the first half).
                if (joinedBefore && glide > 0 && k < glide / 2)
                {
                    const float w = 0.5f + 0.5f * (float) k / (float) std::max (1, glide / 2);
                    move = moves[i - 1] + w * (moves[i] - moves[i - 1]);
                }
                else if (joinedAfter && glide > 0 && n.numFrames - k <= glide / 2)
                {
                    const float w = 0.5f * (float) (glide / 2 - (n.numFrames - k)) / (float) std::max (1, glide / 2);
                    move = moves[i] + w * (moves[i + 1] - moves[i]);
                }
                else if (! joinedBefore && k < ease)
                {
                    move *= (float) (k + 1) / (float) (ease + 1);
                }
                else if (! joinedAfter && n.numFrames - k <= ease)
                {
                    move *= (float) (n.numFrames - k) / (float) (ease + 1);
                }

                // Vibrato: the wavering around the centre, scaled down.
                const float sung = a.pitch[(size_t) f];
                const float waver = sung > 0.0f ? sung - n.centre : 0.0f;
                result[(size_t) f] = move - c.flatten * waver;
            }
        }

        return result;
    }

    /** The curve at a time (seconds into the recording), between frames. */
    inline float offsetAt (const PitchAnalysis& a, const std::vector<float>& curve, double seconds)
    {
        if (curve.empty())
            return 0.0f;

        const double pos = (seconds * a.sampleRate - a.frameSize * 0.5) / a.hop;
        const int i = (int) std::floor (pos);

        if (i < 0 || i >= (int) curve.size())
            return 0.0f;

        const float x = (float) (pos - i);
        const float next = i + 1 < (int) curve.size() ? curve[(size_t) i + 1] : 0.0f;

        // Never blend into a frame outside a note: 0 there means "untouched".
        if (curve[(size_t) i] == 0.0f || next == 0.0f)
            return x < 0.5f ? curve[(size_t) i] : next;

        return curve[(size_t) i] + x * (next - curve[(size_t) i]);
    }
}

} // namespace ikaros
