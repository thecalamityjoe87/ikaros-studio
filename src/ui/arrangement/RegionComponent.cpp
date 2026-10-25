#include "ui/arrangement/RegionComponent.h"
#include "ui/widgets/PaintProbe.h"
#include "ui/theme/Fonts.h"
#include "ui/theme/IkarosLookAndFeel.h"
#include "ui/theme/Tokens.h"
#include "ui/theme/TrackColours.h"
#include "ui/widgets/Icons.h"

namespace ikaros
{

using namespace tokens;

namespace
{
    constexpr float corner = 8.0f;
    constexpr int stripHeight = 20;
    constexpr int loopCornerWidth = 18;    // the loop handle (top-right) and trim handle (bottom-right) are this wide and stripHeight tall
    constexpr float fadeHandleInset = 7.0f;   // a fade handle stays this far inside the edge, so it can be grabbed
    constexpr int fadeHandleReach = 7;        // how close the mouse has to be to grab one
    constexpr float repeatAlpha = 0.45f;   // repeats are drawn fainter than the original

    juce::Font nameFont()   { return fonts::uiBold (textSize::xs); }
}

RegionComponent::RegionComponent (const RegionInfo& r, Family f)
    : info (r), family (f),
      nameWidth (juce::GlyphArrangement::getStringWidth (nameFont(), r.name))
{
    setWantsKeyboardFocus (true);
    setTitle (r.name + " region");
    setMouseCursor (juce::MouseCursor::PointingHandCursor);

    // The rename field: hidden until startRenaming(). Return keeps the new
    // name, Escape cancels, clicking elsewhere keeps it.
    nameEditor.setFont (fonts::uiBold (textSize::sm));
    nameEditor.setIndents (6, 2);
    nameEditor.setJustification (juce::Justification::centredLeft);
    nameEditor.setTitle ("Region name");
    nameEditor.onReturnKey = [this] { finishRenaming (true); };
    nameEditor.onEscapeKey = [this] { finishRenaming (false); };
    nameEditor.onFocusLost = [this] { finishRenaming (true); };
    addChildComponent (nameEditor);
}

void RegionComponent::update (const RegionInfo& r, Family f, bool isSelected, Outline newOutline)
{
    // Drawing thousands of notes is far slower than comparing them, so skip
    // the repaint when nothing this region shows has changed (e.g. another
    // track's knob was turned).
    if (r == info && f == family && isSelected == selected && newOutline == outline)
        return;

    if (r.name != info.name)
        nameWidth = juce::GlyphArrangement::getStringWidth (nameFont(), r.name);

    info = r;
    family = f;
    selected = isSelected;
    outline = newOutline;
    setTitle (r.name + " region");
    repaint();
}

juce::Colour RegionComponent::trackColour() const
{
    return theme().family (family);
}

//==============================================================================
RegionComponent::Zone RegionComponent::getZoneAt (juce::Point<int> p) const
{
    // Edge handles get narrower on short regions, so there's still a middle to grab.
    const int edge = getWidth() >= 24 ? 6 : (getWidth() >= 12 ? 3 : 0);

    if (p.x < edge)
        return Zone::leftEdge;

    // An audio region's fade handles, at the top of the waveform. Near an
    // edge they win over looping and trimming, which the rest of the edge
    // still does.
    if (! info.isMidi && getWidth() >= 32 && p.y >= stripHeight && p.y < stripHeight + fadeHandleReach)
        for (bool fadeIn : { true, false })
            if (std::abs ((float) p.x - getFadeHandle (fadeIn).x) <= (float) fadeHandleReach)
                return fadeIn ? Zone::fadeIn : Zone::fadeOut;

    // The top-right handle loops; the bottom-right one and the rest of the right edge trim.
    if (getWidth() >= 32 && p.y < stripHeight && p.x >= getWidth() - loopCornerWidth)
        return Zone::loopCorner;

    if (getTrimHandleBounds().contains (p) || p.x >= getWidth() - edge)
        return Zone::rightEdge;

    if (getTakeBadgeBounds().contains (p))
        return Zone::takeBadge;

    return p.y < stripHeight ? Zone::nameStrip : Zone::body;
}

juce::Rectangle<int> RegionComponent::getTrimHandleBounds() const
{
    // Empty when the region is too small for the loop and trim handles not to touch.
    if (getWidth() < 32 || getHeight() < 2 * stripHeight)
        return {};

    return getLocalBounds().removeFromBottom (stripHeight).removeFromRight (loopCornerWidth);
}

// "Take 2 ▾" at the start of the name strip, before the name (where it's
// seen even when the region runs off the screen); just the number on a
// narrow region; nothing on one too narrow for that.
static juce::String takeBadgeText (const RegionInfo& info, bool full)
{
    const auto number = juce::String (info.currentTake + 1);
    return full ? "Take " + number : number;
}

static constexpr int fullTakeBadgeMinWidth = 110;   // narrower regions show just the number

juce::Rectangle<int> RegionComponent::getTakeBadgeBounds() const
{
    if (! info.hasTakes() || getWidth() < 32 + loopCornerWidth)
        return {};

    const bool full = getWidth() >= fullTakeBadgeMinWidth;
    const int textWidth = juce::GlyphArrangement::getStringWidthInt (fonts::uiBold (textSize::tiny), takeBadgeText (info, full));
    const int width = textWidth + (full ? 22 : 12);   // room for the arrow on the full one
    return { 6, 3, width, stripHeight - 6 };
}

void RegionComponent::setFadePreview (std::optional<double> fadeInBeats, std::optional<double> fadeOutBeats)
{
    fadeInPreview = fadeInBeats;
    fadeOutPreview = fadeOutBeats;
    repaint();
}

double RegionComponent::getFadeLength (bool fadeIn) const
{
    // value_or: the preview if there is one, else the region's own fade.
    return fadeIn ? fadeInPreview.value_or (info.fadeInBeats) : fadeOutPreview.value_or (info.fadeOutBeats);
}

juce::Point<float> RegionComponent::getFadeHandle (bool fadeIn) const
{
    const auto range = getDisplayRange();
    const float pixelsPerBeat = range.getLength() > 0.0 ? (float) getWidth() / (float) range.getLength() : 0.0f;
    const float length = (float) getFadeLength (fadeIn) * pixelsPerBeat;
    const float x = fadeIn ? length : (float) getWidth() - length;
    return { juce::jlimit (fadeHandleInset, juce::jmax (fadeHandleInset, (float) getWidth() - fadeHandleInset), x),
             (float) stripHeight + 1.0f };
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
    IKAROS_PERF_PAINT ("RegionComponent");
    auto r = getLocalBounds().toFloat();

    // While playing, the playhead moving over a region repaints only a thin
    // strip of it, ~30 times a second for every region it crosses. So the
    // costly parts (the shaped clip, text, icons, notes) are only drawn where
    // they meet the area being repainted. Anti-aliased edges can reach a
    // pixel or so past a part's area, hence the margin.
    const auto clip = g.getClipBounds();
    auto shows = [visible = clip.toFloat()] (juce::Rectangle<float> area) { return visible.intersects (area.expanded (2.0f)); };

    // Clip everything to the rounded shape so the strip gets rounded top corners.
    juce::Path shape;
    shape.addRoundedRectangle (r, corner);

    // Away from the corners the shape is the plain rectangle, which the
    // component's own bounds already clip to.
    const int c = (int) std::ceil (corner);
    const bool clipReachesCorner = (clip.getX() < c || clip.getRight() > getWidth() - c)
                                && (clip.getY() < c || clip.getBottom() > getHeight() - c);

    {
        // ScopedSaveState restores the clip region when it goes out of scope (RAII).
        juce::Graphics::ScopedSaveState save (g);

        if (clipReachesCorner)
            g.reduceClipRegion (shape);

        // The body is a faint tint of the track's family colour, the header
        // strip the full colour, with dark or white text to suit it.
        g.setColour (trackColour().withAlpha (selected ? theme().arrangement.regionBodySelectedAlpha : theme().arrangement.regionBodyAlpha));
        g.fillRect (r);

        auto strip = r.removeFromTop ((float) stripHeight);
        g.setColour (trackColour());
        g.fillRect (strip);
        const auto label = colour::textOn (trackColour());

        // A looped region always shows its loop arrow in the header; others
        // only while the mouse is over them (so the handle can be found).
        const bool showLoopIcon = getWidth() >= 32 && (hovering || loopPreview || info.isLooped());

        // The take badge (if any) comes first; the name follows it.
        const auto badge = getTakeBadgeBounds().toFloat();
        auto nameArea = strip.reduced (8.0f, 0.0f);

        if (! badge.isEmpty())
            nameArea.setLeft (badge.getRight() + 5.0f);

        if (showLoopIcon)
            nameArea.setRight (juce::jmin (nameArea.getRight(), (float) (getWidth() - loopCornerWidth)));   // clear of the loop handle

        // A Drummer region has a small drummer before its name.
        if (info.isDrummer && nameArea.getWidth() >= 30.0f)
        {
            if (const auto icon = nameArea.removeFromLeft (13.0f); shows (icon))
                icons::draw (g, Icon::drummer, icon, label, 13.0f, 1.9f);

            nameArea.removeFromLeft (4.0f);
        }

        // The name is drawn from the left of its area (cut short if it doesn't fit).
        if (shows (nameArea.withWidth (juce::jmin (nameArea.getWidth(), nameWidth))))
        {
            g.setColour (label);
            g.setFont (nameFont());
            g.drawText (info.name, nameArea, juce::Justification::centredLeft, true);
        }

        if (! badge.isEmpty() && shows (badge))
        {
            const bool full = getWidth() >= fullTakeBadgeMinWidth;
            g.setColour (label.withAlpha (hoveringTakeBadge ? 0.32f : 0.18f));
            g.fillRoundedRectangle (badge, badge.getHeight() * 0.5f);

            auto text = badge.reduced (6.0f, 0.0f);
            g.setColour (label);
            g.setFont (fonts::uiBold (textSize::tiny));

            if (full)
            {
                // A small down arrow: it opens a menu.
                auto arrow = text.removeFromRight (7.0f).withSizeKeepingCentre (7.0f, 4.0f);
                juce::Path p;
                p.addTriangle (arrow.getTopLeft(), arrow.getTopRight(), { arrow.getCentreX(), arrow.getBottom() });
                g.fillPath (p);
                text.removeFromRight (3.0f);
            }

            g.drawText (takeBadgeText (info, full), text, juce::Justification::centred, false);
        }

        auto body = r.reduced (3.0f, 4.0f);

        if (info.isMidi)
            paintMidiNotes (g, body);
        else
            paintWaveform (g, body);

        paintLoopDividers (g, getLocalBounds().toFloat().withTrimmedTop ((float) stripHeight));

        if (! info.isMidi)
            paintFades (g, getLocalBounds().toFloat().withTrimmedTop ((float) stripHeight));

        // The loop handle: a small loop arrow in the top-right corner. It
        // shows faintly (see showLoopIcon above), and lights up over the loop
        // zone or while dragging.
        if (showLoopIcon)
        {
            const bool active = hoveringLoopCorner || loopPreview;
            auto handle = getLocalBounds().toFloat().removeFromTop ((float) stripHeight).removeFromRight ((float) loopCornerWidth);

            if (active)
            {
                g.setColour (trackColour().darker (0.3f));
                g.fillRect (handle);
            }

            if (shows (handle))
                icons::draw (g, Icon::loop, handle, label.withAlpha (active ? 1.0f : 0.55f), 16.0f, 1.6f);
        }

        // The trim handle: the same, in the bottom-right corner, with a resize arrow.
        if (const auto trimHandle = getTrimHandleBounds().toFloat(); ! trimHandle.isEmpty() && (hovering || trimPreview.has_value()))
        {
            const bool active = hoveringTrimHandle || (trimPreview.has_value() && ! loopPreview);

            g.setColour (theme().arrangement.tintDark.withAlpha (active ? 0.3f : 0.15f));
            g.fillRect (trimHandle);

            // Shifted half a pixel and stroked 1 px wide, so the shaft sits on one pixel row instead of straddling two.
            if (shows (trimHandle))
                icons::draw (g, Icon::trim, trimHandle.translated (0.0f, 0.5f),
                             label.withAlpha (active ? 1.0f : 0.55f), 16.0f, 1.5f);
        }
    }

    // Away from the corners, the outline in the clip is just its top and
    // bottom edges: filled as two strips, the same pixels as stroking the
    // whole shape. (Half of the normal 1px stroke shows, since it's centred
    // on the edge; the selected 1.5px one is inset, so all of it does.)
    const float outlineWidth = outline == Outline::normal ? 0.5f : 1.5f;

    if (! clipReachesCorner)
    {
        g.setColour (outline == Outline::normal ? trackColour().withAlpha (0.4f)
                                                : theme().arrangement.selectionOutline.withAlpha (outline == Outline::selectedTrack ? 1.0f : 0.35f));
        const auto span = getLocalBounds().toFloat().withLeft ((float) clip.getX()).withRight ((float) clip.getRight());
        g.fillRect (span.withHeight (outlineWidth));
        g.fillRect (span.withTop (span.getBottom() - outlineWidth));
    }
    else if (outline == Outline::normal)
    {
        g.setColour (trackColour().withAlpha (0.4f));
        g.strokePath (shape, juce::PathStrokeType (1.0f));
    }
    else
    {
        // A stroke is centred on the path, and the component clips whatever
        // falls outside its bounds, so the path is inset by half the width
        // to keep the whole 1.5px inside.
        constexpr float width = 1.5f;
        juce::Path inset;
        inset.addRoundedRectangle (getLocalBounds().toFloat().reduced (width * 0.5f), corner - width * 0.5f);

        g.setColour (theme().arrangement.selectionOutline.withAlpha (outline == Outline::selectedTrack ? 1.0f : 0.35f));
        g.strokePath (inset, juce::PathStrokeType (width));
    }

    if (showFocusRing && hasKeyboardFocus (false))
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

    // The pitch range above comes from all the notes on show, so the rows
    // don't move; only the notes in the strip being repainted are drawn.
    const auto clip = g.getClipBounds().toFloat();

    for (size_t i = 0; i < cycleStarts.size(); ++i)
    {
        // The first pass is the original; the repeats are fainter.
        g.setColour (i == 0 ? trackColour() : trackColour().withAlpha (repeatAlpha));

        for (auto& n : info.notes)
        {
            if (! isVisible (n, cycleStarts[i]))
                continue;

            auto x = area.getX() + (float) (cycleStarts[i] + n.startBeat + shift) * pxPerBeat;
            auto w = juce::jmax (2.0f, (float) n.lengthBeats * pxPerBeat - 1.0f);

            if (x + w + 1.0f < clip.getX() || x - 1.0f > clip.getRight())
                continue;

            auto y = area.getY() + (float) (hi - n.pitch) * rowH + (rowH - noteH) * 0.5f;

            // Between its rounded ends a note is a plain rectangle, far cheaper
            // to fill than the rounded shape (the playhead strip usually
            // crosses long notes there).
            const float radius = noteH * 0.5f;

            if (clip.getX() >= x + radius + 1.0f && clip.getRight() <= x + w - radius - 1.0f)
            {
                g.fillRect (juce::Rectangle<float> (x, y, w, noteH).withLeft (clip.getX()).withRight (clip.getRight()));
                continue;
            }

            g.fillRoundedRectangle (x, y, w, noteH, radius);
        }
    }
}

void RegionComponent::paintFades (juce::Graphics& g, juce::Rectangle<float> area)
{
    const auto range = getDisplayRange();

    if (range.getLength() <= 0.0)
        return;

    const float pixelsPerBeat = area.getWidth() / (float) range.getLength();
    const auto clip = g.getClipBounds().toFloat();

    for (bool fadeIn : { true, false })
    {
        // The fade as a line from silence at the edge up to full volume, with
        // the part it turns down shaded. The automatic edge fades are less
        // than a pixel, so they don't show.
        const float length = juce::jmin (area.getWidth(), (float) getFadeLength (fadeIn) * pixelsPerBeat);
        const float edge = fadeIn ? area.getX() : area.getRight();
        const float end = fadeIn ? edge + length : edge - length;

        // (Skipped when it's nowhere near the strip being repainted.)
        if (length >= 1.0f && clip.intersects (area.withLeft (juce::jmin (edge, end)).withRight (juce::jmax (edge, end)).expanded (2.0f)))
        {
            juce::Path faded;
            faded.startNewSubPath (edge, area.getY());
            faded.lineTo (end, area.getY());
            faded.lineTo (edge, area.getBottom());
            faded.closeSubPath();

            g.setColour (theme().arrangement.laneBg.withAlpha (0.5f));   // dimmed: quieter there
            g.fillPath (faded);
            g.setColour (trackColour());
            g.drawLine (edge, area.getBottom(), end, area.getY(), 1.5f);
        }

        // The handles, while the mouse is over the region or one is being dragged.
        if (hovering || fadeInPreview.has_value() || fadeOutPreview.has_value())
        {
            const auto handle = getFadeHandle (fadeIn);
            const auto dot = juce::Rectangle<float> (8.0f, 8.0f).withCentre (handle);
            g.setColour (theme().arrangement.text);
            g.fillEllipse (dot);
            g.setColour (theme().arrangement.laneBg);
            g.drawEllipse (dot, 1.0f);
        }
    }
}

void RegionComponent::paintWaveform (juce::Graphics& g, juce::Rectangle<float> area)
{
    // info.peaks: loudness per slice (0..1, a slice is about 10 ms) of the
    // region, or of one loop if it's looped, from the engine. Drawn
    // mirrored around the centre line, one column per pixel.
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

    // How far through the peaks (0..1) a column's left edge is.
    auto proportionAt = [&] (float x)
    {
        double proportion = (beatAt (x) + repeat.offset) / repeat.length;

        if (repeat.looping)
            proportion -= std::floor (proportion);   // the same place in every repeat

        return proportion;
    };

    auto peak = [&] (int i)    { return info.peaks[(size_t) juce::jlimit (0, numPeaks - 1, i)]; };

    // The level drawn at a column. Unlooped, while an edge is dragged
    // outwards, the new part has no peaks yet and is drawn flat until the
    // drag ends.
    auto peakAt = [&] (float x)
    {
        const double from = proportionAt (x), to = proportionAt (x + 1.0f);

        if (from < 0.0 || from >= 1.0)
            return 0.0f;

        // Zoomed out, several slices fall in one column: show the loudest,
        // so short hits don't vanish between columns.
        if (to > from && (to - from) * numPeaks > 1.0)
        {
            float loudest = 0.0f;

            for (int i = (int) (from * numPeaks), end = juce::jmin (numPeaks, (int) std::ceil (to * numPeaks)); i < end; ++i)
                loudest = juce::jmax (loudest, info.peaks[(size_t) i]);

            return loudest;
        }

        // Zoomed in, a slice is wider than a column: a straight line from
        // one slice's middle to the next, rather than flat steps.
        const double position = from * numPeaks - 0.5;
        const int i = (int) std::floor (position);
        const float t = (float) (position - i);
        return peak (i) + t * (peak (i + 1) - peak (i));
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
        g.setColour (trackColour());
        g.fillPath (wave);
        return;
    }

    // Looped: the repeats faint, then the original (up to the end of the
    // first loop) drawn again over them at full strength.
    g.setColour (trackColour().withAlpha (repeatAlpha));
    g.fillPath (wave);

    const double firstEnd = repeat.length - repeat.offset;   // beats from the region start
    const float firstEndX = area.getX() + (float) ((firstEnd - (range.getStart() - info.startBeat)) / range.getLength()) * area.getWidth();

    juce::Graphics::ScopedSaveState save (g);
    g.reduceClipRegion (area.withRight (juce::jmin (area.getRight(), firstEndX)).toNearestInt());
    g.setColour (trackColour());
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
    g.setColour (trackColour().withAlpha (0.6f));

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
                     || zone == Zone::fadeIn || zone == Zone::fadeOut
                        ? juce::MouseCursor::LeftRightResizeCursor
                        : juce::MouseCursor::PointingHandCursor);

