#pragma once

#include "ui/widgets/IconButton.h"
#include "ui/bottompanel/MixerView.h"
#include "ui/bottompanel/PianoRoll.h"
#include "ui/state/Selection.h"

namespace ikaros
{

//==============================================================================
/**
    The panel under the arrangement. A header shows the selected track, a
    segmented control switches between Sound | Editor | Mixer, and
    "Show details" reveals advanced info (plugin, output, MIDI input).

      Sound:  5 knobs, Effects list (ON/OFF), 3-octave on-screen keyboard
      Editor: piano roll for the selected MIDI region
      Mixer:  channel strips with meters and faders
*/
class BottomPanel  : public juce::Component
{
public:
    enum class Tab { sound, editor, mixer };

    BottomPanel (AudioEngine&, Selection&);
    ~BottomPanel() override;

    void setTab (Tab);
    Tab getTab() const noexcept                 { return tab; }

    void setDetailsVisible (bool);
    bool areDetailsVisible() const noexcept     { return detailsVisible; }

    /** Re-reads the selected track/region from the engine (model or selection changed). */
    void refresh();

    /** Meters and the piano-roll playhead. Called by the owner's ~30 Hz timer. */
    void update();

    /** Called when the user changes tab or details visibility here, so the
        owner can refresh menu tick marks. */
    std::function<void()> onViewStateChanged;

    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int preferredHeight = 300;

private:
    class SoundPage;

    AudioEngine& engine;
    Selection& selection;
    Tab tab = Tab::sound;
    bool detailsVisible = false;

    IconButton soundTab  { "Sound", std::nullopt, "Sound" };
    IconButton editorTab { "Editor", std::nullopt, "Editor" };
    IconButton mixerTab  { "Mixer", std::nullopt, "Mixer" };
    IconButton detailsButton { "Show details", std::nullopt, "Show details" };

    std::unique_ptr<SoundPage> soundPage;
    PianoRoll pianoRoll;
    MixerView mixer;

    std::optional<TrackInfo> track;
    juce::Rectangle<int> headerArea, tabsArea;

    void updateTabButtons();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BottomPanel)
};

} // namespace ikaros
