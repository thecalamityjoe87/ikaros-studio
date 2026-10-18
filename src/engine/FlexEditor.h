#pragma once

#include "engine/EngineTypes.h"
#include "engine/FlexMap.h"
#include "engine/Quantize.h"
#include <juce_events/juce_events.h>
#include <map>
#include <memory>
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
    bool quantizePending = false;    // Quantize is waiting for the hits to be found (see FlexEditor::quantize)

    // Roughly how many beats a second of the region lasts (at its start),
    // so the editor can keep a drag inside the limits the engine uses.
    double beatsPerSecond = 2.0;
};

/** What FlexEditor::quantize did. */
enum class FlexQuantizeResult
{
    quantized,        // the hits moved, as one "Quantize" undo step
    alreadyOnGrid,    // nothing needed to move (no undo step)
    findingHits,      // the hits are being found: Quantize happens when they are
    unavailable       // the region can't be flexed (FlexInfo::unavailableReason says why)
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

    Quantize (see quantize()) lines the hits up with the grid by moving
    their markers, the way the piano roll's Quantize moves notes.

    Every method runs on the message thread, and every change is one undo step.
*/
class FlexEditor  : private juce::Timer
{
public:
    explicit FlexEditor (AudioEngine&);
    ~FlexEditor() override;   // in the .cpp, where PendingQuantize is a complete type

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

    /** Quantize: moves the region's hits towards the grid (on the song's
        timeline, following its tempo and time signatures), each as far as
        the settings' strength says, as one "Quantize" undo step. Each hit
        gets a marker, so the audio between them stretches to fit.

        `selection` (in beats on the timeline) picks the hits to move: those
        played inside it. Markers go on the hits either side as well, so the
        rest of the region stays where it is. Without one, every hit heard
        in the region moves.

        On a track with Flex off, Quantize turns it on, in the same undo
        step. If the hits haven't been found yet (Flex was off, or it's
        still looking), they're found first, in the background, and the
        region is quantized when they are (findingHits; getFlex says
        quantizePending meanwhile). Asking again replaces the waiting request. */
    FlexQuantizeResult quantize (RegionId, const QuantizeSettings&, std::optional<juce::Range<double>> selection = {});

    /** @internal Called (via TrackController) before the Edit is replaced. */
    void sessionChanged();

private:
    struct PendingQuantize;
    struct HitSearch;

    AudioEngine& owner;

    // Quantize requests waiting for their region's hits, checked on the
    // timer. unique_ptr: PendingQuantize holds Tracktion objects, which this
    // header (seen by the UI) doesn't know about.
    std::map<RegionId, std::unique_ptr<PendingQuantize>> pendingQuantizes;

    // Hits found for Quantize while Flex was off (by recording file), so
    // the region doesn't wait for them again when Flex comes on.
    mutable std::map<RegionId, std::pair<juce::String, std::vector<double>>> hitsFoundForQuantize;

    HitSearch findHits (RegionId) const;
    FlexQuantizeResult applyQuantize (RegionId, const std::vector<double>& hits, const QuantizeSettings&,
                                      std::optional<juce::Range<double>> selectionFromStart);
    void timerCallback() override;

    // Which file each region's hits were found in. Choosing another take
    // changes a region's file: then the hits are found again (see getFlex).
    mutable std::map<RegionId, juce::String> analysedFiles;
    mutable std::map<RegionId, std::vector<double>> staleTransients;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FlexEditor)
};

} // namespace ikaros
