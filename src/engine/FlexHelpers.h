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
//
// Recordings made in Ikaros follow the tempo the same way, on any track
// (as in GarageBand): Flex only decides whether their hits can be moved.
// Each one has a "beat map", where its recording sits in beats with no hits
// moved, noted the first time the tempo changes after it was recorded
// (until then it plays its file as it is). With Flex off, a recording plays
// through its beat map, and any hits moved while Flex was on are set aside
// (in beats, so they stay right through tempo changes) until it's back on.
// Imported files don't follow the tempo unless they're on a Flex track:
// Ikaros can't know what tempo they were made at.

#include "engine/FlexMap.h"
#include <tracktion_engine/tracktion_engine.h>
#include <optional>

namespace ikaros::detail
{

namespace te = tracktion;

/** On an audio track's state: Flex is on (see FlexEditor). */
inline const juce::Identifier flexProp ("ikarosFlex");

/** On a TAKE of an audio clip: the warp markers of that take (in seconds),
    kept while another take played, in sessions from before takes kept
    their Flex state in beats (takeFlexType). Still read. */
inline const juce::Identifier takeWarpMarkersType ("IKAROSTAKEWARP");

/** On a TAKE of an audio clip: that take's Flex state while another take
    plays (see TakeEditor): POINTs for its markers, in beats, and its own
    beatMapType and setAsideFlexType trees, if it had them. */
inline const juce::Identifier takeFlexType ("IKAROSTAKEFLEX");

/** On an audio clip that follows the tempo: its beat map, where its
    recording sits in beats with no hits moved. POINTs (see pointType). */
inline const juce::Identifier beatMapType ("IKAROSBEATMAP");

/** On an audio clip that follows the tempo on a track with Flex off: the
    Flex edits made while it was on, set aside. POINTs (see pointType). */
inline const juce::Identifier setAsideFlexType ("IKAROSFLEXASIDE");

/** A marker in beats, in the trees above: sourceTime (seconds into the
    file), beat (counted from where the region's content starts) and,
    for a tempo marker, tempoMarkerProp. */
inline const juce::Identifier pointType ("POINT");

/** On a WARPMARKER: it's a tempo marker (see WarpPoint::tempoMarker). */
inline const juce::Identifier tempoMarkerProp ("ikarosTempoMarker");

inline bool isFlexTrack (const te::Track& track)    { return (bool) track.state.getProperty (flexProp, false); }

/** Why a clip can't be flexed, or an empty string if it can. Loops that
    follow the tempo or key are already stretched by Tracktion, which can't
    warp them as well; a reversed region plays a reversed copy of its file,
    which Tracktion makes instead of the warped one. */
juce::String whyClipCantFlex (const te::Clip&);

inline bool canFlex (const te::Clip& clip)          { return whyClipCantFlex (clip).isEmpty(); }

/** True for a file Ikaros recorded: Tracktion names takes "..._Take_<n>"
    (and the passes of a cycle recording "..._Take_<n>_take_<n>"). */
bool isRecordingFile (const juce::File&);

/** True if the clip follows the tempo through its warp markers: a
    recording made in Ikaros, or any region on a Flex track, that can be
    flexed. */
bool followsTempo (const te::Clip&);

/** True if hits have been moved in the clip (it has markers of its own,
    besides the ends of the recording and tempo markers). */
bool hasHitEdits (const te::Clip&);

/** True if the clip plays through its warp markers. */
inline bool usesFlex (const te::Clip& clip)
{
    auto* audio = dynamic_cast<const te::AudioClipBase*> (&clip);
    return audio != nullptr && audio->getWarpTime();
}

/** Makes the clip follow its track: playing its Flex edits if the track
    has Flex on and the clip can be flexed; a recording on a track with
    Flex off sets its edits aside and plays through its beat map; anything
    else plays its recording as it is. Call after a region lands on a
    track (recorded, imported, pasted, moved to another track) or changes
    in a way that decides whether it can be flexed (reversed). Part of the
    open undo step; does nothing if the clip already follows its track. */
void applyTrackFlex (te::Clip&);

/** Choosing another take: the markers belong to the take they were made
    on (they pin moments of its recording), so the take that was playing
    keeps its Flex state on its TAKE (setAsideTakeFlex, called before the
    clip's source and offset change), and the new one gets back whatever it
    had (bringBackTakeFlex, after): its recording as it is, or following the
    tempo, if it never had any. In the same undo step. */
void setAsideTakeFlex (te::WaveAudioClip&, juce::ValueTree fromTake);
void bringBackTakeFlex (te::WaveAudioClip&, juce::ValueTree toTake);

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

/** The clip's layout, if it follows the tempo (std::nullopt if not). */
std::optional<FlexLayout> captureFlexLayout (const te::Clip&);

/** Puts a clip that follows the tempo back on the beats in `layout`, moved by `shiftBeats`:
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

    // Back on its beats where it is now, then following its (new) track:
    // that may set its Flex edits aside, in beats, which needs them right.
    if (layout.has_value() && followsTempo (clip))
        restoreFlexLayout (clip, *layout, clip.getStartBeat().inBeats() - before);

    applyTrackFlex (clip);
}

/** Every layout in the Edit that follows the tempo, for a tempo change:
    capture() before it, restore() after it (after Tracktion's own
    remapping). capture() also notes the beat map of each recording (and
    each of its takes) that hasn't got one yet, in the open undo step. */
class FlexBeatKeeper
{
public:
    void capture (te::Edit&);
    void restore();

private:
    std::vector<std::pair<te::Clip::Ptr, FlexLayout>> clips;
};

} // namespace ikaros::detail
