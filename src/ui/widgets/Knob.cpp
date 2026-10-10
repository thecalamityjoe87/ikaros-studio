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

void Knob::setArcColour (juce::Colour c)
{
    slider.setColour (IkarosLookAndFeel::knobArcColourId, c);
}

void Knob::paint (juce::Graphics& g)
{
    auto r = getLocalBounds();
    g.setColour (colour::cardBg);
    g.fillRoundedRectangle (r.toFloat(), 12.0f);

    r.removeFromTop (8 + 60 + 4);

    g.setColour (colour::text);
    g.setFont (fonts::uiBold (12.0f));
    g.drawText (label, r.removeFromTop (16), juce::Justification::centred);

    g.setColour (colour::textSecondary);
    g.setFont (fonts::mono (11.0f));
    g.drawText (valueText, r.removeFromTop (16), juce::Justification::centred);
}

void Knob::resized()
{
    slider.setBounds (getLocalBounds().removeFromTop (8 + 60).withTrimmedTop (8).withSizeKeepingCentre (60, 60));
}

} // namespace ikaros
