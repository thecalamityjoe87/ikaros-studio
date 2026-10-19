// The piano roll's drawing: toolbar, keys, grid, notes and velocity lane.

#include "ui/bottompanel/PianoRoll.h"
#include "ui/bottompanel/PianoRollLayout.h"
#include "ui/widgets/Icons.h"
#include "ui/theme/Fonts.h"
#include "ui/theme/Tokens.h"

namespace ikaros
{

using namespace tokens;
using namespace pianoroll;

//==============================================================================
// Painting
void PianoRoll::paint (juce::Graphics& g)
{
    // The note grid is part of the arrangement: dark in both themes.
    g.fillAll (theme().arrangement.laneBg);

    if (! region.has_value() || ! region->isMidi)
    {
        g.setColour (theme().arrangement.textSecondary);
        g.setFont (fonts::ui (textSize::lg));
        g.drawText ("Select a MIDI region to edit it here. Double-click an empty lane to create one.",
                    getLocalBounds(), juce::Justification::centred);
        return;
    }

    paintToolbar (g);
    paintGrid (g);
    paintNotes (g);
    paintKeys (g);
    paintVelocities (g);
}

void PianoRoll::paintToolbar (juce::Graphics& g)
{
    g.setColour (theme().bottomPanelBg);
    g.fillRect (toolbarArea);
    g.setColour (theme().divider);
    g.fillRect (toolbarArea.removeFromBottom (1));

    g.setColour (theme().textSecondary);
    g.setFont (fonts::ui (textSize::sm));
    g.drawFittedText (juce::String::fromUTF8 ("Click to add a note \xc2\xb7 Drag to move \xc2\xb7 Drag the end to resize \xc2\xb7 Shift-drag to select"),
                      hintArea, juce::Justification::centredLeft, 1, 1.0f);

    icons::draw (g, Icon::search, timeZoomIcon.toFloat(), theme().textSecondary, 13.0f, 1.6f);
    icons::draw (g, Icon::keyboards, noteZoomIcon.toFloat(), theme().textSecondary, 15.0f, 1.6f);
}

void PianoRoll::paintKeys (juce::Graphics& g)
{
    juce::Graphics::ScopedSaveState save (g);
    g.reduceClipRegion (keysArea);

    g.setColour (theme().rulerBg);
    g.fillRect (keysArea);

    const int firstPitch = yToPitch (gridArea.getBottom());
    const int lastPitch = yToPitch (gridArea.getY());

    for (int note = firstPitch; note <= lastPitch; ++note)
    {
        auto row = juce::Rectangle<float> ((float) keysArea.getX(), pitchToY (note), (float) keysArea.getWidth(), rowHeight);
        const bool down = note == previewNote;

        g.setColour (down ? (isBlackKey (note) ? theme().pianoBlackDown : theme().pianoWhiteDown)
                          : isBlackKey (note) ? theme().rollKeyBlack : theme().rollKeyWhite);
        g.fillRect (row.reduced (0.0f, 0.5f));

        if (note % 12 == 0 && rowHeight >= 8.0f)
        {
            g.setColour (theme().pianoLabel);
            g.setFont (fonts::ui (juce::jmin (10.0f, rowHeight - 1.0f)));
            g.drawText ("C" + juce::String (note / 12 - 1), row.withTrimmedRight (3.0f), juce::Justification::centredRight);
        }
    }
}

void PianoRoll::paintGrid (juce::Graphics& g)
{
    juce::Graphics::ScopedSaveState save (g);
    g.reduceClipRegion (gridArea);
    auto area = gridArea.toFloat();

    // Rows: darker for black keys, a faint line between rows.
    const int firstPitch = yToPitch (gridArea.getBottom());
    const int lastPitch = yToPitch (gridArea.getY());

    for (int note = firstPitch; note <= lastPitch; ++note)
    {
        const float y = pitchToY (note);

        if (isBlackKey (note))
        {
            g.setColour (theme().arrangement.tintDark.withAlpha (0.18f));
            g.fillRect (area.getX(), y, area.getWidth(), rowHeight);
        }

        g.setColour (theme().arrangement.tintLight.withAlpha (note % 12 == 0 ? 0.07f : 0.035f));
        g.fillRect (area.getX(), y + rowHeight - 1.0f, area.getWidth(), 1.0f);
    }

    // Vertical lines on the song's timeline: grid steps (when they're far
    // enough apart to see), beats, and bars.
    const double regionStart = contentStartBeat();
    auto drawLines = [&] (double step, float alpha)
    {
        if (step <= 0.0 || step * pixelsPerBeat < 6.0)
            return;

        const double first = std::ceil ((regionStart + scrollBeats) / step) * step;

        for (double beat = first; beat - regionStart <= scrollBeats + visibleBeats(); beat += step)
        {
            g.setColour (theme().arrangement.tintLight.withAlpha (alpha));
            g.fillRect (beatToX (beat - regionStart), area.getY(), 1.0f, area.getHeight());
        }
    };

    if (grid != EditorGrid::bar)
        drawLines (gridBeats(), 0.025f);

    drawLines (1.0, 0.04f);

    // Bars can differ in length: the tempo map knows where each one starts.
    if (tempo.shortestBar() * pixelsPerBeat >= 6.0)
    {
        g.setColour (theme().arrangement.tintLight.withAlpha (0.1f));
        tempo.forEachBar (regionStart + scrollBeats, regionStart + scrollBeats + visibleBeats(), 1, [&] (int, double barBeat)
        {
            g.fillRect (beatToX (barBeat - regionStart), area.getY(), 1.0f, area.getHeight());
        });
    }

    // Past the end of the region (or of the loop): shaded.
    const float endX = beatToX (region->getContentLengthBeats());

    if (endX < area.getRight())
    {
        g.setColour (theme().arrangement.tintDark.withAlpha (0.35f));
        g.fillRect (area.withLeft (endX));
    }

    if (playheadBeat >= 0.0)
    {
        g.setColour (theme().arrangement.playhead);
        g.fillRect (beatToX (playheadBeat) - 1.0f, area.getY(), 2.0f, area.getHeight());
    }
}

void PianoRoll::paintNotes (juce::Graphics& g)
{
    juce::Graphics::ScopedSaveState save (g);
    g.reduceClipRegion (gridArea);

    auto& notes = notesToDraw();
    const float corner = juce::jmin (4.0f, rowHeight * 0.3f);

    for (int i = 0; i < (int) notes.size(); ++i)
    {
        auto& n = notes[(size_t) i];
        auto bounds = noteBounds (n);

        if (! bounds.intersects (gridArea.toFloat()))
            continue;

        const bool sel = isSelected (i);
        auto fill = trackColour().withMultipliedAlpha (0.55f + 0.45f * (float) n.velocity / 127.0f);
        g.setColour (sel ? fill.brighter (0.35f) : fill);
        g.fillRoundedRectangle (bounds, corner);

        if (sel)
        {
            g.setColour (theme().arrangement.tintLight);
            g.drawRoundedRectangle (bounds.reduced (0.5f), corner, 1.5f);
        }
        else
        {
            g.setColour (theme().arrangement.tintDark.withAlpha (0.35f));
            g.drawRoundedRectangle (bounds, corner, 1.0f);
        }
    }

    // The note being drawn.
    if (gesture.mode == Gesture::Mode::draw)
    {
        auto bounds = noteBounds (gesture.newNote);
        g.setColour (trackColour().withAlpha (0.6f));
        g.fillRoundedRectangle (bounds, corner);
        g.setColour (theme().arrangement.tintLight);
        g.drawRoundedRectangle (bounds.reduced (0.5f), corner, 1.5f);
    }

    if (gesture.mode == Gesture::Mode::marquee)
    {
        g.setColour (theme().arrangement.highlight.withAlpha (0.08f));
        g.fillRect (gesture.marquee);
        g.setColour (theme().arrangement.highlight);
        g.drawRect (gesture.marquee, 1);
    }
}

void PianoRoll::paintVelocities (juce::Graphics& g)
{
    g.setColour (theme().arrangement.strip);
    g.fillRect (velocityLabelArea.getUnion (velocityArea));
    g.setColour (theme().arrangement.laneDivider);
    g.fillRect (velocityLabelArea.getUnion (velocityArea).removeFromTop (1));

    g.setColour (theme().arrangement.textSecondary);
    g.setFont (fonts::ui (textSize::tiny));
    g.drawText ("Vel.", velocityLabelArea.reduced (4, 0), juce::Justification::centred);

    juce::Graphics::ScopedSaveState save (g);
    g.reduceClipRegion (velocityArea);

    // A stem per note: taller = louder.
    const auto lane = velocityArea.reduced (0, 4).toFloat();
    auto& notes = notesToDraw();

    for (int i = 0; i < (int) notes.size(); ++i)
    {
        auto& n = notes[(size_t) i];
        const float x = beatToX (n.startBeat);

        if (x < lane.getX() - 4.0f || x > lane.getRight() + 4.0f)
            continue;

        const float top = lane.getBottom() - lane.getHeight() * (float) n.velocity / 127.0f;
        const bool sel = isSelected (i);
        g.setColour (sel ? trackColour().brighter (0.4f) : trackColour().withAlpha (0.7f));
        g.fillRect (x, top, 2.0f, lane.getBottom() - top);
        g.fillEllipse (x - 2.5f, top - 2.5f, 7.0f, 7.0f);
    }
}

} // namespace ikaros
