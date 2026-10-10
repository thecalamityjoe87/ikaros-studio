#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace ikaros
{

//==============================================================================
/**
    An arc-style knob tile: the rotary control, its name, and a mono value readout.

    The knob itself is a juce::Slider, so mouse dragging, the mouse wheel,
    arrow keys and screen-reader support all come for free; the LookAndFeel's
    drawRotarySlider() does the arc drawing.
*/
class Knob  : public juce::Component
{
public:
    explicit Knob (const juce::String& name = {});

    void setName (const juce::String&) override;
    void setValue (float normalised, const juce::String& valueText);
    void setArcColour (juce::Colour);

    std::function<void (float)> onValueChange;   // normalised 0..1
    std::function<void()> onDragStart;

    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int preferredWidth = 80;
    static constexpr int preferredHeight = 112;

private:
    juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox };
    juce::String label, valueText;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Knob)
};

} // namespace ikaros
