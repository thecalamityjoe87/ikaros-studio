#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace ikaros
{

//==============================================================================
/**
    A thin, invisible strip laid over the edge between two panels. Dragging it
    resizes a panel; double-clicking resets it. A light line shows while it's
    hovered or dragged.

    With a grip (setShowsGrip), it's a visible strip instead: a short bar
    in the middle shows where to grab it (the bottom panel's top edge).

    The owner does the actual resizing: it remembers the panel's size in
    onDragStart and applies the offset it gets in onDrag.
*/
class ResizeHandle  : public juce::Component
{
public:
    /** vertical: a vertical edge, dragged left and right.
        horizontal: a horizontal edge, dragged up and down. */
    enum class Orientation { vertical, horizontal };

    explicit ResizeHandle (Orientation);

    /** How thick to make the strip. Lay it out centred on the edge. */
    static constexpr int thickness = 6;

    bool isDragging() const noexcept        { return dragging; }

    /** Draws a 40x3 grip in the middle instead of the hover line. */
    void setShowsGrip (bool shouldShow)     { showsGrip = shouldShow; repaint(); }

    std::function<void()> onDragStart;
    std::function<void (int offset)> onDrag;   // pixels from where the drag started (right/down = positive)
    std::function<void()> onDragEnd;
    std::function<void()> onDoubleClick;
    std::function<void()> onClick;           // pressed and let go without dragging

    void paint (juce::Graphics&) override;
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

private:
    const Orientation orientation;
    bool dragging = false;
    bool showsGrip = false;
    juce::Point<int> dragStart;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ResizeHandle)
};

} // namespace ikaros
