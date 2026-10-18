#include "ui/arrangement/ArrangementTempoLane.h"
#include "ui/arrangement/ArrangementLayout.h"
#include "ui/theme/Fonts.h"
#include "ui/theme/IkarosLookAndFeel.h"

namespace ikaros
{

using namespace tokens;
using namespace arrangement;

namespace
{
    constexpr float signatureTop = 2.0f, signatureHeight = 13.0f;   // the "3/4" labels along the top
    constexpr float graphTop = 18.0f, graphBottomGap = 5.0f;         // the tempo line below them
    constexpr float pointRadius = 3.5f, grabDistance = 6.0f;
    constexpr double minBpm = 20.0, maxBpm = 300.0;

    juce::String bpmText (double bpm)
    {
        // Whole numbers without ".0"; typed or Shift-dragged tempos keep one decimal.
        return std::abs (bpm - std::round (bpm)) < 0.05 ? juce::String (juce::roundToInt (bpm)) : juce::String (bpm, 1);
    }

    juce::String signatureText (const TimeSignatureChange& s)
    {
        return juce::String (s.numerator) + "/" + juce::String (s.denominator);
    }
}

ArrangementView::TempoLane::TempoLane (ArrangementView& o) : owner (o)
{
    setTitle ("Tempo lane");
    setTooltip ("Tempo changes: double-click to add one at a bar, drag a point to move it or change its tempo, "
                "double-click a point to type its tempo. Click a time signature to change it.");
    setWantsKeyboardFocus (true);   // for Delete on a clicked point

    tempoField.setFont (fonts::mono (textSize::sm));
    tempoField.setJustification (juce::Justification::centred);
    tempoField.setInputRestrictions (5, "0123456789.");
    tempoField.setTitle ("Tempo in BPM");
    IkarosLookAndFeel::styleInlineEditor (tempoField);
    tempoField.onReturnKey = [this] { commitTempoField(); };
    tempoField.onEscapeKey = [this] { hideTempoField(); };
    tempoField.onFocusLost = [this] { commitTempoField(); };
    addChildComponent (tempoField);
}

//==============================================================================
// Geometry
juce::Rectangle<int> ArrangementView::TempoLane::laneArea() const
{
    return getLocalBounds().withTrimmedLeft (headerWidth).withTrimmedBottom (1);
}

juce::Rectangle<float> ArrangementView::TempoLane::graphArea() const
{
    return { 0.0f, graphTop, (float) laneArea().getWidth(), (float) laneArea().getHeight() - graphTop - graphBottomGap };
}

std::pair<double, double> ArrangementView::TempoLane::bpmRange() const
{
    if (drag.has_value())
        return { drag->lowBpm, drag->highBpm };

    double low = maxBpm, high = minBpm;

    for (auto& t : owner.geometry.tempo.getTempoChanges())
    {
        low = juce::jmin (low, t.bpm);
        high = juce::jmax (high, t.bpm);
    }

    // At least 40 BPM top to bottom, so a single tempo sits in the middle
    // and small changes look small.
    const double pad = juce::jmax (10.0, (high - low) * 0.15, (40.0 - (high - low)) / 2.0);
    return { juce::jmax (0.0, low - pad), high + pad };
}

float ArrangementView::TempoLane::bpmToY (double bpm, std::pair<double, double> range) const
{
    const auto area = graphArea();
    const double proportion = (bpm - range.first) / juce::jmax (1.0, range.second - range.first);
    return area.getBottom() - (float) proportion * area.getHeight();
}

float ArrangementView::TempoLane::beatToLaneX (double beat) const   { return owner.geometry.beatToX (beat); }
double ArrangementView::TempoLane::laneXToBeat (float x) const      { return juce::jmax (0.0, owner.geometry.xToBeat (x)); }

juce::Point<float> ArrangementView::TempoLane::laneXY (const juce::MouseEvent& e) const
{
    return e.position.translated ((float) -headerWidth, 0.0f);
}

juce::Rectangle<float> ArrangementView::TempoLane::signatureBounds (int index) const
{
    const auto& sig = owner.geometry.tempo.getTimeSignatureChanges()[(size_t) index];
    const float width = juce::GlyphArrangement::getStringWidth (fonts::mono (textSize::tiny), signatureText (sig)) + 10.0f;
    return { beatToLaneX (sig.beat) + 3.0f, signatureTop, width, signatureHeight };
}

ArrangementView::TempoLane::Hit ArrangementView::TempoLane::findHit (juce::Point<float> p) const
{
    const auto& tempo = owner.geometry.tempo;
    const auto& sigs = tempo.getTimeSignatureChanges();

    if (p.x < 0.0f)
        return {};

    for (int i = 0; i < (int) sigs.size(); ++i)
        if (signatureBounds (i).contains (p))
            return { Hit::Kind::signature, i };

    const auto range = bpmRange();
    const auto& tempos = tempo.getTempoChanges();

    // Points first, the later one winning where two are close together.
    for (int i = (int) tempos.size(); --i >= 0;)
        if (p.getDistanceFrom ({ beatToLaneX (tempos[(size_t) i].beat), bpmToY (tempos[(size_t) i].bpm, range) }) <= grabDistance)
            return { Hit::Kind::point, i };

    const int index = tempo.indexOfTempoAt (laneXToBeat (p.x));

    if (std::abs (bpmToY (tempos[(size_t) index].bpm, range) - p.y) <= 5.0f)
        return { Hit::Kind::line, index };

    return {};
}

//==============================================================================
void ArrangementView::TempoLane::paint (juce::Graphics& g)
{
    auto r = getLocalBounds();
    g.fillAll (colour::sidebarBg);
    g.setColour (colour::divider);
    g.fillRect (r.removeFromBottom (1));

    auto label = r.removeFromLeft (headerWidth);
    g.setColour (colour::divider);
    g.fillRect (label.removeFromRight (1));
    g.setColour (colour::textSecondary);
    g.setFont (fonts::ui (textSize::xs).withExtraKerningFactor (0.1f));
    g.drawText ("TEMPO", label.withTrimmedLeft (12), juce::Justification::centredLeft);

    // Everything below scrolls with the timeline; keep it out of the header part.
    juce::Graphics::ScopedSaveState save (g);
    g.reduceClipRegion (r);
    g.setOrigin (r.getX(), 0);
    g.setColour (colour::windowBg);
    g.fillRect (r.withX (0));

    auto& geo = owner.geometry;
    const auto& tempo = geo.tempo;
    const float width = (float) r.getWidth();

    // Faint bar lines, as in the lanes.
    g.setColour (colour::divider);
    forEachVisibleBar (geo, r.getWidth(), barStride (geo, 8.0), [&] (int, int x) { g.fillRect (x, 0, 1, r.getHeight()); });

    // The tempo line: flat from each change to the next, then a step.
    const auto range = bpmRange();
    const auto& tempos = tempo.getTempoChanges();
    juce::Path line;

    for (size_t i = 0; i < tempos.size(); ++i)
    {
        const float x = beatToLaneX (tempos[i].beat), y = bpmToY (tempos[i].bpm, range);
        const float nextX = i + 1 < tempos.size() ? beatToLaneX (tempos[i + 1].beat) : width + 10.0f;

        if (i == 0)
            line.startNewSubPath (juce::jmin (x, -10.0f), y);
        else
            line.lineTo (x, y);

        line.lineTo (nextX, y);
    }

    auto fill = line;
    fill.lineTo (width + 10.0f, (float) r.getHeight());
    fill.lineTo (-10.0f, (float) r.getHeight());
    fill.closeSubPath();
    g.setColour (colour::accent.withAlpha (0.1f));
    g.fillPath (fill);
    g.setColour (colour::accent.withAlpha (hover.kind == Hit::Kind::line ? 1.0f : 0.8f));
    g.strokePath (line, juce::PathStrokeType (1.5f));

    // A point and its tempo at each change (the first one at the song's start).
    g.setFont (fonts::mono (textSize::tiny));

    for (size_t i = 0; i < tempos.size(); ++i)
    {
        const float x = beatToLaneX (tempos[i].beat), y = bpmToY (tempos[i].bpm, range);
        const bool lit = ((hover.kind == Hit::Kind::point || hover.kind == Hit::Kind::line) && hover.index == (int) i)
                          || (selected.kind == Hit::Kind::point && selected.index == (int) i);
        const float radius = lit ? pointRadius + 1.0f : pointRadius;

        g.setColour (selected.kind == Hit::Kind::point && selected.index == (int) i ? colour::text : colour::accent);
        g.fillEllipse (x - radius, y - radius, radius * 2.0f, radius * 2.0f);

        // The number goes above the line, or below it near the top.
        const float nextX = i + 1 < tempos.size() ? beatToLaneX (tempos[i + 1].beat) : width;
        const float textY = y - graphTop + 2.0f < 12.0f ? y + 3.0f : y - 13.0f;

        if (nextX - x > 30.0f)
        {
            g.setColour (lit ? colour::text : colour::textSecondary);
            g.drawText (bpmText (tempos[i].bpm), juce::Rectangle<float> (x + 6.0f, textY, 50.0f, 11.0f), juce::Justification::centredLeft);
        }
    }

    // Time signatures along the top, where each one starts.
    const auto& sigs = tempo.getTimeSignatureChanges();

    for (int i = 0; i < (int) sigs.size(); ++i)
    {
        const auto box = signatureBounds (i);
        const bool lit = hover.kind == Hit::Kind::signature && hover.index == i;

        g.setColour (colour::borderStrong);
        g.fillRect (beatToLaneX (sigs[(size_t) i].beat), 0.0f, 1.0f, signatureTop + signatureHeight);
        g.setColour (lit ? colour::controlFill : colour::raised);
        g.fillRoundedRectangle (box, size::radiusTiny);
        g.setColour (lit ? colour::text : colour::textSecondary);
        g.drawText (signatureText (sigs[(size_t) i]), box, juce::Justification::centred);
    }
}

//==============================================================================
// Mouse
void ArrangementView::TempoLane::mouseMove (const juce::MouseEvent& e)
{
    const auto hit = findHit (laneXY (e));

    if (hit.kind != hover.kind || hit.index != hover.index)
    {
        hover = hit;
        setMouseCursor (hit.kind == Hit::Kind::point     ? juce::MouseCursor::DraggingHandCursor
                      : hit.kind == Hit::Kind::line      ? juce::MouseCursor::UpDownResizeCursor
                      : hit.kind == Hit::Kind::signature ? juce::MouseCursor::PointingHandCursor
                                                         : juce::MouseCursor::NormalCursor);
        repaint();
    }
}

void ArrangementView::TempoLane::mouseExit (const juce::MouseEvent&)
{
    hover = {};
    repaint();
}

void ArrangementView::TempoLane::mouseDown (const juce::MouseEvent& e)
{
    drag.reset();

    if (e.x < headerWidth)
        return;

    const auto p = laneXY (e);
    const auto hit = findHit (p);
    auto& engine = owner.engine;

    if (e.mods.isPopupMenu())
    {
        if (hit.kind == Hit::Kind::signature)
        {
            showSignatureMenu (hit.index);
            return;
        }

        juce::PopupMenu menu;

        if (hit.kind == Hit::Kind::point)
        {
            // PopupMenu items with an action: the lambda runs when it's picked.
            // SafePointer: this lane could (in theory) be gone by then.
            menu.addItem ("Set Tempo...", [safeThis = juce::Component::SafePointer<TempoLane> (this), index = hit.index]
            {
                if (safeThis != nullptr)
                    safeThis->showTempoField (index);
            });

            if (hit.index > 0)
                menu.addItem ("Delete Tempo Change", [&engine, index = hit.index] { engine.removeTempoChange (index); });

            menu.addSeparator();
        }

        owner.addTempoMenuItems (menu, laneXToBeat (p.x));
        menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition());
        return;
    }

