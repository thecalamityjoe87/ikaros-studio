#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace ikaros
{

//==============================================================================
/**
    A thin, invisible strip laid over the edge between two panels. Dragging it
    resizes a panel; double-clicking resets it. An accent line shows while it's
    hovered or dragged.

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

    std::function<void()> onDragStart;
    std::function<void (int offset)> onDrag;   // pixels from where the drag started (right/down = positive)
    std::function<void()> onDragEnd;
    std::function<void()> onDoubleClick;

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
    juce::Point<int> dragStart;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ResizeHandle)
};

} // namespace ikaros
