#pragma once

#include "engine/AudioEngine.h"
#include "engine/TrackController.h"
#include <algorithm>

namespace ikaros
{

//==============================================================================
/**
    What the user has selected: one track, and any number of regions.

    The last region clicked is the "current" one, shown in the Editor.

    Components that care (track headers, regions, the bottom panel) listen for
    change messages. Track selection is forwarded to the engine because it
    decides which instrument the keyboards play.
*/
class Selection  : public juce::ChangeBroadcaster
{
public:
    explicit Selection (AudioEngine& e) : engine (e) {}

    TrackId getTrack() const                        { return engine.getTracks().getSelectedTrack(); }

    /** The region shown in the Editor (the one clicked last), or 0. */
    RegionId getRegion() const                      { return current; }

    const std::vector<RegionId>& getRegions() const { return regions; }
    bool hasRegions() const noexcept                { return ! regions.empty(); }

    bool isSelected (RegionId r) const
    {
        return std::find (regions.begin(), regions.end(), r) != regions.end();
    }

    /** Selects a track and deselects every region. */
    void selectTrack (TrackId id)
    {
        if (id != getTrack() || ! regions.empty())
        {
            engine.getTracks().setSelectedTrack (id);
            regions.clear();
            current = 0;
            sendChangeMessage();
        }
    }

    /** Selects just this region (0 = none) and its track. */
    void selectRegion (TrackId track, RegionId r)
    {
        selectRegions (track, r != 0 ? std::vector<RegionId> { r } : std::vector<RegionId> {});
    }

    /** Selects these regions. The last one becomes the current region. */
    void selectRegions (TrackId track, std::vector<RegionId> newRegions)
    {
        if (track != getTrack() || newRegions != regions)
        {
            engine.getTracks().setSelectedTrack (track);
            regions = std::move (newRegions);
            current = regions.empty() ? 0 : regions.back();
            sendChangeMessage();
        }
    }

    /** Shift-click: adds a region to the selection, or takes it out again. */
    void toggleRegion (TrackId track, RegionId r)
    {
        auto next = regions;

        if (isSelected (r))
            next.erase (std::find (next.begin(), next.end(), r));
        else
            next.push_back (r);

        selectRegions (track, next);
    }

    /** Drops selections that no longer exist (after undo, delete, or loading a session). */
    void validate()
    {
        bool changed = false;
        auto& tracks = engine.getTracks();

        if (! tracks.getTrack (getTrack()).has_value())
        {
            auto all = tracks.getTracks();
            tracks.setSelectedTrack (all.empty() ? 0 : all.front().id);
            changed = true;
        }

        // std::erase_if (C++20) removes every element the lambda returns true for.
        changed = std::erase_if (regions, [&tracks] (RegionId r) { return ! tracks.getRegion (r).has_value(); }) > 0
                    || changed;

        if (current != 0 && ! isSelected (current))
        {
            current = regions.empty() ? 0 : regions.back();
            changed = true;
        }

        if (changed)
            sendChangeMessage();
    }

private:
    AudioEngine& engine;
    std::vector<RegionId> regions;
    RegionId current = 0;
};

} // namespace ikaros