    if (hit.kind == Hit::Kind::signature)
    {
        showSignatureMenu (hit.index);
        return;
    }

    selected = hit.kind == Hit::Kind::point ? hit : Hit {};
    grabKeyboardFocus();
    repaint();

    if (hit.kind == Hit::Kind::point || hit.kind == Hit::Kind::line)
    {
        const auto& t = owner.geometry.tempo.getTempoChanges()[(size_t) hit.index];
        const auto range = bpmRange();

        Drag d;
        d.index = hit.index;
        d.movesBar = hit.kind == Hit::Kind::point && hit.index > 0;
        d.startBpm = t.bpm;
        d.startBeat = t.beat;
        d.lowBpm = range.first;
        d.highBpm = range.second;
        drag = d;
    }
}

void ArrangementView::TempoLane::mouseDrag (const juce::MouseEvent& e)
{
    if (! drag.has_value())
        return;

    auto& engine = owner.engine;

    // Up and down: the tempo, half a BPM per pixel (the lane is too short
    // to go by its own scale). Shift: a tenth of the speed, in steps of 0.1 BPM.
    const bool fine = e.mods.isShiftDown();
    const double raw = drag->startBpm - e.getDistanceFromDragStartY() * (fine ? 0.05 : 0.5);
    const double bpm = juce::jlimit (minBpm, maxBpm, fine ? std::round (raw * 10.0) / 10.0 : std::round (raw));

    // Along: the nearest bar line (points only; not the song's first tempo).
    // The engine has the up-to-date map: the lane's copy is refreshed
    // asynchronously, after the change has been made.
    const auto current = engine.getTempoMap();
    const auto& now = current.getTempoChanges()[(size_t) drag->index];
    const double beat = drag->movesBar ? current.nearestBarLine (laneXToBeat (laneXY (e).x)) : now.beat;

    if (std::abs (beat - now.beat) < 1.0e-6 && std::abs (bpm - now.bpm) < 1.0e-6)
        return;

    // The undo step starts with the first real change, so a click that only
    // selects a point leaves the undo history alone.
    if (! drag->changed)
        engine.beginUndoStep ("Tempo Change");

    drag->changed = true;

    // Past a neighbouring change: keep to the bar it's on.
    if (! engine.setTempoChange (drag->index, beat, bpm))
        engine.setTempoChange (drag->index, now.beat, bpm);
}

