#pragma once

#include "engine/GrooveLibrary.h"
#include <vector>

namespace ikaros
{

/** One bar of the song, as the Drummer sees it. */
struct DrumBar
{
    int bar = 0;                // the song's bar number (0 = the first, as in TempoMap)
    double startBeat = 0.0;     // on the timeline
    int numerator = 4, denominator = 4;
    double bpm = 120.0;         // the tempo at the start of the bar

    bool isFourFour() const noexcept    { return numerator == 4 && denominator == 4; }
};

/** Where a Drummer region plays, and the song around it. */
struct DrumContext
{
    /** The song's bars, in order, from the anchor (the last section
        change at or before the region's first bar, or else bar 0) to the
        bar after the region's last. The Drummer plans its phrases from
        the anchor, so a region plays the same bars the same way wherever
        it starts: a split region's halves carry on as one. */
    std::vector<DrumBar> bars;

    /** Bars where the song's sections change (a section starts, or one
        ends with a gap after it), in order. A fill plays in the bar
        before each, and a crash on it. */
    std::vector<int> boundaries;

    /** The region, on the timeline: only notes in here are made. */
    double fromBeat = 0.0, toBeat = 0.0;
};

/** One drum hit the Drummer plays. */
struct DrumHit
{
    DrumPiece piece = DrumPiece::kick;
    double beat = 0.0;          // on the timeline
    int velocity = 100;         // 1..127

    bool operator== (const DrumHit&) const = default;
};

/** What the Drummer plays in a region, and a little about how. */
struct DrumPart
{
    std::vector<DrumHit> hits;      // in time order
    std::vector<int> fillBars;      // bars (the song's numbers) that end in a fill
    std::vector<int> crashBars;     // bars that start with a crash (after a fill, or a loud section's first)
    std::vector<int> silentBars;    // bars of the region the Drummer leaves out: not in 4/4
};

//==============================================================================
/**
    The Drummer's rules: how a region's settings (DrummerSettings) become
    drum hits, from the human-played grooves in a GrooveLibrary. No machine
    learning, and no Tracktion: just choosing and adjusting real drummers'
    bars. DrummerEditor turns the hits into a region's MIDI notes.

    How it plays:
      - Phrases: from each section change, the bars go in phrases of four
        or eight. Each phrase plays a run of bars one drummer played in a
        row, chosen for its complexity and loudness (closest to the
        settings) and its tempo (near the song's). The next phrase in the
        section carries on with that performance, repeats it, or now and
        then picks another.
      - Complexity: between the grooves on offer, ghost notes and hi-hat
        subdivisions are taken out or added, so the setting moves smoothly.
      - Loudness: velocities follow a curve (soft playing is gentler, loud
        harder). Above 0.7 the time-keeping moves from the closed hi-hat
        towards the open hat or the ride, and sections start with a crash.
      - Fills: one in the bar before every section change, half a bar to a
        bar long, with a crash after it; and, with Fills up, extra ones at
        the end of four-bar groups (about every four bars at 1).
      - Swing: off-beat 8ths or 16ths (the drummer's own subdivision) move
        later, up to a triplet feel, on top of the drummer's own timing.
      - Piece toggles leave those pieces out, and nothing else changes.
      - Only 4/4 bars play; other bars are left silent.

    Every random choice comes from the seed and the bar it's for, so the
    same settings, seed and song always give exactly the same hits.
    Generating a hundred bars takes a millisecond or so; any thread can do it.
*/
namespace drummer
{
    DrumPart generate (const GrooveLibrary&, const DrummerSettings&, const DrumContext&);

    /** A velocity at a loudness setting (0..1): the curve described above. */
    int shapeVelocity (int velocity, float loudness);

    /** Loudness above this brings in the open hat or ride, and crashes. */
    inline constexpr float loudTimekeeping = 0.7f;
}

} // namespace ikaros
