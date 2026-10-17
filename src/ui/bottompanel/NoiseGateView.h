#pragma once

#include "ui/state/Selection.h"
#include "ui/widgets/IconButton.h"

namespace ikaros
{

//==============================================================================
/**
    The "Noise Gate" section of the track inspector (audio tracks only): an
    on/off switch and a threshold slider, like GarageBand's.

    It's a shortcut to the first Noise Gate effect in the track's chain (see
    EffectsController::getNoiseGate). Switching it on adds one at the top of
    the chain if there isn't one yet; switching it off bypasses it. The same
    effect shows in the Plug-ins list, where its Release knob is too.
*/
class NoiseGateView  : public juce::Component
{
public:
    NoiseGateView (AudioEngine&, Selection&);

    void refresh();
    int getIdealHeight() const;

    /** Only audio tracks have the section (it's for microphones and guitars). */
    bool appliesToSelection() const noexcept    { return isAudio; }

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    AudioEngine& engine;
    Selection& selection;
    TrackId trackId = 0;
    bool isAudio = false;
    bool dragging = false;      // the threshold slider is being dragged
    juce::String thresholdText;

    IconButton onButton { "Noise gate", std::nullopt, "Noise gate off" };
    juce::Slider threshold { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };
    juce::Rectangle<int> thresholdRow, valueArea, hintArea;

    static constexpr int sidePadding = 12;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NoiseGateView)
};

} // namespace ikaros
