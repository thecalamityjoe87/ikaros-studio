#pragma once

#include "ui/widgets/ClipLight.h"
#include "ui/widgets/IconButton.h"
#include "ui/widgets/MeterSlider.h"
#include "ui/state/Selection.h"

namespace ikaros
{

//==============================================================================
/**
    One row in the track headers column (320 × 118): colour edge, icon tile,
    name, Mute/Solo/(Monitor)/Arm, the sound name, a volume slider (with the
    track's level meter in it) and a pan knob, and next to Mute a button that shows the track's automation.
    While it shows, a picker under the volume row says which setting's
    automation is drawn over the track's lane. The Monitor button only shows on audio tracks: instrument tracks
    always play what you play. Audio tracks also have a Flex button, at the
    end of the sound row: Flex Time, for dragging the hits in their recordings.

    Right-click shows the track menu (via onShowMenu); double-click the name
    to rename the track; click the icon tile to pick a different icon.
*/
class TrackHeader  : public juce::Component
{
public:
    TrackHeader (AudioEngine&, Selection&, const TrackInfo&);

    void update (const TrackInfo&, bool isSelected);
    TrackId getTrackId() const noexcept     { return info.id; }
    juce::Component& getArmButton() noexcept   { return armButton; }

    /** Updates the meter in the volume slider (the ~30 Hz timer calls this):
        the track's level, or while it's armed to record audio, its input's
        level, with a clip light beside it. */
    void updateMeter();

    /** Shows the rename field over the name. */
    void startRenaming();

    /** Right-click: the arrangement shows the track menu. */
    std::function<void()> onShowMenu;

    /** The "Track Icon" menu: Automatic, then every icon to choose from.
        Shared with the arrangement's track menu. */
    static juce::PopupMenu createIconMenu (AudioEngine&, TrackId);

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;

private:
    AudioEngine& engine;
    Selection& selection;
    TrackInfo info;
    bool selected = false;

    IconButton muteButton { "Mute", Icon::mute };
    IconButton soloButton { "Solo", Icon::headphones };          // headphones = solo, as in GarageBand
    IconButton monitorButton { "Monitor input", Icon::waveform };
    IconButton armButton  { "Arm for recording", Icon::record };
    IconButton automationButton { "Show automation", Icon::automation };
    IconButton flexButton { "Flex", std::nullopt, "Flex" };   // audio tracks only
    bool flexOn = false;
    MeterSlider volume;   // the track's level and volume in one, like the master's
    ClipLight clipLight;  // armed audio tracks: the input has clipped
    juce::Slider pan { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox };
    juce::TextEditor nameEditor;
    bool renaming = false;

    juce::Rectangle<int> iconArea, nameArea, soundArea, pickerArea;

    void showIconMenu();
    void showAutomationMenu();

    // While the automation shows: the setting it's for (see showAutomationMenu).
    juce::String automationName;
    bool automationTargetAutomated = false;

    void updateAccessibleNames();
    void finishRenaming (bool keepNewName);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TrackHeader)
};

} // namespace ikaros
