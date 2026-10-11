#include "ui/widgets/ResizeHandle.h"
#include "ui/theme/Tokens.h"

namespace ikaros
{

using namespace tokens;

ResizeHandle::ResizeHandle (Orientation o) : orientation (o)
{
    setMouseCursor (o == Orientation::vertical ? juce::MouseCursor::LeftRightResizeCursor
                                               : juce::MouseCursor::UpDownResizeCursor);
    setRepaintsOnMouseActivity (true);

    // The View menu commands are the accessible way to show and hide panels.
    setAccessible (false);
}

void ResizeHandle::paint (juce::Graphics& g)
{
    if (! dragging && ! isMouseOver())
        return;

    // A 2px line along the middle of the strip, i.e. on the edge itself.
    g.setColour (colour::accent.withAlpha (dragging ? 1.0f : 0.7f));
    auto r = getLocalBounds().toFloat();

    if (orientation == Orientation::vertical)
        g.fillRect (r.withSizeKeepingCentre (2.0f, r.getHeight()));
    else
        g.fillRect (r.withSizeKeepingCentre (r.getWidth(), 2.0f));
}

void ResizeHandle::mouseEnter (const juce::MouseEvent&)    { repaint(); }
void ResizeHandle::mouseExit (const juce::MouseEvent&)     { repaint(); }

void ResizeHandle::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu() || e.getNumberOfClicks() > 1)
        return;

    dragging = true;
    dragStart = e.getScreenPosition();
    repaint();

    if (onDragStart)
        onDragStart();
}

void ResizeHandle::mouseDrag (const juce::MouseEvent& e)
{
    if (! dragging)
        return;

    // Measured on screen: the handle itself moves as the panel resizes, so
    // its own coordinates would drift.
    const auto moved = e.getScreenPosition() - dragStart;
    const auto offset = orientation == Orientation::vertical ? moved.x : moved.y;

    if (onDrag)
        onDrag (offset);
}

void ResizeHandle::mouseUp (const juce::MouseEvent&)
{
    if (! std::exchange (dragging, false))
        return;

    repaint();

    if (onDragEnd)
        onDragEnd();
}

void ResizeHandle::mouseDoubleClick (const juce::MouseEvent&)
{
    if (onDoubleClick)
        onDoubleClick();
}

} // namespace ikaros
