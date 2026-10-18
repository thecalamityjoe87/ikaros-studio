#include "ui/arrangement/ArrangementRuler.h"
#include "ui/arrangement/ArrangementLayout.h"
#include "ui/theme/Fonts.h"

namespace ikaros
{

using namespace tokens;
using namespace arrangement;

namespace
{
    constexpr int loopStripHeight = 14;    // top part of the ruler where dragging sets the loop
}

ArrangementView::Ruler::Ruler (ArrangementView& o) : owner (o)
{
    setTitle ("Bar ruler");
    setTooltip ("Click to move the playhead, or drag its head. Drag along the top edge to set the loop. "
                "Right-click for tempo and time-signature changes.");
}

void ArrangementView::Ruler::setLoop (bool on, double start, double end)
{
    if (on != looping || start != loopStart || end != loopEnd)
    {
        looping = on;
        loopStart = start;
        loopEnd = end;
        repaint();
    }
}

void ArrangementView::Ruler::paint (juce::Graphics& g)
{
    auto r = getLocalBounds();
    g.fillAll (colour::sidebarBg);
    g.setColour (colour::divider);
    g.fillRect (r.removeFromBottom (1));

    auto label = r.removeFromLeft (headerWidth);
    g.setColour (colour::trackColumnBg);
    g.fillRect (label);
    g.setColour (colour::divider);
    g.fillRect (label.removeFromRight (1));
    g.setColour (colour::textSecondary);
    g.setFont (columnLabelFont());
    g.drawText ("TRACKS", label.withTrimmedLeft (columnPadding), juce::Justification::centredLeft);

    // Everything below scrolls with the timeline; keep it out of the header part.
    juce::Graphics::ScopedSaveState save (g);
    g.reduceClipRegion (r);
    g.setOrigin (r.getX(), 0);
    auto& geo = owner.geometry;

    // Cycle range: an amber bar along the top (the accent: cycle is on).
    if (looping && loopEnd > loopStart)
    {
        auto x1 = geo.beatToX (loopStart);
        auto x2 = geo.beatToX (loopEnd);
        g.setColour (colour::accent.withAlpha (0.4f));
        g.fillRoundedRectangle (x1, 4.0f, x2 - x1, 7.0f, 3.5f);
    }

    // Bar numbers with a tick on the left of each. Zoomed out, only every
    // 2nd/4th/... bar gets a number so they don't overlap.
    const int labelStride = barStride (geo, 36.0);
    const int bottom = getHeight() - 1;
    g.setFont (fonts::mono (textSize::xs));

    forEachVisibleBar (geo, r.getWidth(), barStride (geo, 6.0), [&] (int bar, int x)
    {
        const bool labelled = bar % labelStride == 0;
        g.setColour (colour::borderStrong);
        g.fillRect (x, bottom - (labelled ? 16 : 7), 1, labelled ? 14 : 5);

        if (labelled)
        {
            g.setColour (colour::textSecondary);
            g.drawText (juce::String (bar + 1), x + 4, bottom - 16, 40, 14, juce::Justification::centredLeft);
        }
    });
}

void ArrangementView::Ruler::mouseDown (const juce::MouseEvent& e)
{
    if (e.x < headerWidth)
        return;

    if (e.mods.isPopupMenu())
    {
        // Tempo and time-signature changes at the bar nearest the click.
        juce::PopupMenu menu;
        owner.addTempoMenuItems (menu, beatAt (e.x));
        menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition());
        ignoreDrag = true;
        return;
    }

    ignoreDrag = false;
    draggingLoop = e.y < loopStripHeight;
    dragStartBeat = beatAt (e.x);
    mouseDrag (e);
}

void ArrangementView::Ruler::mouseDrag (const juce::MouseEvent& e)
{
    if (e.mouseDownPosition.x < headerWidth || ignoreDrag)
        return;

    auto beat = beatAt (e.x);
    auto& engine = owner.engine;

    if (draggingLoop)
    {
        // Snap the loop to whole bars: from the start of the first bar to
        // the end of the last (bars can differ in length).
        const auto& tempo = owner.geometry.tempo;
        const int firstBar = tempo.barAt (juce::jmin (dragStartBeat, beat));
        const int lastBar = juce::jmax (firstBar, tempo.barAt (juce::jmax (dragStartBeat, beat) - 1.0e-9));
        const double a = tempo.barToBeat (firstBar);
        const double b = tempo.barToBeat (lastBar + 1);

        engine.setLoopRangeBeats (a, b);
        engine.setLooping (true);
    }
    else
    {
        engine.setPositionBeats (owner.snapBeat (beat));
    }
}

void ArrangementView::Ruler::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    owner.handleWheel (e, wheel, true);   // over the ruler, the wheel always scrolls in time
}

double ArrangementView::Ruler::beatAt (int x) const
{
    return juce::jmax (0.0, owner.geometry.xToBeat ((float) (x - headerWidth)));
}

} // namespace ikaros
