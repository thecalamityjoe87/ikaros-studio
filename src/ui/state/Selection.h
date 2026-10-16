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

    /** The tracks (in order) and regions there are, to compare with after an
        undo or redo (see selectAfterUndoOrRedo). */
    struct Contents
    {
        std::vector<TrackId> tracks;
        std::vector<RegionId> regions;
    };

    Contents getContents() const
    {
        Contents c;
        auto& tracks = engine.getTracks();

        for (auto& t : tracks.getTracks())
        {
            c.tracks.push_back (t.id);

            for (auto& r : tracks.regions().getRegions (t.id))
                c.regions.push_back (r.id);
        }

        return c;
    }

    /** Call after an undo or redo, with the contents from before it. Selects
        what it brought back (regions, or else a track), so you can see what
        changed. If it took the selected track away, the one now in its place
        is selected, as when deleting a track. Anything else that's gone is
        dropped from the selection. */
    void selectAfterUndoOrRedo (const Contents& before)
    {
        auto& tracks = engine.getTracks();
        auto contains = [] (const auto& list, auto id) { return std::find (list.begin(), list.end(), id) != list.end(); };

        std::vector<RegionId> regionsBack;
        TrackId regionsTrack = 0, trackBack = 0;

        for (auto& t : tracks.getTracks())
        {
            if (! contains (before.tracks, t.id))
                trackBack = t.id;

            for (auto& r : tracks.regions().getRegions (t.id))
            {
                if (! contains (before.regions, r.id))
                {
                    regionsBack.push_back (r.id);
                    regionsTrack = t.id;
                }
            }
        }

        if (! regionsBack.empty())
        {
            selectRegions (regionsTrack, std::move (regionsBack));
        }
        else if (trackBack != 0)
        {
            selectTrack (trackBack);
        }
        else if (! tracks.getTrack (getTrack()).has_value())
        {
            // The first track after it that's still there, or else the
            // nearest one before it.
            const auto at = std::find (before.tracks.begin(), before.tracks.end(), getTrack());
            TrackId neighbour = 0;

            for (auto it = at; it != before.tracks.end() && neighbour == 0; ++it)
                if (tracks.getTrack (*it).has_value())
                    neighbour = *it;

            for (auto it = at; it != before.tracks.begin() && neighbour == 0;)
                if (tracks.getTrack (*--it).has_value())
                    neighbour = *it;

            if (neighbour != 0)
                selectTrack (neighbour);
        }

        validate();
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
        changed = std::erase_if (regions, [&tracks] (RegionId r) { return ! tracks.regions().getRegion (r).has_value(); }) > 0
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
