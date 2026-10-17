#pragma once

#include "engine/EngineTypes.h"
#include "engine/FlexMap.h"
#include <map>
#include <optional>

namespace ikaros
{

class AudioEngine;

/** What the audio editor needs to show and edit a region's Flex Time. */
struct FlexInfo
{
    bool trackFlex = false;          // the region's track has Flex on
    bool available = false;          // the region can be flexed (see unavailableReason)
    juce::String unavailableReason;  // why not, for a tooltip; empty if it can
    bool on = false;                 // both: the region plays through its markers

    // Every marker, first to last. The first and last are the ends of the
    // recording; they follow the tempo but aren't dragged. Beats are on the
    // timeline (a looped region's: its first time round).
    std::vector<FlexMarker> markers;

    // The hits found in the recording, sorted, where they play now.
    std::vector<FlexMarker> transients;

    bool analysing = false;          // still finding the hits (in the background)
    float analysisProgress = -1.0f;  // ...how far it's got, 0..1 (-1 = can't tell)
    float stretchProgress = -1.0f;   // the stretched audio is being made: 0..1 (-1 = it's ready)
    bool hasEdits = false;           // a marker has been moved: Reset Flex has something to undo

    // Roughly how many beats a second of the region lasts (at its start),
    // so the editor can keep a drag inside the limits the engine uses.
    double beatsPerSecond = 2.0;
};

//==============================================================================
/**
    Flex Time: moving the hits in an audio recording, stretching the audio
    in between, like GarageBand's Flex.

    Flex is switched on per audio track (off by default). Every region on a
    Flex track that can be flexed then plays through warp markers, using
    Tracktion's WarpTimeManager: it finds the hits (transients) in the
    recording on a background thread, and renders the stretched audio in
    the background too. Until that render is ready the region is silent
    (as when it's reversed), and playback and export both use the result,
    so a song exports exactly as it plays.

    A flexed region stays locked to the music: its markers are kept on
    their beats when the tempo or time signature changes, or the region is
    moved, so a hit dragged onto the beat stays there. (Tracktion keeps
    markers in seconds; the engine puts them back on their beats after each
    such change, see FlexHelpers.h.)

    Every method runs on the message thread, and every change is one undo step.
*/
class FlexEditor
{
public:
    explicit FlexEditor (AudioEngine&);

    /** True if the track has Flex on. */
    bool isFlexOn (TrackId) const;

    /** Switches Flex on or off for an audio track, as one "Flex On" or "Flex
        Off" undo step. A region's markers are kept while Flex is off, and
        play again when it comes back on. */
    void setFlexOn (TrackId, bool shouldBeOn);

    /** The region's Flex state. Polled by the audio editor while it's
        analysing or stretching (nothing in the session changes then). */
    FlexInfo getFlex (RegionId) const;

    /** Drags the hit (or marker) at `sourceSeconds` in the recording to
        `newBeat` on the timeline, as one "Move Flex Marker" undo step.
        Grabbing a hit that has no marker yet adds one, plus markers on the
        neighbouring hits, so only the audio between those stretches. The
        marker can't pass its neighbours. Returns false if nothing changed. */
    bool moveMarker (RegionId, double sourceSeconds, double newBeat);

    /** What moveMarker would do, without changing anything: the region's
        markers after the drag. For drawing the drag as it happens. */
    static std::vector<FlexMarker> previewMove (const FlexInfo&, double sourceSeconds, double newBeat);

    /** Removes the marker at `sourceSeconds` ("Remove Flex Marker"): the
        audio either side goes back to stretching evenly between its
        neighbours. The first and last markers can't be removed. */
    bool removeMarker (RegionId, double sourceSeconds);

    /** Takes every marker away ("Reset Flex"): the region plays the
        recording as it was recorded. Returns false if there were none. */
    bool resetFlex (RegionId);

    /** @internal Called (via TrackController) after the Edit has been replaced. */
    void sessionChanged();

private:
    AudioEngine& owner;

    // Which file each region's hits were found in. Choosing another take
    // changes a region's file: then the hits are found again (see getFlex).
    mutable std::map<RegionId, juce::String> analysedFiles;
    mutable std::map<RegionId, std::vector<double>> staleTransients;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FlexEditor)
};

} // namespace ikaros