    if ((zone == Zone::loopCorner) != hoveringLoopCorner)
    {
        hoveringLoopCorner = zone == Zone::loopCorner;
        repaint();
    }

    if (getTrimHandleBounds().contains (e.getPosition()) != hoveringTrimHandle)
    {
        hoveringTrimHandle = ! hoveringTrimHandle;
        repaint();
    }

    if ((zone == Zone::takeBadge) != hoveringTakeBadge)
    {
        hoveringTakeBadge = zone == Zone::takeBadge;
        repaint();
    }
}

void RegionComponent::mouseEnter (const juce::MouseEvent&)
{
    hovering = true;
    repaint();   // the loop handle (and an audio region's fade handles) show while the mouse is over it
}

void RegionComponent::mouseExit (const juce::MouseEvent&)
{
    hovering = false;
    repaint();

    if (hoveringLoopCorner || hoveringTrimHandle || hoveringTakeBadge)
    {
        hoveringLoopCorner = hoveringTrimHandle = hoveringTakeBadge = false;
        repaint();
    }
}

void RegionComponent::mouseDown (const juce::MouseEvent& e)
{
    grabKeyboardFocus();

    // The take badge is a button of its own, not the start of a drag.
    pressedTakeBadge = e.mods.isLeftButtonDown() && getZoneAt (e.getPosition()) == Zone::takeBadge && onTakeBadge;

    if (pressedTakeBadge)
    {
        onTakeBadge (*this);
        return;
    }

    if (onMouseDown)
        onMouseDown (*this, e);
}

void RegionComponent::mouseDrag (const juce::MouseEvent& e)
{
    if (onMouseDrag && ! pressedTakeBadge)
        onMouseDrag (*this, e);
}

void RegionComponent::mouseUp (const juce::MouseEvent& e)
{
    if (onMouseUp && ! std::exchange (pressedTakeBadge, false))
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
