#pragma once

#include "engine/EngineTypes.h"
#include "ui/widgets/IconButton.h"

namespace ikaros
{

//==============================================================================
/**
    The black "LCD" in the transport: BAR.BEAT, BPM, time signature and key.

    BPM is editable (click it and type, or scroll the mouse wheel over it).
    Clicking the time signature offers common choices.
*/
class LcdDisplay  : public juce::Component
{
public:
    LcdDisplay();

    void setState (const TransportState&, const juce::String& keyName);

    std::function<void (double bpm)> onBpmChanged;
    std::function<void (int numerator, int denominator)> onTimeSignatureChanged;

    /** The width this component wants, given its content. */
    static constexpr int preferredWidth = 300;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    // A Label that becomes a text field on click: the simplest editable value in JUCE.
    juce::Label bpmLabel;
    IconButton timeSigButton { "Time signature" };

    juce::String positionText { "01.1" }, keyText { "C maj" };
    double bpm = 120.0;
    int timeSigNum = 4, timeSigDen = 4;

    juce::Rectangle<int> positionArea, bpmArea, timeArea, keyArea;

    void paintCaption (juce::Graphics&, juce::Rectangle<int> area, const juce::String&);
    void showTimeSignatureMenu();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LcdDisplay)
};

} // namespace ikaros
