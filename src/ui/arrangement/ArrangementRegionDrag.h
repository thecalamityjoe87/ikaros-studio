#pragma once

// Internal to ArrangementView: only its own .cpp files include this.

#include "ui/arrangement/ArrangementView.h"

namespace ikaros
{

//==============================================================================
// Region gestures (RegionComponent forwards its mouse events to the
// ArrangementView, which hands them on to this): click to select, drag to
// move, drag an edge to trim, drag the top-right corner to loop.
class ArrangementView::RegionDrag
{
public:
    explicit RegionDrag (ArrangementView& o) : owner (o) {}

    void regionMouseDown (RegionComponent&, const juce::MouseEvent&);
    void regionMouseDrag (RegionComponent&, const juce::MouseEvent&);
    void regionMouseUp (RegionComponent&, const juce::MouseEvent&);

    /** Escape: abandons a move, trim or loop drag in progress, putting every
        region back as it was. Nothing reaches the session (drags only change
        it on mouse-up). Returns false if there was nothing to cancel. */
    bool cancel();

    /** True from a mouse-down on a region until the mouse-up (including a
        click that hasn't moved far enough to count as a drag yet). */
    bool isDragging() const noexcept    { return drag.mode != Drag::Mode::none; }

private:
    struct Drag
    {
        enum class Mode { none, pending, move, trimStart, trimEnd, loop };

        Mode mode = Mode::none;
        RegionId anchor = 0;                 // the region that was clicked
        juce::Point<int> start;              // mouse-down position in this component
        bool selectOnlyAnchorOnClick = false;
        std::vector<RegionInfo> regions;     // the selected regions as they were at mouse-down
        std::vector<int> rows;               // each one's track row
        double beatDelta = 0.0;              // move: how far in time
        int rowDelta = 0;                    // move: how many tracks down
        juce::Range<double> trim;            // trim and loop: the anchor's new edges
    };

    ArrangementView& owner;
    Drag drag;

    void updateMoveDrag (juce::Point<int>);
    void updateTrimDrag (int dx);
    void updateLoopDrag (int dx);
};

} // namespace ikaros
