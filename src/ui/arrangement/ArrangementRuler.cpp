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
    setTooltip ("Click to move the playhead, or drag its head. Drag along the top edge to set the loop.");
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
    g.setColour (colour::divider);
    g.fillRect (label.removeFromRight (1));
    g.setColour (colour::textSecondary);
    g.setFont (fonts::ui (11.0f).withExtraKerningFactor (0.1f));
    g.drawText ("TRACKS", label.withTrimmedLeft (12), juce::Justification::centredLeft);

    // Everything below scrolls with the timeline; keep it out of the header part.
    juce::Graphics::ScopedSaveState save (g);
    g.reduceClipRegion (r);
    g.setOrigin (r.getX(), 0);
    auto& geo = owner.geometry;

    // Loop range: an amber bar along the top.
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
    g.setFont (fonts::mono (11.0f));

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

    draggingLoop = e.y < loopStripHeight;
    dragStartBeat = beatAt (e.x);
    mouseDrag (e);
}

void ArrangementView::Ruler::mouseDrag (const juce::MouseEvent& e)
{
    if (e.mouseDownPosition.x < headerWidth)
        return;

    auto beat = beatAt (e.x);
    auto& engine = owner.engine;

    if (draggingLoop)
    {
        // Snap the loop to whole bars.
        auto bpb = owner.geometry.beatsPerBar;
        auto a = std::floor (juce::jmin (dragStartBeat, beat) / bpb) * bpb;
        auto b = std::ceil (juce::jmax (dragStartBeat, beat) / bpb) * bpb;

        if (b <= a)
            b = a + bpb;

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
