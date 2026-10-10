#pragma once

#include "ui/widgets/IconButton.h"
#include "ui/state/Selection.h"

namespace ikaros
{

//==============================================================================
/**
    One row in the track headers column (240 × 76): colour edge, icon tile,
    name, Mute/Solo, the sound name and a volume slider.
*/
class TrackHeader  : public juce::Component
{
public:
    TrackHeader (AudioEngine&, Selection&, const TrackInfo&);

    void update (const TrackInfo&, bool isSelected);
    TrackId getTrackId() const noexcept     { return info.id; }

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    AudioEngine& engine;
    Selection& selection;
    TrackInfo info;
    bool selected = false;

    IconButton muteButton { "Mute", std::nullopt, "M" };
    IconButton soloButton { "Solo", std::nullopt, "S" };
    juce::Slider volume { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };

    juce::Rectangle<int> iconArea, nameArea, soundArea;

    void updateAccessibleNames();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TrackHeader)
};

} // namespace ikaros
