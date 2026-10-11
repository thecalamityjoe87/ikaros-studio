#pragma once

#include "ui/arrangement/RegionComponent.h"
#include "ui/state/Selection.h"
#include "ui/arrangement/TrackHeader.h"
#include "ui/widgets/IconButton.h"
#include <map>

namespace ikaros
{

//==============================================================================
/**
    The main timeline: bar ruler (with loop range), track headers, lanes with
    regions, the dashed drop zone, and the playhead.

    Layout:
        ┌─────────── 260 ───────────┬──────────── lanes ────────────┐
        │ TRACKS  [Snap: Beat] ⌕──○ │ ruler  1   2   3   4 ...      │  30px
        ├───────────────────────────┼───────────────────────────────┤
        │ TrackHeader               │ lane  [region] [region]       │  76px
        │ TrackHeader               │ lane                          │  each
        │                           │ ┌ drop zone ┐                 │
        │                           │ ◀═══════ scroll bar ═══════▶  │  12px
        └───────────────────────────┴───────────────────────────────┘
    Headers + lanes scroll vertically together inside a Viewport. Time
    (horizontal zoom and scroll) is handled here instead: the ruler, lanes and
    playhead all draw through the same Geometry, so they always line up.

    Regions: click to select, Shift-click to add to the selection, drag to
    move (to another track of the same kind too), drag an edge to trim,
    double-click the name to rename. Audio files can be dropped on a lane or
    the drop zone.

    Right-click a track header for the track menu (duplicate, rename, colour,
    reorder, delete...), or an empty part of a lane for the lane menu (add or
    paste a region there, plus the track menu). Drag the playhead's head, or
    click the ruler, to move the playhead.
*/
class ArrangementView  : public juce::Component,
                         public juce::FileDragAndDropTarget,
                         public juce::DragAndDropTarget,
                         private juce::ScrollBar::Listener
{
public:
    ArrangementView (AudioEngine&, Selection&, juce::ApplicationCommandManager&);
    ~ArrangementView() override;

    /** Re-reads tracks and regions from the engine. Call after model changes. */
    void refresh();

    /** Updates the playhead and loop bar. Called by the owner's ~30 Hz timer. */
    void updateTransport();

    //==============================================================================
    /** Snap to grid: where dragged regions, edges and the playhead land. */
    enum class Snap { bar, beat, off };
    void setSnap (Snap);
    Snap getSnap() const noexcept               { return snap; }

    /** The nearest grid line to a beat (or the beat itself with snap off). */
    double snapBeat (double beat) const;

    void zoomIn();
    void zoomOut();

    /** Shows the rename field on a region (scrolling it into view first). */
    void startRenaming (RegionId);

    /** Shows the rename field on a track header (scrolling it into view first). */
    void startRenamingTrack (TrackId);

    std::function<void (RegionId)> onOpenRegionInEditor;
    std::function<void (juce::Component*)> onAddTrackRequested;   // shows the track-type menu at this component
    std::function<void()> onSnapChanged;                          // so the owner can update menu ticks

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

    // DragAndDropTarget: loops and audio files dragged from the library.
    bool isInterestedInDragSource (const SourceDetails&) override;
    void itemDragEnter (const SourceDetails&) override;
    void itemDragMove (const SourceDetails&) override;
    void itemDragExit (const SourceDetails&) override;
    void itemDropped (const SourceDetails&) override;

    /** Maps between beats and x-positions within a lane. */
    struct Geometry
    {
        double pixelsPerBeat = 20.0;
        double beatsPerBar = 4.0;
        double scrollBeats = 0.0;      // the beat at the left edge of the lanes

        float beatToX (double beat) const          { return (float) ((beat - scrollBeats) * pixelsPerBeat); }
        double xToBeat (float x) const             { return x / pixelsPerBeat + scrollBeats; }
        float beatsToWidth (double beats) const    { return (float) (beats * pixelsPerBeat); }
    };

private:
    class Ruler;
    class Lane;
    class DropZone;
    class Playhead;
    class Overlay;
    class Content;
    class RegionDrag;

    AudioEngine& engine;
    Selection& selection;
    juce::ApplicationCommandManager& commands;
    Geometry geometry;
    Snap snap = Snap::beat;

    std::unique_ptr<Ruler> ruler;
    std::unique_ptr<Content> content;
    std::unique_ptr<Overlay> overlay;
    std::unique_ptr<Playhead> playhead;
    juce::Viewport viewport;
    juce::ScrollBar timeScrollBar { false };   // false = horizontal

    IconButton snapButton { "Snap to grid", std::nullopt, "Snap: Beat" };
    juce::Slider zoomSlider { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };
    juce::Rectangle<int> zoomIconArea;

    std::vector<TrackInfo> tracks;
    double contentEndBeat = 0.0;
    bool zoomInitialised = false;
    float lastPlayheadX = 0.0f;

    //==============================================================================
    // Time: zoom and horizontal scroll
    int getLaneWidth() const;
    double getVisibleBeats() const;
    double getTotalBeats() const;
    void setZoom (double pixelsPerBeat, float anchorX);
    void scrollToBeat (double beat);
    void timelineChanged();
    void updateScrollBar();
    void scrollBarMoved (juce::ScrollBar*, double newRangeStart) override;
    void showSnapMenu();

    /** Mouse wheel over the timeline: Ctrl/Cmd zooms, Shift (or a sideways
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

    // Context menus for tracks. They act on the selected track, so the
    // header or lane selects its track before showing them.
    void showTrackMenu();
    void showLaneMenu (TrackId, double beat);
    juce::PopupMenu createTrackMenu();
    juce::PopupMenu createColourMenu (TrackId);

    /** Playhead dragging: moves it to an x-position in the lanes, scrolling
        the timeline when that's past either edge. */
    void dragPlayheadTo (float laneX);
    void scrollRowIntoView (int row);

    RegionComponent* findRegion (RegionId) const;
    int rowOf (TrackId) const;
    int rowAt (int y) const;   // y in this component's coordinates
    juce::Rectangle<float> rowAreaInOverlay (int row) const;

    // Audio file drops
    struct DropTarget { TrackId track = 0; int row = -1; double beat = 0.0; };
    DropTarget findDropTarget (int x, int y) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ArrangementView)
};

} // namespace ikaros
