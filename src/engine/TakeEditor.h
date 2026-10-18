#pragma once

#include "engine/EngineTypes.h"
#include <vector>

namespace ikaros
{

class AudioEngine;

/** One take of a region, for the take menu. */
struct TakeInfo
{
    juce::String name;      // "Take 1", "Take 2"...
    bool current = false;   // the one the region plays
};

//==============================================================================
/**
    Takes: several versions of a region recorded in the same place, of which
    the region plays one (its current take).

    Takes come from recording:
      - Cycle recording (the loop on) records one take per pass, audio and
        MIDI alike. For MIDI the passes can instead be merged into one region
        (the default; see AudioEngine::setMidiCycleTakes).
      - Recording over an audio region keeps what was there as a take of the
        new region (the rest of the old region stays as it was). Recording
        the same cycle again adds the new passes to the region's takes.

    Audio takes use Tracktion's TAKES list on the wave clip (one TAKE per
    file), but not its take switching, which only works with Tracktion
    projects (Ikaros sessions refer to plain files). MIDI takes are
    Tracktion's own: one MidiList per take.

    Flex edits (see FlexEditor) belong to the take they were made on: each
    take keeps its own markers, and choosing a take brings back its own. A
    region that plays flexed can't be kept as a take when it's recorded
    over, just as a stretched or reversed one can't (its take would sound
    different).

    Every method runs on the message thread, and every change is one undo step.
*/
class TakeEditor
{
public:
    explicit TakeEditor (AudioEngine&);

    /** The region's takes, in recording order (empty for an ordinary region). */
    std::vector<TakeInfo> getTakes (RegionId) const;

    /** Makes the region play another of its takes ("Choose Take"). */
    bool setCurrentTake (RegionId, int takeIndex);

    /** Throws away every take but the current one. The region keeps its take
        list (with one take in it), so recording the same cycle again adds to it. */
    bool deleteUnusedTakes (RegionId);

    /** Turns the region into an ordinary region playing its current take. */
    bool flattenTakes (RegionId);

    /** Called by AudioEngine when a take has been recorded, inside its
        "Record" undo step, with the regions Tracktion made. Tidies up the
        takes of cycle recordings, and keeps what was recorded over as takes.
        midiCycleTakes: MIDI passes were recorded as takes (not merged). */
    void gatherRecordedTakes (const std::vector<RegionId>& recorded, bool midiCycleTakes);

private:
    AudioEngine& owner;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TakeEditor)
};

} // namespace ikaros
