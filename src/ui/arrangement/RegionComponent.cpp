#include "ui/arrangement/RegionComponent.h"
#include "ui/theme/Fonts.h"
#include "ui/theme/IkarosLookAndFeel.h"
#include "ui/theme/Tokens.h"
#include "ui/widgets/Icons.h"

namespace ikaros
{

using namespace tokens;

namespace
{
    constexpr float corner = 8.0f;
    constexpr int stripHeight = 20;
    constexpr int loopCornerWidth = 14;    // the loop handle in the top-right corner
    constexpr float repeatAlpha = 0.45f;   // repeats are drawn fainter than the original
}

RegionComponent::RegionComponent (const RegionInfo& r, juce::Colour c)
    : info (r), colour (c)
{
    setWantsKeyboardFocus (true);
    setTitle (r.name + " region");
    setMouseCursor (juce::MouseCursor::PointingHandCursor);

    // The rename field: hidden until startRenaming(). Return keeps the new
    // name, Escape cancels, clicking elsewhere keeps it.
    nameEditor.setFont (fonts::uiBold (12.0f));
    nameEditor.setIndents (6, 2);
    nameEditor.setJustification (juce::Justification::centredLeft);
    nameEditor.setTitle ("Region name");
    nameEditor.onReturnKey = [this] { finishRenaming (true); };
    nameEditor.onEscapeKey = [this] { finishRenaming (false); };
    nameEditor.onFocusLost = [this] { finishRenaming (true); };
    addChildComponent (nameEditor);
}

void RegionComponent::update (const RegionInfo& r, juce::Colour c, bool isSelected)
{
    // Drawing thousands of notes is far slower than comparing them, so skip
    // the repaint when nothing this region shows has changed (e.g. another
    // track's knob was turned).
    if (r == info && c == colour && isSelected == selected)
        return;

    info = r;
    colour = c;
    selected = isSelected;
    setTitle (r.name + " region");
    repaint();
}

//==============================================================================
RegionComponent::Zone RegionComponent::getZoneAt (juce::Point<int> p) const
{
    // Edge handles get narrower on short regions, so there's still a middle to grab.
    const int edge = getWidth() >= 24 ? 6 : (getWidth() >= 12 ? 3 : 0);

    if (p.x < edge)
        return Zone::leftEdge;

    // The top of the right edge loops; the rest of it trims.
    if (getWidth() >= 32 && p.y < stripHeight && p.x >= getWidth() - loopCornerWidth)
        return Zone::loopCorner;

    if (p.x >= getWidth() - edge)
        return Zone::rightEdge;

    return p.y < stripHeight ? Zone::nameStrip : Zone::body;
}

void RegionComponent::setTrimPreview (std::optional<juce::Range<double>> range, bool asLoop)
{
    trimPreview = range;
    loopPreview = range.has_value() && asLoop;
    repaint();
}

RegionComponent::Repeat RegionComponent::getRepeat() const
{
    if (info.isLooped())
        return { true, info.loopLengthBeats, info.loopOffsetBeats };

    // Not looped yet, but being dragged by its loop corner: preview the
    // whole region repeating.
    return { loopPreview, info.lengthBeats, 0.0 };
}

juce::Range<double> RegionComponent::getDisplayRange() const
{
    return trimPreview.value_or (juce::Range<double> (info.startBeat, info.getEndBeat()));
}

//==============================================================================
void RegionComponent::startRenaming()
{
    renaming = true;
    nameEditor.setText (info.name, false);
    nameEditor.setVisible (true);
    nameEditor.grabKeyboardFocus();
    nameEditor.selectAll();
}

void RegionComponent::finishRenaming (bool keepNewName)
{
    // Hiding the field takes its focus away, which calls onFocusLost and so
    // this function again; the flag makes that second call do nothing.
    if (! renaming)
        return;

    renaming = false;
    const bool editorHadFocus = nameEditor.hasKeyboardFocus (true);
    nameEditor.setVisible (false);

    if (editorHadFocus)
        grabKeyboardFocus();   // Return/Escape: focus back on the region

    if (keepNewName && onRename)
        onRename (nameEditor.getText());
}

void RegionComponent::resized()
{
    nameEditor.setBounds (getLocalBounds().removeFromTop (stripHeight).reduced (2, 1));
}

//==============================================================================
void RegionComponent::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();

    // Clip everything to the rounded shape so the strip gets rounded top corners.
    juce::Path shape;
    shape.addRoundedRectangle (r, corner);

