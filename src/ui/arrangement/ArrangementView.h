#pragma once

#include "ui/arrangement/RegionComponent.h"
#include "engine/TempoMap.h"
#include "ui/state/Selection.h"
#include "ui/arrangement/TrackHeader.h"
#include "ui/widgets/IconButton.h"
#include "ui/theme/Tokens.h"
#include <map>
#include <optional>

namespace ikaros
{

//==============================================================================
/**
    The main timeline: bar ruler (with loop range), track headers, lanes with
    regions, the dashed drop zone, and the playhead.

    Layout:
        ┌─────────── 320 ───────────┬──────────── lanes ────────────┐
        │ TRACKS [Snap] ⟋ ◷     [+] │ ruler  1   2   3   4 ...      │  30px
        │ TEMPO                     │ tempo lane (when shown) ▔▔╲▁▁ │  44px
        │ SECTIONS              [⚑] │ [Intro   ][Verse      ][Cho.. │  24px
        ├───────────────────────────┼───────────────────────────────┤
        │ TrackHeader               │ lane  [region] [region]       │ 84px (Tall: 100)
        │ TrackHeader               │ lane                          │ (+18 w/ automation)
        │ ┌╌╌╌╌╌ + Add track ╌╌╌╌╌┐ │ ┌ drop zone ┐                 │
        │ ⌕ ───── zoom ───○ [⊖|↺|⊕] │ ◀═══════ scroll bar ═══════▶  │  26px
        └───────────────────────────┴───────────────────────────────┘
    Headers + lanes scroll vertically together inside a Viewport. Time
    (horizontal zoom and scroll) is handled here instead: the ruler, lanes and
    playhead all draw through the same Geometry, so they always line up.

    Regions: click to select, Shift-click to add to the selection, drag to
    move (to another track of the same kind too), drag an edge to trim,
    double-click the name to rename. Audio files can be dropped on a lane or
    the drop zone.

    Each track can show its automation over its lane (the button under its
    icon, or "Show Automation" above the headers for every track): see
    AutomationLane.

    Right-click a track header for the track menu (duplicate, rename, colour,
    reorder, delete...), or an empty part of a lane for the lane menu (add or
    paste a region there, plus the track menu). Drag the playhead's head, or
    click the ruler, to move the playhead.

    The tempo lane (the clock button, or View > Show Tempo Lane) shows the
    song's tempo and time-signature changes, which happen at bar lines: see
    TempoLane. Bars can differ in length, so everything that needs a bar
    asks geometry.tempo (a TempoMap) for the one at a beat.

    The marker strip above the tracks shows the song's sections (Intro,
    Verse...). Dragging one moves its music on every track: see SectionStrip.
*/
class ArrangementView  : public juce::Component,
                         public juce::FileDragAndDropTarget,
                         public juce::TextDragAndDropTarget,
                         public juce::DragAndDropTarget,
                         private juce::ScrollBar::Listener
{
public:
    ArrangementView (AudioEngine&, Selection&, juce::ApplicationCommandManager&);
    ~ArrangementView() override;

    /** Re-reads tracks and regions from the engine. Call after model changes. */
    void refresh();

    /** Re-reads only the tracks' own settings (names, mute, volume, the
        selection), not their regions. Falls back to refresh() if tracks were
        added, removed or reordered. */
    void refreshTracks();

    /** Re-reads only these regions. Falls back to refresh() if one of them
        is new, gone, or on another track. */
    void refreshRegions (const std::set<RegionId>&);

    /** Updates the playhead and loop bar. Called by the owner's ~30 Hz timer. */
    void updateTransport();

    //==============================================================================
    /** Snap to grid: where dragged regions, edges and the playhead land. */
    enum class Snap { bar, beat, off };
    void setSnap (Snap);
    Snap getSnap() const noexcept               { return snap; }

    /** Takes the snap setting from the Preferences (on or off, and its grid). */
    void applySnapPreference();

    /** The nearest grid line to a beat (or the beat itself with snap off). */
    double snapBeat (double beat) const;

    /** The distance between grid lines at a beat, in beats (0 with snap
        off). With the bar grid it's the length of the bar there. */
    double getSnapBeats (double atBeat) const;

    /** The tempo lane under the ruler (a View preference). */
    void setTempoLaneShown (bool);
    bool isTempoLaneShown() const noexcept       { return tempoLaneShown; }

    /** View > Compact Tracks (84px rows, the default) or Tall Tracks (100px).
        Saved in the Preferences. */
    void setTracksTall (bool);
    bool areTracksTall() const noexcept          { return tracksTall; }

    void zoomIn();
    void zoomOut();

    /** Back to the starting zoom (16 bars across when the view first had
        room), where tracks are their normal height. */
    void resetZoom();

    /** The zoom and where the view is scrolled to, so a session can open
        the way it was left (see MainComponent's view state). */
    struct ViewPosition
    {
        double pixelsPerBeat = 0.0;   // 0 = the starting zoom
        double scrollBeats = 0.0;     // the beat at the left edge of the lanes
        int scrollY = 0;              // how far down the tracks are scrolled
    };

    ViewPosition getViewPosition() const;

    /** Before the view first has a size, it's kept until it does. */
    void setViewPosition (const ViewPosition&);

    /** Shows the rename field on a region (scrolling it into view first). */
    void startRenaming (RegionId);

    /** Shows the rename field on a track header (scrolling it into view first). */
    void startRenamingTrack (TrackId);

    /** A track's record-arm button (scrolling its row into view first), or
        nullptr if there's no such track. */
    juce::Component* getArmButton (TrackId);

    std::function<void (RegionId)> onOpenRegionInEditor;
    std::function<void()> onSnapChanged;                          // so the owner can update menu ticks
    std::function<void()> onTempoLaneToggled;                     // the same, for Show Tempo Lane
    std::function<void()> onAddTrack;                             // the "Add track" row under the tracks

    //==============================================================================
    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;

    // FileDragAndDropTarget: audio files dragged in from the file manager.
    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter (const juce::StringArray& files, int x, int y) override;
    void fileDragMove (const juce::StringArray& files, int x, int y) override;
    void fileDragExit (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;

    // TextDragAndDropTarget: some file managers' drags arrive as text, a list
    // of file:// links, one per line. Treated the same as a file drag.
    bool isInterestedInTextDrag (const juce::String& text) override;
    void textDragEnter (const juce::String& text, int x, int y) override;
    void textDragMove (const juce::String& text, int x, int y) override;
    void textDragExit (const juce::String& text) override;
    void textDropped (const juce::String& text, int x, int y) override;

    // DragAndDropTarget: sounds, loops and audio files dragged from the library.
    bool isInterestedInDragSource (const SourceDetails&) override;
    void itemDragEnter (const SourceDetails&) override;
    void itemDragMove (const SourceDetails&) override;
    void itemDragExit (const SourceDetails&) override;
    void itemDropped (const SourceDetails&) override;

    /** Maps between beats and x-positions within a lane. */
    struct Geometry
    {
        double pixelsPerBeat = 20.0;
        TempoMap tempo;                // where the bars are (they can differ in length)
        double scrollBeats = 0.0;      // the beat at the left edge of the lanes
        int rowHeight = tokens::size::trackRowCompact;   // track height: grows when zoomed in (see setZoom)

        float beatToX (double beat) const          { return (float) ((beat - scrollBeats) * pixelsPerBeat); }
        double xToBeat (float x) const             { return x / pixelsPerBeat + scrollBeats; }
        float beatsToWidth (double beats) const    { return (float) (beats * pixelsPerBeat); }
    };

private:
    class Ruler;
    class TempoLane;
    class SectionStrip;
    class Lane;
    class DropZone;
    class AddTrackButton;
    class Playhead;
    class Overlay;
    class Content;
    class RegionDrag;
    class AutomationLane;

    AudioEngine& engine;
    Selection& selection;
    juce::ApplicationCommandManager& commands;
    Geometry geometry;
    Snap snap = Snap::beat;

    std::unique_ptr<Ruler> ruler;
    std::unique_ptr<TempoLane> tempoLane;
    bool tempoLaneShown = false;
    bool tracksTall = false;
    std::unique_ptr<SectionStrip> sectionStrip;
    std::unique_ptr<Content> content;
    std::unique_ptr<Overlay> overlay;
    std::unique_ptr<Playhead> playhead;
    juce::Viewport viewport;
    juce::ScrollBar timeScrollBar { false };   // false = horizontal

    IconButton snapButton { "Snap to grid", std::nullopt, "Snap: Beat" };
    IconButton automationButton { "Show automation", Icon::automation };
    IconButton tempoLaneButton { "Show tempo lane", Icon::clock };
    IconButton newTrackButton { "Add track", Icon::plus };   // the tracks bar's "+", always in view
    juce::Slider zoomSlider { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };
    juce::Rectangle<int> zoomIconArea;                       // the magnifier in front of the zoom slider
    IconButton zoomOutButton { "Zoom out", Icon::zoomOut };   // the "zoom out | reset | zoom in" group beside the zoom slider
    IconButton zoomResetButton { "Reset zoom", Icon::reset };
    IconButton zoomInButton { "Zoom in", Icon::zoomIn };
    juce::Rectangle<int> zoomButtonsArea;                   // their shared outline (drawn in paint)

    std::vector<TrackInfo> tracks;
    double contentEndBeat = 0.0;
    bool zoomInitialised = false;
    std::optional<ViewPosition> pendingViewPosition;   // set before zoomInitialised: applied once it is
    double startPixelsPerBeat = 20.0;   // the first zoom (16 bars across): tracks are their normal height here and below
    float lastPlayheadX = 0.0f;

    //==============================================================================
    // Time: zoom and horizontal scroll
    int getLaneWidth() const;
    double getVisibleBeats() const;
    double getTotalBeats() const;
    void setZoom (double pixelsPerBeat, float anchorX);
    int getBaseRowHeight() const noexcept;   // a track's height when not zoomed in: compact or tall
    float getZoomAnchorX (float fallbackX) const;   // where a zoom should stay put, in lane x
    void scrollToBeat (double beat);
    void timelineChanged();
    void updateScrollBar();
    void scrollBarMoved (juce::ScrollBar*, double newRangeStart) override;
    void showSnapMenu();

    /** The tempo and time-signature items for the bar nearest a beat, shared
        by the ruler's and the tempo lane's right-click menus. */
    void addTempoMenuItems (juce::PopupMenu&, double beat);
    void updateAutomationButton();   // on while every track's automation shows

    /** Mouse wheel over the timeline: Ctrl/Cmd zooms (unless the Preferences
        say the wheel only scrolls), Shift (or a sideways
        swipe) scrolls in time. Returns false to let the wheel scroll vertically. */
    bool handleWheel (const juce::MouseEvent&, const juce::MouseWheelDetails&, bool wheelScrollsTime);

    void layoutContent();

    //==============================================================================
    // Region gestures (RegionComponent forwards its mouse events here).
    // Clicks, moves and trims are handled by RegionDrag.
    std::unique_ptr<RegionDrag> regionDrag;

    void regionMouseDown (RegionComponent&, const juce::MouseEvent&);
    void regionMouseDrag (RegionComponent&, const juce::MouseEvent&);
    void regionMouseUp (RegionComponent&, const juce::MouseEvent&);
    void regionDoubleClicked (RegionComponent&, RegionComponent::Zone);
    void showRegionMenu();
    void showTakeMenu (RegionComponent&);   // the take badge's menu

    // Context menus for tracks. They act on the selected track, so the
    // header or lane selects its track before showing them.
    void showTrackMenu();
    void showLaneMenu (TrackId, double beat);
    juce::PopupMenu createTrackMenu();

    /** Playhead dragging: moves it to an x-position in the lanes, scrolling
        the timeline when that's past either edge. */
    void dragPlayheadTo (float laneX);
    void scrollRowIntoView (int row);

    RegionComponent* findRegion (RegionId) const;
    int rowOf (TrackId) const;
    int rowAt (int y) const;   // y in this component's coordinates
    juce::Rectangle<float> rowAreaInOverlay (int row) const;

    // A row is taller while its track's automation shows (room for the
    // picker in its header), so rows can differ in height.
    int rowTop (int row) const;          // in the content's coordinates
    int rowHeightOf (int row) const;
    int getTracksHeight() const;         // all the rows

    // Audio file drops
    struct DropTarget { TrackId track = 0; int row = -1; double beat = 0.0; };
    DropTarget findDropTarget (int x, int y) const;
    int findSoundDropRow (int y) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ArrangementView)
};

} // namespace ikaros
