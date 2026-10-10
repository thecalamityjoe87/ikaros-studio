#include "ui/bottompanel/PianoRoll.h"
#include "ui/theme/Fonts.h"
#include "ui/theme/Tokens.h"

namespace ikaros
{

using namespace tokens;

namespace
{
    bool isBlackKey (int note)
    {
        const int n = note % 12;
        return n == 1 || n == 3 || n == 6 || n == 8 || n == 10;
    }
}

PianoRoll::PianoRoll()
{
    setTitle ("Piano roll");
}

void PianoRoll::setRegion (std::optional<RegionInfo> r, juce::Colour c, double bpb)
{
    region = std::move (r);
    colour = c;
    beatsPerBar = bpb;

    // Show the notes' range plus a little space, at least one octave.
    lowNote = 60;
    highNote = 71;

    if (region.has_value() && ! region->notes.empty())
    {
        int lo = 127, hi = 0;

        for (auto& n : region->notes)
        {
            lo = juce::jmin (lo, n.pitch);
            hi = juce::jmax (hi, n.pitch);
        }

        lowNote = juce::jmax (0, lo - 2);
        highNote = juce::jmin (127, juce::jmax (hi + 2, lowNote + 11));
    }

    repaint();
}

void PianoRoll::setPlayheadBeat (double editBeat)
{
    double rel = -1.0;

    if (region.has_value() && editBeat >= region->startBeat && editBeat < region->startBeat + region->lengthBeats)
        rel = editBeat - region->startBeat;

    if (rel != playheadBeat)
    {
        playheadBeat = rel;
        repaint();
    }
}

void PianoRoll::paint (juce::Graphics& g)
{
    auto r = getLocalBounds();
    g.fillAll (colour::windowBg);

    if (! region.has_value() || ! region->isMidi)
    {
        g.setColour (colour::textSecondary);
        g.setFont (fonts::ui (14.0f));
        g.drawText ("Select a MIDI region to edit it here. Double-click an empty lane to create one.",
                    r, juce::Justification::centred);
        return;
    }

    const int numRows = highNote - lowNote + 1;
    const float rowH = (float) r.getHeight() / (float) numRows;
    auto keys = r.removeFromLeft (keysWidth);
    auto grid = r.toFloat();

    // Keys column
    g.setColour (colour::sidebarBg);
    g.fillRect (keys);

    for (int note = lowNote; note <= highNote; ++note)
    {
        auto y = (float) (highNote - note) * rowH;
        auto row = juce::Rectangle<float> ((float) keys.getX(), y, (float) keys.getWidth(), rowH);
        g.setColour (isBlackKey (note) ? colour::wellBg : juce::Colour (0xffc9d0da));
        g.fillRect (row.reduced (0.0f, 0.5f));

        if (note % 12 == 0)
        {
            g.setColour (colour::pianoLabel);
            g.setFont (fonts::ui (9.0f));
            g.drawText ("C" + juce::String (note / 12 - 1), row.withTrimmedRight (3.0f), juce::Justification::centredRight);
        }

        // Row stripes in the grid: darker for black keys.
        g.setColour (isBlackKey (note) ? juce::Colours::black.withAlpha (0.18f) : juce::Colours::transparentBlack);
        g.fillRect (grid.getX(), y, grid.getWidth(), rowH);
        g.setColour (juce::Colours::white.withAlpha (0.035f));
        g.fillRect (grid.getX(), y + rowH - 1.0f, grid.getWidth(), 1.0f);
    }

    // Beat and bar lines
    const double length = juce::jmax (1.0, region->lengthBeats);
    const float pxPerBeat = grid.getWidth() / (float) length;

    for (int beat = 0; beat <= (int) std::ceil (length); ++beat)
    {
        const bool isBar = std::fmod ((double) beat, beatsPerBar) == 0.0;
        g.setColour (juce::Colours::white.withAlpha (isBar ? 0.1f : 0.04f));
        g.fillRect (grid.getX() + (float) beat * pxPerBeat, grid.getY(), 1.0f, grid.getHeight());
    }

    // Notes
    for (auto& n : region->notes)
    {
        if (n.pitch < lowNote || n.pitch > highNote)
            continue;

        auto note = juce::Rectangle<float> (grid.getX() + (float) n.startBeat * pxPerBeat,
                                            (float) (highNote - n.pitch) * rowH + 1.0f,
                                            juce::jmax (3.0f, (float) n.lengthBeats * pxPerBeat - 1.0f),
                                            rowH - 2.0f);
        g.setColour (colour.withMultipliedAlpha (0.55f + 0.45f * (float) n.velocity / 127.0f));
        g.fillRoundedRectangle (note, 4.0f);
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.drawRoundedRectangle (note, 4.0f, 1.0f);
    }

    if (playheadBeat >= 0.0)
    {
        g.setColour (colour::accent);
        g.fillRect (grid.getX() + (float) playheadBeat * pxPerBeat - 1.0f, grid.getY(), 2.0f, grid.getHeight());
    }
}

} // namespace ikaros
