#pragma once

#include "ui/state/Selection.h"
#include "ui/widgets/IconButton.h"

namespace ikaros
{

//==============================================================================
/**
    The "Tune" section of the track inspector, on Vocal tracks: an on/off
    switch, Correction and Speed, and a button for the rest of its settings.

    Like the Noise Gate section, it's a shortcut to the first Tune effect in
    the track's chain (see EffectsController::getFirstTune). Switching it on
    adds one early in the chain if there isn't one yet; switching it off
    bypasses it. The same effect shows in the Plug-ins list.
*/
class TuneSectionView  : public juce::Component
{
public:
    TuneSectionView (AudioEngine&, Selection&);

    void refresh();
    int getIdealHeight() const;

    /** Vocal tracks have the section (and audio tracks that have a Tune already). */
    bool appliesToSelection() const noexcept    { return shown; }

    /** Settings was clicked: show the Tune's settings (its PluginId). */
    std::function<void (PluginId)> onOpenEffect;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    AudioEngine& engine;
    Selection& selection;
    TrackId trackId = 0;
    PluginId tuneId = 0;
    bool shown = false;
    juce::Slider* dragging = nullptr;   // the slider being dragged (it's ahead of the session)
    juce::String correctionText, speedText;

    IconButton onButton { "Tune", std::nullopt, "Tune off" };
    IconButton settingsButton { "Tune settings", std::nullopt, "Settings" };
    juce::Slider correction { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };
    juce::Slider speed { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };
    juce::Rectangle<int> correctionRow, correctionValue, speedRow, speedValue, hintArea;

    void setUpSlider (juce::Slider&, int parameterIndex, const juce::String& title, const juce::String& undoName, const juce::String& tip);

    static constexpr int sidePadding = 12;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TuneSectionView)
};

} // namespace ikaros
