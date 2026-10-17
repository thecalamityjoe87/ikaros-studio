#pragma once

// Which time-stretcher plays a stretched audio region, for the engine's .cpp
// files. Every place that turns stretching on goes through here, so the
// choice is made in one place.
//
// Ikaros stretches with Rubber Band (built into Tracktion, see the
// ikaros_timestretch library in CMakeLists.txt). It has two modes here:
//  - melodic, for anything with a pitch (chords, bass, a transposed
//    recording): keeps notes steady;
//  - percussive, for drums and other unpitched loops: keeps hits sharp.
//
// Tracktion renders a stretched region into a "proxy" file in the
// background, and both playback and export read that same file, so a song
// exports exactly as it plays. (Tracktion asks Rubber Band for the same
// settings for those renders as for live playback, so there's no separate
// higher-quality mode for export to pick.)
//
// Sessions saved before Rubber Band used SoundTouch. SoundTouch is still
// built in so they open, and their regions are moved to Rubber Band when
// they're loaded (upgradeLegacyStretcher). The stretcher only changes how a
// region sounds, never where it is or how long it lasts: Tracktion fits the
// stretched audio to the region either way (the smoke test's "stretch"
// section checks the hits land in the same places).

#include <tracktion_engine/tracktion_engine.h>

namespace ikaros::detail
{

namespace te = tracktion;

/** True for Rubber Band's modes, the stretcher Ikaros uses. */
inline bool usesRubberBand (te::TimeStretcher::Mode mode)
{
    return mode == te::TimeStretcher::rubberbandMelodic || mode == te::TimeStretcher::rubberbandPercussive;
}

/** Turns on stretching for a region (a loop following the tempo or key).
    percussive: the region holds drums or other unpitched sounds. */
inline void useStretcher (te::AudioClipBase& clip, bool percussive)
{
    // setTimeStretchMode goes through the clip's undo manager, so this is
    // part of whatever undo step is open (importing the loop, say).
    clip.setTimeStretchMode (percussive ? te::TimeStretcher::rubberbandPercussive
                                        : te::TimeStretcher::rubberbandMelodic);
}

/** For a region whose pitch is about to change: pitch shifting needs a
    stretcher. One it already has (a drum loop's percussive mode) is kept. */
inline void ensureStretcher (te::AudioClipBase& clip)
{
    if (! usesRubberBand (clip.getTimeStretchMode()))
        useStretcher (clip, false);
}

/** After loading a session: a region saved with SoundTouch (sessions from
    before Rubber Band) switches to Rubber Band, in the mode a loop placed
    now would get (see prepareRegions in AudioEngine.cpp). */
inline void upgradeLegacyStretcher (te::AudioClipBase& clip, bool percussive)
{
    const auto mode = clip.getTimeStretchMode();

    if (mode == te::TimeStretcher::soundtouchNormal || mode == te::TimeStretcher::soundtouchBetter)
        useStretcher (clip, percussive);
}

} // namespace ikaros::detail
