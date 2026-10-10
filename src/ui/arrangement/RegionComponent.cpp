#include "ui/arrangement/RegionComponent.h"
#include "ui/theme/Fonts.h"
#include "ui/theme/IkarosLookAndFeel.h"
#include "ui/theme/Tokens.h"

namespace ikaros
{

using namespace tokens;

namespace
{
    constexpr float corner = 8.0f;
    constexpr int stripHeight = 20;
}

RegionComponent::RegionComponent (const RegionInfo& r, juce::Colour c)
    : info (r), colour (c)
{
    setWantsKeyboardFocus (true);
    setTitle (r.name + " region");
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void RegionComponent::update (const RegionInfo& r, juce::Colour c, bool isSelected)
{
    info = r;
    colour = c;
    selected = isSelected;
    setTitle (r.name + " region");
    repaint();
}

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
    }

    g.setColour (selected ? colour : colour.withAlpha (0.4f));
    g.strokePath (shape, juce::PathStrokeType (selected ? 2.0f : 1.0f));

    if (hasKeyboardFocus (false))
        IkarosLookAndFeel::drawFocusRing (g, getLocalBounds().toFloat(), corner);
}

void RegionComponent::paintMidiNotes (juce::Graphics& g, juce::Rectangle<float> area)
{
    if (info.notes.empty() || info.lengthBeats <= 0.0)
        return;

    // Fit the region's pitch range into the available height.
    int lo = 127, hi = 0;

    for (auto& n : info.notes)
    {
        lo = juce::jmin (lo, n.pitch);
        hi = juce::jmax (hi, n.pitch);
    }

    lo -= 1;
    hi += 1;
    const float rowH = area.getHeight() / (float) (hi - lo + 1);
    const float noteH = juce::jlimit (2.0f, 4.0f, rowH);
    const float pxPerBeat = area.getWidth() / (float) info.lengthBeats;

    g.setColour (colour);

    for (auto& n : info.notes)
    {
        auto x = area.getX() + (float) n.startBeat * pxPerBeat;
        auto w = juce::jmax (2.0f, (float) n.lengthBeats * pxPerBeat - 1.0f);
        auto y = area.getY() + (float) (hi - n.pitch) * rowH + (rowH - noteH) * 0.5f;
        g.fillRoundedRectangle (x, y, w, noteH, noteH * 0.5f);
    }
}

void RegionComponent::paintWaveform (juce::Graphics& g, juce::Rectangle<float> area)
{
    // TODO(milestone-2): draw the real waveform via Tracktion's SmartThumbnail,
    // exposed through the engine layer. This is a placeholder shape.
    juce::Path wave;
    const float mid = area.getCentreY();
    const float amp = area.getHeight() * 0.45f;

    wave.startNewSubPath (area.getX(), mid);

    for (float x = 0; x <= area.getWidth(); x += 2.0f)
    {
        auto a = (0.25f + 0.75f * std::abs (std::sin (x / 4.3f) * std::sin (x / 9.1f))) * amp;
        wave.lineTo (area.getX() + x, mid - a);
    }

    for (float x = area.getWidth(); x >= 0; x -= 2.0f)
    {
        auto a = (0.25f + 0.75f * std::abs (std::sin (x / 4.3f) * std::sin (x / 9.1f))) * amp;
        wave.lineTo (area.getX() + x, mid + a);
    }

    wave.closeSubPath();
    g.setColour (colour);
    g.fillPath (wave);
}

void RegionComponent::mouseDown (const juce::MouseEvent&)
{
    grabKeyboardFocus();

    if (onSelect)
        onSelect();
}

void RegionComponent::mouseDoubleClick (const juce::MouseEvent&)
{
    if (onOpenEditor)
        onOpenEditor();
}

bool RegionComponent::keyPressed (const juce::KeyPress& key)
{
    if ((key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey) && onDelete)
    {
        onDelete();
        return true;
    }

    if (key == juce::KeyPress::returnKey && onOpenEditor)
    {
        onOpenEditor();
        return true;
    }

    return false;
}

} // namespace ikaros
