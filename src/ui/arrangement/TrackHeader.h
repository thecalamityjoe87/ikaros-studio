#pragma once

#include "ui/widgets/ClipLight.h"
#include "ui/widgets/IconButton.h"
#include "ui/widgets/MeterSlider.h"
#include "ui/state/Selection.h"

namespace ikaros
{

//==============================================================================
/**
    One row in the track headers column (320 × 84, or 100 with Tall Tracks):

        ▌┌──┐ Track name                  [Flex][~] (●)
        ▌└──┘ Sound name · −3.0 dB
        ▌     [M][S][⟋] ───────○──────────────  (◔)

    A 3px edge in the track colour, an icon tile, then three rows: the name
    with Flex and Monitor (audio tracks only: instrument tracks always play
    what you play) and the record-arm button; the sound (or the input, for
    audio tracks) and the volume in dB; and Mute, Solo, Automation, the
    volume slider (with the track's level meter in it) and the pan knob.
    The pan knob sits right under the arm button, in one column.
    Flex is Flex Time, for dragging the hits in a track's recordings.

    While the track's automation shows, a picker under the bottom row says
    which setting's automation is drawn over the track's lane.

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
    void parentHierarchyChanged() override;

private:
    AudioEngine& engine;
    Selection& selection;
    TrackInfo info;
    bool selected = false;

    IconButton muteButton { "Mute", Icon::speaker };             // a crossed-out speaker while muted
    IconButton soloButton { "Solo", Icon::headphones };          // headphones = solo, as in GarageBand
    IconButton monitorButton { "Input monitoring", Icon::waveform };
    IconButton armButton  { "Record arm", Icon::record };
    IconButton automationButton { "Automation", Icon::automation };
    IconButton flexButton { "Flex Time", std::nullopt, "Flex" };   // audio tracks only
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
