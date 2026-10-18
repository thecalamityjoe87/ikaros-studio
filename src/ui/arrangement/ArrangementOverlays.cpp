#include "ui/arrangement/ArrangementOverlays.h"
#include "ui/theme/Fonts.h"
#include "ui/theme/IkarosLookAndFeel.h"
#include "ui/theme/Tokens.h"

namespace ikaros
{

using namespace tokens;

//==============================================================================
// DropZone
ArrangementView::DropZone::DropZone()
{
    setInterceptsMouseClicks (false, false);
    setAccessible (false);   // nothing to do with it except drop things on it
}

void ArrangementView::DropZone::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f);

    juce::Path outline, dashed;
    outline.addRoundedRectangle (r, size::radiusCard);
    const float dashes[] = { 5.0f, 4.0f };
    juce::PathStrokeType (1.0f).createDashedStroke (dashed, outline, dashes, 2);

    g.setColour (colour::borderStrong);
    g.fillPath (dashed);

    g.setColour (colour::textSecondary);
    g.setFont (fonts::ui (textSize::lg));
    g.drawText ("Drop a sound, loop or audio file here", r, juce::Justification::centred);
}

//==============================================================================
// Playhead
ArrangementView::Playhead::Playhead (ArrangementView& o) : owner (o)
{
    setInterceptsMouseClicks (true, false);
    setMouseCursor (juce::MouseCursor::LeftRightResizeCursor);
    setAccessible (false);   // the ruler is the accessible way to move it
}

bool ArrangementView::Playhead::hitTest (int px, int py)
{
    return py < size::rulerHeight && std::abs ((float) px - x) <= grabRadius;
}

void ArrangementView::Playhead::mouseDown (const juce::MouseEvent& e)
{
    // Keep the grab point under the mouse, so the playhead doesn't jump
    // when it's picked up a few pixels off-centre.
    dragging = true;
    grabOffset = (float) e.x - x;
}

void ArrangementView::Playhead::mouseDrag (const juce::MouseEvent& e)
{
    dragX = (float) e.x - grabOffset;
    owner.dragPlayheadTo (dragX);

    // Held past either edge: keep scrolling even when the mouse is still.
    if (dragX < 0.0f || dragX > (float) getWidth())
        startTimerHz (30);
    else
        stopTimer();
}

void ArrangementView::Playhead::mouseUp (const juce::MouseEvent& e)
{
    stopTimer();
    dragging = false;
    setHighlighted (isMouseOver() && hitTest (e.x, e.y));
}

void ArrangementView::Playhead::setX (float newX)
{
    if (std::abs (newX - x) < 0.25f)
        return;

    // Only repaint the thin strips that changed, not the whole timeline.
    repaint (stripAt (x));
    x = newX;
    repaint (stripAt (x));
}

void ArrangementView::Playhead::paint (juce::Graphics& g)
{
    if (x < -8.0f || x > (float) getWidth() + 8.0f)
        return;

    g.setColour (highlighted ? colour::accent.brighter (0.3f) : colour::accent);
    g.fillRect (x - 1.0f, 0.0f, 2.0f, (float) getHeight());

    if (highlighted)
        g.fillRoundedRectangle (x - 8.0f, 0.0f, 16.0f, 14.0f, 4.5f);
    else
        g.fillRoundedRectangle (x - 7.0f, 0.0f, 14.0f, 12.0f, size::radiusXs);
}

void ArrangementView::Playhead::timerCallback()
{
    if (! dragging || ! juce::Component::isMouseButtonDownAnywhere())
        stopTimer();
    else
        owner.dragPlayheadTo (dragX);
}

void ArrangementView::Playhead::setHighlighted (bool shouldHighlight)
{
    if (highlighted != shouldHighlight)
    {
        highlighted = shouldHighlight;
        repaint (stripAt (x));
    }
}

juce::Rectangle<int> ArrangementView::Playhead::stripAt (float px) const
{
    return { juce::roundToInt (px) - 9, 0, 18, getHeight() };
}

//==============================================================================
// Overlay
ArrangementView::Overlay::Overlay()
{
    setInterceptsMouseClicks (false, false);
    setAccessible (false);
}

void ArrangementView::Overlay::setGhosts (std::vector<Ghost> newGhosts)
{
    ghosts = std::move (newGhosts);
    repaint();
}

void ArrangementView::Overlay::setDropTarget (juce::Rectangle<float> area, float markerX, const juce::String& text)
{
    dropArea = area;
    dropMarkerX = markerX;
    dropText = text;
    repaint();
}

void ArrangementView::Overlay::clear()
{
    ghosts.clear();
    dropArea = {};
    repaint();
}

void ArrangementView::Overlay::paint (juce::Graphics& g)
{
    for (auto& ghost : ghosts)
    {
        g.setColour (ghost.colour.withAlpha (0.3f));
        g.fillRoundedRectangle (ghost.area, size::radius);
        g.setColour (ghost.colour);
        g.drawRoundedRectangle (ghost.area.reduced (0.75f), size::radius, 1.5f);
        g.setColour (colour::text);
        g.setFont (fonts::uiBold (textSize::xs));
        g.drawText (ghost.name, ghost.area.reduced (8.0f, 3.0f).removeFromTop (14.0f),
                    juce::Justification::centredLeft, true);
    }

    if (! dropArea.isEmpty())
    {
        g.setColour (colour::highlight.withAlpha (0.08f));
        g.fillRoundedRectangle (dropArea, size::radiusLarge);
        g.setColour (colour::highlight);
        g.drawRoundedRectangle (dropArea.reduced (1.0f), size::radiusLarge, 1.5f);
        // Where a file will start. (Sounds have no position: markerX < 0.)
        const bool hasMarker = dropMarkerX >= 0.0f;

        if (hasMarker)
            g.fillRect (dropMarkerX - 1.0f, dropArea.getY(), 2.0f, dropArea.getHeight());

        g.setFont (fonts::uiBold (textSize::md));
        g.drawText (dropText, dropArea.withLeft (hasMarker ? dropMarkerX + 8.0f : dropArea.getX() + 14.0f).reduced (0.0f, 6.0f),
                    juce::Justification::topLeft, true);
    }
}

} // namespace ikaros
