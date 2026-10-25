#pragma once

#include "ui/widgets/IconButton.h"

namespace ikaros
{

//==============================================================================
/**
    The transport's metronome toggle. While the click plays, the icon's arm
    swings from side to side like a real metronome's, reaching an end on
    every click; otherwise it rests leaning right, as in the static icon.
*/
class MetronomeButton  : public IconButton
{
public:
    MetronomeButton();

    /** Where the playhead is, in beats, while the click plays; nullopt to
        rest the arm. Called on every transport refresh (~30 Hz), so it only
        repaints when the arm moves. */
    void setSwing (std::optional<double> beats);

protected:
    void paintIcon (juce::Graphics&, juce::Rectangle<float> area, juce::Colour) override;

private:
    float angle;   // radians from upright, positive leaning right

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MetronomeButton)
};

} // namespace ikaros
