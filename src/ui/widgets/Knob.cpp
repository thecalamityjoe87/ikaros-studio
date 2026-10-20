#include "ui/widgets/Knob.h"
#include "ui/theme/Fonts.h"
#include "ui/theme/IkarosLookAndFeel.h"
#include "ui/theme/Tokens.h"

namespace ikaros
{

using namespace tokens;

Knob::Knob (const juce::String& name)
{
    // Angles are radians clockwise from 12 o'clock: a 270° sweep from
    // 7:30 (1.25π) to 4:30 (2.75π). `true` stops the knob at the ends.
    slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f,
                                juce::MathConstants<float>::pi * 2.75f, true);
    slider.setRange (0.0, 1.0);
    slider.setMouseDragSensitivity (180);
    slider.setDoubleClickReturnValue (false, 0.0);

    // What screen readers announce as the value.
    slider.textFromValueFunction = [this] (double) { return valueText; };

    slider.onDragStart = [this] { if (onDragStart) onDragStart(); };
    slider.onValueChange = [this]
    {
        if (onValueChange)
            onValueChange ((float) slider.getValue());
    };

    addAndMakeVisible (slider);
    setName (name);
}

void Knob::setName (const juce::String& name)
{
    // The Sound tab sets every knob's name on each refresh (many a second
    // while automation plays): only repaint when it's a new one.
    if (name == label && name == getName())
        return;

    juce::Component::setName (name);
    label = name;
    slider.setTitle (name);
    repaint();
}

void Knob::setValue (float normalised, const juce::String& text)
{
    if (! slider.isMouseButtonDown())
        slider.setValue (normalised, juce::dontSendNotification);

    if (text != valueText)
    {
        valueText = text;
        slider.setTooltip (label + ": " + text);
        repaint();
    }
}

void Knob::setArcFamily (Family f)
{
    IkarosLookAndFeel::setFamilyColour (slider, f, IkarosLookAndFeel::FamilyShade::knobArc);
}

void Knob::paint (juce::Graphics& g)
{
    // A card, with a border in light mode (transparent in dark). Its shadow
    // falls outside the knob's bounds, so the parent draws it: see drawShadow.
    const auto card = getLocalBounds().toFloat();
    g.setColour (theme().smartCardBg);
    g.fillRoundedRectangle (card, size::radiusCard);
    g.setColour (theme().smartCardBorder);
    g.drawRoundedRectangle (card.reduced (0.5f), size::radiusCard, 1.0f);

    auto r = textArea;

    if (wide)
    {
        // Name and value beside the dial, centred vertically.
        r = r.withSizeKeepingCentre (r.getWidth(), 40);
        g.setColour (theme().text);
        g.setFont (fonts::uiBold (textSize::xl));
        g.drawText (label, r.removeFromTop (22), juce::Justification::centredLeft, true);

        g.setColour (theme().textSecondary);
        g.setFont (fonts::mono (textSize::lg));
        g.drawText (valueText, r, juce::Justification::centredLeft, true);
        return;
    }

    g.setColour (theme().text);
    g.setFont (fonts::uiBold (textSize::sm));
    g.drawText (label, r.removeFromTop (16), juce::Justification::centred, true);

    g.setColour (theme().textSecondary);
    g.setFont (fonts::mono (textSize::xs));
    g.drawText (valueText, r.removeFromTop (16), juce::Justification::centred, true);
}

void Knob::drawShadow (juce::Graphics& g, const Knob& knob)
{
    // A component can only paint inside its own bounds, so a shadow around
    // it has to come from its parent's paint(), which runs before the
    // children paint on top.
    juce::DropShadow (theme().smartCardShadow, 6, { 0, 1 }).drawForRectangle (g, knob.getBounds());
}

void Knob::resized()
{
    // Two ways to lay out the tile; use whichever gives the bigger dial.
    // Stacked: dial at the top, then the name and value (16px each).
    // Wide: dial on the left as tall as the tile, name and value to its right
    // (they need about 70px).
    const auto bounds = getLocalBounds();
    const int stackedDial = juce::jmin (bounds.getWidth() - 20, bounds.getHeight() - 52);
    const int wideDial = juce::jmin (bounds.getHeight() - 16, bounds.getWidth() - 24 - 12 - 70, 120);
    wide = wideDial > stackedDial;

    if (wide)
    {
        auto r = bounds.reduced (12, 8);
        slider.setBounds (r.removeFromLeft (wideDial).withSizeKeepingCentre (wideDial, wideDial));
        r.removeFromLeft (12);
        textArea = r;
        return;
    }

    auto r = bounds;
    r.removeFromTop (8);
    const int dial = juce::jmax (24, stackedDial);
    slider.setBounds (r.removeFromTop (dial).withSizeKeepingCentre (dial, dial));
    r.removeFromTop (4);
    textArea = r;
}

} // namespace ikaros
