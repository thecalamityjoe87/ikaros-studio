#pragma once

#include "ui/widgets/IconButton.h"
#include "ui/state/Selection.h"

namespace ikaros
{

//==============================================================================
/**
    One row in the track headers column (240 × 76): colour edge, icon tile,
    name, Mute/Solo, the sound name and a volume slider.

    Right-click shows the track menu (via onShowMenu); double-click the name
    to rename the track.
*/
class TrackHeader  : public juce::Component
{
public:
    TrackHeader (AudioEngine&, Selection&, const TrackInfo&);

    void update (const TrackInfo&, bool isSelected);
    TrackId getTrackId() const noexcept     { return info.id; }

    /** Shows the rename field over the name. */
    void startRenaming();

    /** Right-click: the arrangement shows the track menu. */
    std::function<void()> onShowMenu;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

private:
    AudioEngine& engine;
    Selection& selection;
    TrackInfo info;
    bool selected = false;

    IconButton muteButton { "Mute", std::nullopt, "M" };
    IconButton soloButton { "Solo", std::nullopt, "S" };
    IconButton armButton  { "Arm for recording", Icon::record };
    juce::Slider volume { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };
    juce::TextEditor nameEditor;
    bool renaming = false;

    juce::Rectangle<int> iconArea, nameArea, soundArea;

    void updateAccessibleNames();
    void finishRenaming (bool keepNewName);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TrackHeader)
};

} // namespace ikaros
