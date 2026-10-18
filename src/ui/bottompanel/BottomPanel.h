#pragma once

#include "ui/widgets/IconButton.h"
#include "ui/bottompanel/AudioEditor.h"
#include "ui/bottompanel/EqView.h"
#include "ui/bottompanel/MixerView.h"
#include "ui/bottompanel/PianoRoll.h"
#include "ui/bottompanel/PluginEditor.h"
#include "ui/bottompanel/RegionInspector.h"
#include "ui/bottompanel/TrackInspector.h"
#include "ui/state/Selection.h"

namespace ikaros
{

//==============================================================================
/**
    The panel under the arrangement:

        ============ grip ============   (drag to resize)
        [Sound | Editor | EQ | Mixer] | (dot) Track name          [Inspector] [v]
        [Track | Master]  region name
        (inspector)                      tab content

    One switch picks what the panel shows:

      Sound:  the track's knobs and on-screen keyboard (or the master's
              volume and meter)
      Editor: the selected region, close up: the piano roll for a MIDI
              region, the audio editor for an audio region (tracks only)
      EQ:     a 4-band EQ
      Mixer:  every track's fader at once (and the master's, fixed on the
              right), across the whole panel

    The inspector column on the left (the Inspector button shows and hides
    it) has a small switch of its own at the top. Track | Master picks what
    the panel works on, like GarageBand: the selected track, or the master
    channel (the whole mix). On the Editor tab it's Track | Region instead:
    the region in the Editor (its gain, pitch, reverse, the Editor's grid;
    see RegionInspector), or its track. Picking a region switches to Region.
    Below the switch: the track's Recording settings and Plug-ins (or the
    master's effects). Clicking an effect there shows its settings in the
    main area until "Done".

    Collapsed, only a slim strip with the grip and a show button is left, so
    the arrangement gets the room. The owner (MainComponent) lays a
    ResizeHandle over getHandleBounds() and does the resizing.
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

    /** On the Editor tab: the left column shows the region in the Editor
        (true) or its track (false). Picking a region turns it on. */
    void setRegionMode (bool);
    bool isRegionMode() const noexcept          { return regionMode; }

    /** The full mixer: every track's fader, across the whole panel. */
    void setMixerShown (bool);
    bool isMixerShown() const noexcept          { return mixerShown; }

    /** The inspector column on the left (the Inspector button). */
    void setInspectorVisible (bool);
    bool isInspectorVisible() const noexcept    { return inspectorVisible; }

    /** Collapsed: only a slim strip shows (collapsedHeight tall). */
    void setCollapsed (bool);
    bool isCollapsed() const noexcept           { return collapsed; }

    /** Re-reads the selected track/region from the engine (model or selection changed). */
    void refresh();

    /** Re-reads only what these session changes affect: a knob turned on
        the Mixer doesn't need the piano roll re-read, and a note dragged
        doesn't need the track's effects re-read. */
    void refresh (const ModelChanges&);

    /** Meters and the Editor's playhead. Called by the owner's ~30 Hz timer. */
    void update();

    /** Shows an effect's settings in the main area (instead of the current tab). */
    void openEffect (PluginId);
    /** Back to the current tab. */
    void closeEffect();
    bool isShowingEffect() const noexcept       { return effectEditor != nullptr; }

    /** Called when the user changes tab or inspector visibility here, so the
        owner can refresh menu tick marks. */
    std::function<void()> onViewStateChanged;

    /** The collapse (or show) button was clicked. The owner decides the
        layout, so it calls setCollapsed(). */
    std::function<void (bool shouldCollapse)> onCollapseRequested;

    /** The Sound tab's Download button, on a track whose sound pack isn't
        installed. The owner opens the More Sounds window. */
    std::function<void (const juce::String& packId)> onDownloadPackRequested;

    /** Where the owner puts the resize handle: the grip along the top edge,
        or (collapsed) the whole strip apart from the show button. */
    juce::Rectangle<int> getHandleBounds() const;

    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int defaultHeight = tokens::size::bottomPanelHeight;
    static constexpr int minHeight = 180;        // the owner keeps it between this and half the window
    static constexpr int handleHeight = 8;       // the grip strip along the top edge
    static constexpr int collapsedHeight = 16;   // collapsed: just the grip and the show button

private:
    class SoundPage;
    class MasterPage;

    AudioEngine& engine;
    Selection& selection;
    Tab tab = Tab::sound;
    bool inspectorVisible = true;
    bool collapsed = false;
    bool masterMode = false;
    bool regionMode = true;
    bool inspectorShowsMaster = false;   // what TrackInspector was last set to (see showTabPage)
    RegionId lastSelectedRegion = 0;     // to notice a new region being picked (see refreshEditor)
    bool mixerShown = false;

    // The header's switch: Sound | Editor | EQ | Mixer.
    IconButton soundTab  { "Sound", std::nullopt, "Sound" };
    IconButton editorTab { "Editor", std::nullopt, "Editor" };
    IconButton eqTab     { "EQ", std::nullopt, "EQ" };
    IconButton mixerButton { "Mixer", std::nullopt, "Mixer" };
    IconButton inspectorButton { "Inspector", std::nullopt, "Inspector" };
    IconButton collapseButton { "Hide panel", Icon::chevronDown };

    // The inspector column's switch: Track | Master, or Track | Region on the Editor tab.
    IconButton trackModeButton  { "Track", std::nullopt, "Track" };
    IconButton masterModeButton { "Master", std::nullopt, "Master" };
    IconButton regionModeButton { "Region", std::nullopt, "Region" };   // in Master's place on the Editor tab

    TrackInspector inspector;
    RegionInspector regionInspector { engine };
    std::unique_ptr<SoundPage> soundPage;
    std::unique_ptr<MasterPage> masterPage;
    EqView eqView { engine };
    PianoRoll pianoRoll { engine };
    AudioEditor audioEditor { engine };
    MixerView mixer;

    // Effect settings, shown in place of the tab pages while open.
    std::unique_ptr<PluginEditor> effectEditor;
    juce::Viewport effectViewport;

    std::optional<TrackInfo> track;
    std::optional<RegionInfo> editorRegion;   // the region the Editor shows
    bool editingAudio = false;   // the Editor tab shows the audio editor (else the piano roll)
    juce::Rectangle<int> titleArea;       // in the header: the selected track's dot and name
    juce::Rectangle<int> tabsArea;        // the header's switch
    juce::Rectangle<int> headerDivider;   // between the switch and the title
    juce::Rectangle<int> switchRow;       // the top of the inspector column: its switch...
    juce::Rectangle<int> modeArea;        // ...(Track | Master or Track | Region)...
    juce::Rectangle<int> regionNameArea;  // ...and, in Region mode, the region's name
    juce::Rectangle<int> contentArea;

    /** What the panel works on: the selected track, or masterTrackId. */
    TrackId getTarget() const;

    /** Master mode, and a tab it applies to (the Editor is always about a track's regions). */
    bool showsMaster() const noexcept           { return masterMode && tab != Tab::editor; }

    /** The inspector column is showing (the full mixer takes the whole width). */
    bool showsInspectorColumn() const noexcept  { return inspectorVisible && ! mixerShown && ! collapsed; }

    /** The left column shows the region inspector rather than the track's. */
    bool showsRegionInspector() const noexcept  { return tab == Tab::editor && regionMode && ! mixerShown && ! isShowingEffect(); }

    void refreshTrack();     // the inspector, Sound tab and Mixer
    void refreshEditor();    // the piano roll or audio editor
    void updateTabButtons();
    void showTabPage();
    void layoutEffectEditor();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BottomPanel)
};

} // namespace ikaros
