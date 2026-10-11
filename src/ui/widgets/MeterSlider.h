#pragma once

#include "ui/widgets/LevelMeter.h"

namespace ikaros
{

//==============================================================================
/**
    A volume slider whose track is a stereo level meter, like GarageBand's
    master volume: the left and right levels light up along the slider, and
    the round handle sits on top of them.

    It's a juce::Slider (horizontal, 0..1), so dragging, double-click to
    reset, the keyboard and accessibility all work as usual; only the
    drawing is its own. The owner feeds it levels from its ~30 Hz timer.
*/
class MeterSlider  : public juce::Slider
{
public:
    MeterSlider();

    void setLevel (StereoLevel);

    void paint (juce::Graphics&) override;

private:
    float left = 0.0f, right = 0.0f;    // displayed levels, 0..1 along the bars

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MeterSlider)
};

} // namespace ikaros