void ArrangementView::TempoLane::mouseUp (const juce::MouseEvent&)
{
    drag.reset();
    repaint();   // back to the range the tempos need now
}

void ArrangementView::TempoLane::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (e.x < headerWidth || e.mods.isPopupMenu())
        return;

    const auto p = laneXY (e);
    const auto hit = findHit (p);

    if (hit.kind == Hit::Kind::point)
    {
        showTempoField (hit.index);
        return;
    }

    if (hit.kind == Hit::Kind::signature)
        return;

    // A new change at the nearest bar line, at the tempo already there (so
    // nothing changes until it's dragged). There's always a tempo at bar 1.
    const auto& tempo = owner.geometry.tempo;
    const int bar = tempo.nearestBar (laneXToBeat (p.x));
    const double beat = tempo.barToBeat (bar);
    const int existing = tempo.indexOfTempoAt (beat);

    if (std::abs (tempo.getTempoChanges()[(size_t) existing].beat - beat) < 1.0e-6)
        showTempoField (existing);
    else
        owner.engine.addTempoChange (bar, tempo.tempoAt (beat).bpm);
}

void ArrangementView::TempoLane::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    owner.handleWheel (e, wheel, true);   // as over the ruler: the wheel scrolls in time
}

bool ArrangementView::TempoLane::keyPressed (const juce::KeyPress& key)
{
    if ((key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)
         && selected.kind == Hit::Kind::point && selected.index > 0)
    {
        owner.engine.removeTempoChange (selected.index);
        selected = {};
        repaint();
        return true;
    }

    if (key == juce::KeyPress::escapeKey && selected.kind != Hit::Kind::none)
    {
        selected = {};
        repaint();
        return true;
    }

    return false;   // everything else goes on to the app's shortcuts
}