    {
        // ScopedSaveState restores the clip region when it goes out of scope (RAII).
        juce::Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (shape);

        g.setColour (colour.withAlpha (selected ? 0.24f : 0.14f));
        g.fillRect (r);

        auto strip = r.removeFromTop ((float) stripHeight);
        g.setColour (colour::regionStrip (colour));
        g.fillRect (strip);

        g.setColour (colour::regionStripText (colour));
        g.setFont (fonts::uiBold (11.0f));
        g.drawText (info.name, strip.reduced (8.0f, 0.0f), juce::Justification::centredLeft, true);

        auto body = r.reduced (3.0f, 4.0f);

        if (info.isMidi)
            paintMidiNotes (g, body);
        else
            paintWaveform (g, body);

        paintLoopDividers (g, getLocalBounds().toFloat().withTrimmedTop ((float) stripHeight));

        // The loop handle: a small loop arrow in the corner, while the mouse
        // is over it or it's being dragged.
        if (hoveringLoopCorner || loopPreview)
        {
            auto handle = getLocalBounds().toFloat().removeFromTop ((float) stripHeight).removeFromRight ((float) loopCornerWidth + 4.0f);
            g.setColour (colour::regionStrip (colour).darker (0.3f));
            g.fillRect (handle);
            icons::draw (g, Icon::loop, handle, colour::regionStripText (colour), 12.0f, 1.6f);
        }
    }

    g.setColour (selected ? colour : colour.withAlpha (0.4f));
    g.strokePath (shape, juce::PathStrokeType (selected ? 2.0f : 1.0f));

    if (hasKeyboardFocus (false))
        IkarosLookAndFeel::drawFocusRing (g, getLocalBounds().toFloat(), corner);
}

void RegionComponent::paintMidiNotes (juce::Graphics& g, juce::Rectangle<float> area)
{
    const auto range = getDisplayRange();
    const auto repeat = getRepeat();

    if (info.notes.empty() || range.getLength() <= 0.0)
        return;

    // Positions here are in beats from the region's real start. While an
    // edge is being dragged, the displayed start differs: shift by this.
    const double shift = info.startBeat - range.getStart();
    const double visibleEnd = range.getEnd() - info.startBeat;

    // A looped region's notes are one loop's worth (see RegionInfo); notes
    // outside the loop don't play.
    auto inLoop = [&] (const NoteInfo& n) { return ! repeat.looping || (n.startBeat >= 0.0 && n.startBeat < repeat.length); };

    // Each repeat of the content starts at one of these beats. Unlooped,
    // there's just the one, at 0.
    std::vector<double> cycleStarts { -repeat.offset };

    if (repeat.looping && repeat.length > 0.0)
        for (double c = repeat.length - repeat.offset; c < visibleEnd; c += repeat.length)
            cycleStarts.push_back (c);

    auto isVisible = [&] (const NoteInfo& n, double cycleStart)
    {
        const double start = cycleStart + n.startBeat + shift;
        return inLoop (n) && start < range.getLength() && start + n.lengthBeats > 0.0;
    };

    // Fit the pitch range of the notes on show into the available height.
    int lo = 127, hi = 0;

    for (auto& n : info.notes)
    {
        for (auto c : cycleStarts)
        {
            if (isVisible (n, c))
            {
                lo = juce::jmin (lo, n.pitch);
                hi = juce::jmax (hi, n.pitch);
                break;
            }
        }
    }

    if (lo > hi)
        return;

    lo -= 1;
    hi += 1;
    const float rowH = area.getHeight() / (float) (hi - lo + 1);
    const float noteH = juce::jlimit (2.0f, 4.0f, rowH);
    const float pxPerBeat = area.getWidth() / (float) range.getLength();

    for (size_t i = 0; i < cycleStarts.size(); ++i)
    {
        // The first pass is the original; the repeats are fainter.
        g.setColour (i == 0 ? colour : colour.withAlpha (repeatAlpha));

        for (auto& n : info.notes)
        {
            if (! isVisible (n, cycleStarts[i]))
                continue;

            auto x = area.getX() + (float) (cycleStarts[i] + n.startBeat + shift) * pxPerBeat;
            auto w = juce::jmax (2.0f, (float) n.lengthBeats * pxPerBeat - 1.0f);
            auto y = area.getY() + (float) (hi - n.pitch) * rowH + (rowH - noteH) * 0.5f;
            g.fillRoundedRectangle (x, y, w, noteH, noteH * 0.5f);
        }
    }
}

