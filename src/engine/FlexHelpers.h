#pragma once

// Flex Time on Tracktion clips, for the engine's .cpp files (see FlexEditor
// for what Flex is). Implemented in FlexEditor.cpp.
//
// Tracktion keeps a flexed clip's warp markers in seconds: the WARPTIME
// tree inside the clip's state, saved with the session. Seconds don't
// follow the music, so everything that changes where a flexed region's
// audio falls in beats - a tempo or time-signature change, moving the
// region somewhere the tempo differs - goes like this:
//
//     auto layout = captureFlexLayout (clip);       // where everything is, in beats
//     ...change the tempo, or move the clip...
//     restoreFlexLayout (clip, *layout, shift);     // back on those beats (moved by `shift`)
//
// Splitting, trimming, copying and looping don't need it: the clip's state
// (markers included) is copied or kept as it is, and its content stays put.
//
// Between two markers, Tracktion stretches the audio evenly in seconds. If
// the tempo changes in between, beats aren't evenly spread in seconds
// there, so putting just the markers back on their beats would leave the
// hits between them off theirs. So the layout keeps every moment of the
// recording on its beat: captureFlexLayout notes where the audio is at
// each tempo change inside the region, and restoreFlexLayout adds a
// "tempo marker" wherever the tempo changes inside it afterwards (and
// drops the ones no longer needed). Between any two markers the tempo is
// then steady, and seconds and beats are in step again.

#include "engine/FlexMap.h"
#include <tracktion_engine/tracktion_engine.h>
#include <optional>

namespace ikaros::detail
{

namespace te = tracktion;

/** On an audio track's state: Flex is on (see FlexEditor). */
inline const juce::Identifier flexProp ("ikarosFlex");

/** On a TAKE of an audio clip: the warp markers of that take, kept while
    another take plays (see TakeEditor). */
inline const juce::Identifier takeWarpMarkersType ("IKAROSTAKEWARP");

/** On a WARPMARKER: it's a tempo marker (see WarpPoint::tempoMarker). */
inline const juce::Identifier tempoMarkerProp ("ikarosTempoMarker");

inline bool isFlexTrack (const te::Track& track)    { return (bool) track.state.getProperty (flexProp, false); }

/** Why a clip can't be flexed, or an empty string if it can. Loops that
    follow the tempo or key are already stretched by Tracktion, which can't
    warp them as well; a reversed region plays a reversed copy of its file,
    which Tracktion makes instead of the warped one. */
juce::String whyClipCantFlex (const te::Clip&);

inline bool canFlex (const te::Clip& clip)          { return whyClipCantFlex (clip).isEmpty(); }

/** True if the clip plays through its warp markers. */
inline bool usesFlex (const te::Clip& clip)
{
    auto* audio = dynamic_cast<const te::AudioClipBase*> (&clip);
    return audio != nullptr && audio->getWarpTime();
}

/** Makes the clip follow its track: warped if the track has Flex on and
    the clip can be flexed, plain otherwise. Call after a region lands on a
    track (recorded, imported, pasted, moved to another track) or changes
    in a way that decides whether it can be flexed (reversed). Part of the
    open undo step; does nothing if the clip already follows its track. */
void applyTrackFlex (te::Clip&);

/** Choosing another take: the markers belong to the take they were made
    on (they pin moments of its recording), so the take that was playing
    keeps its own on its TAKE, and the new one gets back whatever it had
    (plain markers if none). Call after the clip's source has changed to
    the new take's file, in the same undo step. */
void switchTakeWarp (te::WaveAudioClip&, juce::ValueTree fromTake, juce::ValueTree toTake);

/** The clip's markers, first to last (empty if it has never been flexed). */
std::vector<WarpPoint> getWarpPoints (const te::AudioClipBase&);

/** Replaces the clip's markers, as part of the open undo step. Only the
    values that differ are written, so writing the same markers changes
    nothing. The first and last must be the ends of the recording. */
void setWarpPoints (te::AudioClipBase&, const std::vector<WarpPoint>&);

/** Where the clip's content (its warp time 0) would start on the
    timeline, in seconds: before the region's start by its offset (and, if
    it's looped, by where in its content the loop begins). */
te::TimePosition contentStartOf (const te::Clip&);

/** How long the clip's content lasts, in seconds: as warped, or the
    recording's own length. 0 if unknown. */
te::TimeDuration contentLengthOf (const te::AudioClipBase&);

//==============================================================================
/** Where a flexed clip and its markers are, in beats. */
struct FlexLayout
{
    double startBeat = 0.0, endBeat = 0.0;   // the region
    double contentStartBeat = 0.0;           // contentStartOf, in beats

    // Each marker (sourceSeconds), and where it plays in beats from the
    // content start. Also a point wherever the tempo changes inside the
    // region (as tempo markers, though the clip may not have them yet), so
    // the audio's beats are in step with its markers' all the way through.
    std::vector<FlexMarker> markers;
    bool looped = false;
    double loopStartBeats = 0.0, loopEndBeats = 0.0;   // from the content start
};

/** The clip's layout, if it's flexed (std::nullopt if not). */
std::optional<FlexLayout> captureFlexLayout (const te::Clip&);

/** Puts a flexed clip back on the beats in `layout`, moved by `shiftBeats`:
    its start, end and offset, its loop, and every marker, with tempo
    markers where the tempo changes inside it now. Part of the open undo
    step; writes nothing that's already right. */
void restoreFlexLayout (te::Clip&, const FlexLayout&, double shiftBeats = 0.0);

/** For edits that move a clip (fn does the moving): keeps it flexed on its
    beats, wherever it ends up, and following its (new) track's Flex. */
template <typename Fn>
void moveKeepingFlex (te::Clip& clip, Fn&& fn)
{
    const auto layout = captureFlexLayout (clip);
    const double before = clip.getStartBeat().inBeats();
    fn();
    applyTrackFlex (clip);

    if (layout.has_value() && usesFlex (clip))
        restoreFlexLayout (clip, *layout, clip.getStartBeat().inBeats() - before);
}

/** Every flexed clip's layout in the Edit, for a tempo change: capture()
    before it, restore() after it (after Tracktion's own remapping). */
class FlexBeatKeeper
{
public:
    void capture (te::Edit&);
    void restore();

private:
    std::vector<std::pair<te::Clip::Ptr, FlexLayout>> clips;
};

} // namespace ikaros::detail