void ArrangementView::TempoLane::focusLost (FocusChangeType)
{
    // A point stays selected only while the lane has the keyboard (so the
    // Delete key can't surprise anyone later).
    if (selected.kind != Hit::Kind::none && ! hasKeyboardFocus (true))
    {
        selected = {};
        repaint();
    }
}

//==============================================================================
void ArrangementView::TempoLane::showSignatureMenu (int index)
{
    const auto& sig = owner.geometry.tempo.getTimeSignatureChanges()[(size_t) index];
    auto& engine = owner.engine;

    juce::PopupMenu menu;
    menu.addSectionHeader ("Time Signature at Bar " + juce::String (sig.bar + 1));

    for (auto& choice : TempoMap::commonSignatures())
    {
        const int n = choice.first, d = choice.second;
        menu.addItem (juce::String (n) + "/" + juce::String (d), true, n == sig.numerator && d == sig.denominator,
                      [&engine, index, n, d] { engine.setTimeSignatureChange (index, n, d); });
    }

    if (index > 0)
    {
        menu.addSeparator();
        menu.addItem ("Delete Time Signature Change", [&engine, index] { engine.removeTimeSignatureChange (index); });
    }

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (signatureBounds (index).toNearestInt()
                                                                            .translated (headerWidth, 0) + getScreenPosition()));
}

void ArrangementView::TempoLane::showTempoField (int index)
{
    const auto& tempos = owner.geometry.tempo.getTempoChanges();

    if (index < 0 || index >= (int) tempos.size())
        return;

    editingIndex = index;
    const float x = beatToLaneX (tempos[(size_t) index].beat) + (float) headerWidth;
    tempoField.setBounds (juce::jlimit (headerWidth, juce::jmax (headerWidth, getWidth() - 56), juce::roundToInt (x) + 4),
                          juce::roundToInt (graphTop) - 2, 52, 20);
    tempoField.setText (bpmText (tempos[(size_t) index].bpm), false);
    tempoField.setVisible (true);
    tempoField.grabKeyboardFocus();
    tempoField.selectAll();
}

void ArrangementView::TempoLane::commitTempoField()
{
    // Hiding the field takes its focus away, which calls onFocusLost and so
    // this again: editingIndex is -1 by then, so that call does nothing.
    if (editingIndex < 0)
        return;

    const int index = editingIndex;
    const double bpm = tempoField.getText().getDoubleValue();
    hideTempoField();

    const auto map = owner.engine.getTempoMap();

    if (bpm >= minBpm && bpm <= maxBpm && index < (int) map.getTempoChanges().size()
         && std::abs (map.getTempoChanges()[(size_t) index].bpm - bpm) > 1.0e-6)
    {
        owner.engine.beginUndoStep ("Tempo");
        owner.engine.setTempoChange (index, map.getTempoChanges()[(size_t) index].beat, bpm);
    }
}

void ArrangementView::TempoLane::hideTempoField()
{
    editingIndex = -1;
    const bool hadFocus = tempoField.hasKeyboardFocus (true);
    tempoField.setVisible (false);

    if (hadFocus)
        grabKeyboardFocus();
}

} // namespace ikaros
