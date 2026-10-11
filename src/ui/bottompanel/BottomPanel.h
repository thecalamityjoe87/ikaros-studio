#pragma once

#include "ui/widgets/IconButton.h"
#include "ui/bottompanel/MixerView.h"
#include "ui/bottompanel/PianoRoll.h"
#include "ui/bottompanel/PluginEditor.h"
#include "ui/bottompanel/TrackInspector.h"
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

    The track inspector (Plug-ins) sits on the left of all of them. Clicking an
    effect there shows its settings in the main area until "Done".

    The panel can be collapsed to a slim strip showing the selected track, so
    the arrangement gets the room. Clicking the strip opens it again.
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

    /** Collapsed: only a slim strip shows (collapsedHeight tall). */
    void setCollapsed (bool);
    bool isCollapsed() const noexcept           { return collapsed; }

    /** Re-reads the selected track/region from the engine (model or selection changed). */
    void refresh();

    /** Re-reads only what these session changes affect: a knob turned on
        the Mixer doesn't need the piano roll re-read, and a note dragged
        doesn't need the track's effects re-read. */
    void refresh (const ModelChanges&);

    /** Meters and the piano-roll playhead. Called by the owner's ~30 Hz timer. */
    void update();

    /** Shows an effect's settings in the main area (instead of the current tab). */
    void openEffect (PluginId);
    /** Back to the current tab. */
    void closeEffect();
    bool isShowingEffect() const noexcept       { return effectEditor != nullptr; }

    /** Called when the user changes tab or details visibility here, so the
        owner can refresh menu tick marks. */
    std::function<void()> onViewStateChanged;

    /** The collapse button was clicked, or the collapsed strip was. The owner
        decides the layout, so it calls setCollapsed(). */
    std::function<void (bool shouldCollapse)> onCollapseRequested;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;

    static constexpr int preferredHeight = 300;   // also the smallest height the Sound tab fits in
    static constexpr int collapsedHeight = 32;    // the slim strip: track name and the show button

private:
    class SoundPage;

    AudioEngine& engine;
    Selection& selection;
    Tab tab = Tab::sound;
    bool detailsVisible = false;
    bool collapsed = false;

    IconButton soundTab  { "Sound", std::nullopt, "Sound" };
    IconButton editorTab { "Editor", std::nullopt, "Editor" };
    IconButton mixerTab  { "Mixer", std::nullopt, "Mixer" };
    IconButton detailsButton { "Show details", std::nullopt, "Show details" };
    IconButton collapseButton { "Hide panel", Icon::chevronDown };

    TrackInspector inspector;
    std::unique_ptr<SoundPage> soundPage;
    PianoRoll pianoRoll { engine };
    MixerView mixer;

    // Effect settings, shown in place of the tab pages while open.
    std::unique_ptr<PluginEditor> effectEditor;
    juce::Viewport effectViewport;

    std::optional<TrackInfo> track;
    juce::Rectangle<int> headerArea, tabsArea, contentArea;

    void refreshTrack();     // the inspector, Sound tab and Mixer
    void refreshEditor();    // the piano roll
    void updateTabButtons();
    void showTabPage();
    void layoutEffectEditor();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BottomPanel)
};

} // namespace ikaros
