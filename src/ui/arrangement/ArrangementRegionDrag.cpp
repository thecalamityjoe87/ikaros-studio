#include "ui/arrangement/ArrangementRegionDrag.h"
#include "ui/arrangement/ArrangementLayout.h"
#include "ui/arrangement/ArrangementOverlays.h"

namespace ikaros
{

using namespace arrangement;

namespace
{
    constexpr int dragThreshold = 4;       // pixels the mouse must move before a click becomes a drag
}

void ArrangementView::RegionDrag::regionMouseDown (RegionComponent& comp, const juce::MouseEvent& e)
{
    const auto info = comp.getInfo();   // a copy: the component may be updated during the drag
    drag = {};

    if (e.mods.isPopupMenu())
    {
        // Right-click: act on the clicked region, keeping a selection it's part of.
        if (! owner.selection.isSelected (info.id))
            owner.selection.selectRegion (info.trackId, info.id);

        owner.showRegionMenu();
        return;
    }

    if (e.mods.isShiftDown() || e.mods.isCommandDown())
    {
        owner.selection.toggleRegion (info.trackId, info.id);

        if (! owner.selection.isSelected (info.id))
            return;   // it was just deselected: nothing to drag
    }
    else if (! owner.selection.isSelected (info.id))
    {
        owner.selection.selectRegion (info.trackId, info.id);
    }
    else
    {
        // Already part of a selection: keep it so the whole group can be
        // dragged; a plain click (no drag) selects just this one on mouse-up.
        drag.selectOnlyAnchorOnClick = owner.selection.getRegions().size() > 1;
    }

    const auto zone = comp.getZoneAt (e.getPosition());
    drag.anchor = info.id;
    drag.start = e.getEventRelativeTo (&owner).getPosition();
    drag.mode = zone == RegionComponent::Zone::leftEdge  ? Drag::Mode::trimStart
              : zone == RegionComponent::Zone::rightEdge ? Drag::Mode::trimEnd
                                                         : Drag::Mode::pending;
    drag.trim = { info.startBeat, info.getEndBeat() };

    for (auto id : owner.selection.getRegions())
    {
        if (auto* c = owner.findRegion (id))
        {
            drag.regions.push_back (c->getInfo());
            drag.rows.push_back (owner.rowOf (c->getInfo().trackId));
        }
    }
}

void ArrangementView::RegionDrag::regionMouseDrag (RegionComponent&, const juce::MouseEvent& e)
{
    const auto pos = e.getEventRelativeTo (&owner).getPosition();

    if (drag.mode == Drag::Mode::pending && pos.getDistanceFrom (drag.start) >= dragThreshold)
    {
        drag.mode = Drag::Mode::move;

        // Dim the originals; outlines show where they'll land.
        for (auto& r : drag.regions)
            if (auto* c = owner.findRegion (r.id))
                c->setAlpha (0.35f);
    }

    if (drag.mode == Drag::Mode::move)
        updateMoveDrag (pos);
    else if (drag.mode == Drag::Mode::trimStart || drag.mode == Drag::Mode::trimEnd)
        updateTrimDrag (pos.x - drag.start.x);
}

void ArrangementView::RegionDrag::updateMoveDrag (juce::Point<int> pos)
{
    auto anchor = std::find_if (drag.regions.begin(), drag.regions.end(),
                                [this] (const RegionInfo& r) { return r.id == drag.anchor; });

    if (anchor == drag.regions.end())
        return;

    const int anchorIndex = (int) std::distance (drag.regions.begin(), anchor);

    // Time: snap the clicked region's start to the grid; the others keep their spacing.
    double delta = (pos.x - drag.start.x) / owner.geometry.pixelsPerBeat;
    delta = owner.snapBeat (anchor->startBeat + delta) - anchor->startBeat;

    double earliest = anchor->startBeat;

    for (auto& r : drag.regions)
        earliest = juce::jmin (earliest, r.startBeat);

    delta = juce::jmax (delta, -earliest);   // nothing goes before the start of the song

    // Tracks: follow the mouse up and down, as long as every region lands on
    // a track of its own kind (MIDI on instrument tracks, audio on audio
    // tracks). Otherwise they stay on their own tracks.
    int rowDelta = owner.rowAt (pos.y) - drag.rows[(size_t) anchorIndex];

    for (size_t i = 0; i < drag.regions.size(); ++i)
    {
        const int target = drag.rows[i] + rowDelta;

        if (drag.rows[i] < 0 || ! juce::isPositiveAndBelow (target, (int) owner.tracks.size())
             || (owner.tracks[(size_t) target].kind == TrackKind::audio) == drag.regions[i].isMidi)
        {
            rowDelta = 0;
            break;
        }
    }

    drag.beatDelta = delta;
    drag.rowDelta = rowDelta;

    std::vector<Overlay::Ghost> ghosts;

    for (size_t i = 0; i < drag.regions.size(); ++i)
    {
        auto& r = drag.regions[i];
        const int row = drag.rows[i] + rowDelta;

        if (! juce::isPositiveAndBelow (row, (int) owner.tracks.size()))
            continue;

        auto area = owner.rowAreaInOverlay (row);
        ghosts.push_back ({ { owner.geometry.beatToX (r.startBeat + delta), area.getY() + (float) regionTop,
                              juce::jmax (8.0f, owner.geometry.beatsToWidth (r.lengthBeats)), area.getHeight() - (float) regionGap },
                            owner.tracks[(size_t) row].colour, r.name });
    }

    owner.overlay->setGhosts (std::move (ghosts));
}

void ArrangementView::RegionDrag::updateTrimDrag (int dx)
{
    auto* comp = owner.findRegion (drag.anchor);

    if (comp == nullptr)
        return;

    auto& info = comp->getInfo();
    const double delta = dx / owner.geometry.pixelsPerBeat;
    const double minLength = TrackController::minRegionBeats;

    if (drag.mode == Drag::Mode::trimStart)
    {
        auto start = owner.snapBeat (info.startBeat + delta);
        start = juce::jlimit (juce::jmax (0.0, info.minStartBeat), info.getEndBeat() - minLength, start);
        drag.trim = { start, info.getEndBeat() };
    }
    else
    {
        auto end = owner.snapBeat (info.getEndBeat() + delta);
        end = juce::jlimit (info.startBeat + minLength, juce::jmax (info.startBeat + minLength, info.maxEndBeat), end);
        drag.trim = { info.startBeat, end };
    }

    comp->setTrimPreview (drag.trim);

    if (auto* lane = comp->getParentComponent())
        lane->resized();
}

void ArrangementView::RegionDrag::regionMouseUp (RegionComponent&, const juce::MouseEvent&)
{
    auto finished = std::exchange (drag, {});   // take the drag state and reset it
    auto& tc = owner.engine.getTracks();

    if (finished.mode == Drag::Mode::move)
    {
        owner.overlay->clear();

        for (auto& r : finished.regions)
            if (auto* c = owner.findRegion (r.id))
                c->setAlpha (1.0f);

        if (finished.beatDelta != 0.0 || finished.rowDelta != 0)
        {
            std::vector<RegionPlacement> placements;

            for (size_t i = 0; i < finished.regions.size(); ++i)
            {
                const int row = finished.rows[i] + finished.rowDelta;

                if (juce::isPositiveAndBelow (row, (int) owner.tracks.size()))
                    placements.push_back ({ finished.regions[i].id, owner.tracks[(size_t) row].id,
                                            finished.regions[i].startBeat + finished.beatDelta });
            }

            tc.moveRegions (placements);

            // The selected track follows the clicked region to its new track.
            if (auto moved = tc.getRegion (finished.anchor))
                owner.selection.selectRegions (moved->trackId, owner.selection.getRegions());

            owner.refresh();   // now, so the regions don't flash back to where they were
        }
    }
    else if (finished.mode == Drag::Mode::trimStart || finished.mode == Drag::Mode::trimEnd)
    {
        if (auto* comp = owner.findRegion (finished.anchor))
        {
            const auto& info = comp->getInfo();
            const bool changed = finished.trim.getStart() != info.startBeat || finished.trim.getEnd() != info.getEndBeat();
            comp->setTrimPreview (std::nullopt);

            if (changed)
                tc.trimRegion (finished.anchor, finished.trim.getStart(), finished.trim.getEnd());

            owner.refresh();
        }
    }
    else if (finished.mode == Drag::Mode::pending && finished.selectOnlyAnchorOnClick)
    {
        if (auto r = tc.getRegion (finished.anchor))
            owner.selection.selectRegion (r->trackId, r->id);
    }
}

} // namespace ikaros