void RegionComponent::paintWaveform (juce::Graphics& g, juce::Rectangle<float> area)
{
    // info.peaks: loudness per slice (0..1) of the region, or of one loop if
    // it's looped, from the engine. Drawn mirrored around the centre line,
    // one column per pixel.
    const auto range = getDisplayRange();
    const auto repeat = getRepeat();

    if (info.peaks.empty() || repeat.length <= 0.0 || area.getWidth() <= 0.0f)
        return;

    const float mid = area.getCentreY();
    const float halfHeight = area.getHeight() * 0.48f;
    const auto numPeaks = (int) info.peaks.size();

    // The beat (from the region's real start) under a column.
    auto beatAt = [&] (float x)
    {
        return range.getStart() - info.startBeat + (double) (x / area.getWidth()) * range.getLength();
    };

    // The peak under a column. Unlooped, while an edge is dragged outwards,
    // the new part has no peaks yet and is drawn flat until the drag ends.
    auto peakAt = [&] (float x)
    {
        double proportion = (beatAt (x) + repeat.offset) / repeat.length;

        if (repeat.looping)
            proportion -= std::floor (proportion);   // the same place in every repeat

        if (proportion < 0.0 || proportion >= 1.0)
            return 0.0f;

        return info.peaks[(size_t) juce::jlimit (0, numPeaks - 1, (int) (proportion * numPeaks))];
    };

    // Zoomed in, a region can be far wider than the screen: only trace the
    // part that's being repainted.
    auto visible = g.getClipBounds().toFloat().getIntersection (area);

    if (visible.isEmpty())
        return;

    const float from = std::floor (visible.getX() - area.getX());
    const float to = std::ceil (visible.getRight() - area.getX());

    juce::Path wave;
    wave.startNewSubPath (area.getX() + from, mid);

    for (float x = from; x <= to; x += 1.0f)
        wave.lineTo (area.getX() + x, mid - peakAt (x) * halfHeight);

    for (float x = to; x >= from; x -= 1.0f)
        wave.lineTo (area.getX() + x, mid + peakAt (x) * halfHeight);

    wave.closeSubPath();

    if (! repeat.looping)
    {
        g.setColour (colour);
        g.fillPath (wave);
        return;
    }

    // Looped: the repeats faint, then the original (up to the end of the
    // first loop) drawn again over them at full strength.
    g.setColour (colour.withAlpha (repeatAlpha));
    g.fillPath (wave);

    const double firstEnd = repeat.length - repeat.offset;   // beats from the region start
    const float firstEndX = area.getX() + (float) ((firstEnd - (range.getStart() - info.startBeat)) / range.getLength()) * area.getWidth();

    juce::Graphics::ScopedSaveState save (g);
    g.reduceClipRegion (area.withRight (juce::jmin (area.getRight(), firstEndX)).toNearestInt());
    g.setColour (colour);
    g.fillPath (wave);
}

void RegionComponent::paintLoopDividers (juce::Graphics& g, juce::Rectangle<float> area)
{
    const auto range = getDisplayRange();
    const auto repeat = getRepeat();

    if (! repeat.looping || repeat.length <= 0.0 || range.getLength() <= 0.0)
        return;

    // A thin line where each repeat begins.
    const float pxPerBeat = area.getWidth() / (float) range.getLength();
    const double shift = info.startBeat - range.getStart();
    g.setColour (colour.withAlpha (0.6f));

    for (double c = repeat.length - repeat.offset; c + shift < range.getLength(); c += repeat.length)
    {
        const float x = area.getX() + (float) (c + shift) * pxPerBeat;

        if (x > area.getX() + 1.0f)
            g.fillRect (x, area.getY(), 1.0f, area.getHeight());
    }
}

//==============================================================================
void RegionComponent::mouseMove (const juce::MouseEvent& e)
{
    // Edges show a resize cursor, so it's clear they can be dragged; the
    // loop corner shows its loop arrow.
    auto zone = getZoneAt (e.getPosition());
    setMouseCursor (zone == Zone::leftEdge || zone == Zone::rightEdge || zone == Zone::loopCorner
                        ? juce::MouseCursor::LeftRightResizeCursor
                        : juce::MouseCursor::PointingHandCursor);

    if ((zone == Zone::loopCorner) != hoveringLoopCorner)
    {
        hoveringLoopCorner = zone == Zone::loopCorner;
        repaint();
    }
}

void RegionComponent::mouseExit (const juce::MouseEvent&)
{
    if (hoveringLoopCorner)
    {
        hoveringLoopCorner = false;
        repaint();
    }
}

void RegionComponent::mouseDown (const juce::MouseEvent& e)
{
    grabKeyboardFocus();

    if (onMouseDown)
        onMouseDown (*this, e);
}

void RegionComponent::mouseDrag (const juce::MouseEvent& e)
{
    if (onMouseDrag)
        onMouseDrag (*this, e);
}

void RegionComponent::mouseUp (const juce::MouseEvent& e)
{
    if (onMouseUp)
        onMouseUp (*this, e);
}

void RegionComponent::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (onDoubleClick)
        onDoubleClick (*this, getZoneAt (e.getPosition()));
}

bool RegionComponent::keyPressed (const juce::KeyPress& key)
{
    // Delete, copy, paste etc. are app commands (see MainComponent), so they
    // also work from the menus. Return is the one key a region handles itself.
    if (key == juce::KeyPress::escapeKey && onEscape && onEscape())
        return true;

    if (key == juce::KeyPress::returnKey && onOpenEditor)
    {
        onOpenEditor();
        return true;
    }

    return false;
}

} // namespace ikaros
