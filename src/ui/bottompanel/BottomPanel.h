#pragma once

#include "ui/widgets/IconButton.h"
#include "ui/bottompanel/EqView.h"
#include "ui/bottompanel/MixerView.h"
#include "ui/bottompanel/PianoRoll.h"
#include "ui/bottompanel/PluginEditor.h"
#include "ui/bottompanel/TrackInspector.h"
#include "ui/state/Selection.h"

namespace ikaros
{

//==============================================================================
/**
    The panel under the arrangement:

        [Track | Master]        [Sound | Editor | EQ]  [Mixer]        [Show details] [v]
         name of the track
         (inspector)            tab content

    Track | Master picks what the panel works on, like GarageBand: the
    selected track, or the master channel (the whole mix). The tabs show its
    controls:

      Sound:  the track's 5 knobs and on-screen keyboard (or the master's
              volume and meter)
      Editor: piano roll for the selected MIDI region (tracks only)
      EQ:     a 4-band EQ

    The inspector on the left shows the track's Recording settings and
    Plug-ins (or the master's effects). Clicking an effect there shows its
    settings in the main area until "Done". "Show details" reveals advanced
    info (plugin, output, MIDI input).

    The Mixer button is a view of its own: every track's fader at once (and
    the master's, fixed on the right) across the whole panel, until it's
    clicked again or a tab, Track or Master is picked.

    The panel can be collapsed to a slim strip showing the selected track, so
    the arrangement gets the room. Clicking the strip opens it again.
*/
class BottomPanel  : public juce::Component
{
public:
    enum class Tab { sound, editor, eq };

    BottomPanel (AudioEngine&, Selection&);
    ~BottomPanel() override;

    /** Shows a tab (leaving the full mixer, if it's showing). */
    void setTab (Tab);
    Tab getTab() const noexcept                 { return tab; }

    /** Master mode: the panel works on the master channel instead of the selected track. */
    void setMasterMode (bool);
    bool isMasterMode() const noexcept          { return masterMode; }

    /** The full mixer: every track's fader, across the whole panel. */
    void setMixerShown (bool);
    bool isMixerShown() const noexcept          { return mixerShown; }

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
    class MasterPage;

    AudioEngine& engine;
    Selection& selection;
    Tab tab = Tab::sound;
    bool detailsVisible = false;
    bool collapsed = false;
    bool masterMode = false;
    bool mixerShown = false;

    IconButton trackModeButton  { "Track", std::nullopt, "Track" };
    IconButton masterModeButton { "Master", std::nullopt, "Master" };
    IconButton soundTab  { "Sound", std::nullopt, "Sound" };
    IconButton eqTab     { "EQ", std::nullopt, "EQ" };
    IconButton editorTab { "Editor", std::nullopt, "Editor" };
    IconButton mixerButton { "Mixer", std::nullopt, "Mixer" };
    IconButton detailsButton { "Show details", std::nullopt, "Show details" };
    IconButton collapseButton { "Hide panel", Icon::chevronDown };

    TrackInspector inspector;
    std::unique_ptr<SoundPage> soundPage;
    std::unique_ptr<MasterPage> masterPage;
    EqView eqView { engine };
    PianoRoll pianoRoll { engine };
    MixerView mixer;

    // Effect settings, shown in place of the tab pages while open.
    std::unique_ptr<PluginEditor> effectEditor;
    juce::Viewport effectViewport;

    std::optional<TrackInfo> track;
    juce::Rectangle<int> headerArea, modeArea, tabsArea, contentArea;
    juce::Rectangle<int> nameStrip;   // under Track | Master: the name of what the panel works on

    /** What the panel works on: the selected track, or masterTrackId. */
    TrackId getTarget() const;

    void refreshTrack();     // the inspector, Sound tab and Mixer
    void refreshEditor();    // the piano roll
    void updateTabButtons();
    void showTabPage();
    void layoutEffectEditor();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BottomPanel)
};

} // namespace ikaros
