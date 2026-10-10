#pragma once

#include "ui/arrangement/RegionComponent.h"
#include "ui/state/Selection.h"
#include "ui/arrangement/TrackHeader.h"
#include <map>

namespace ikaros
{

//==============================================================================
/**
    The main timeline: bar ruler (with loop range), track headers, lanes with
    regions, the dashed drop zone, and the playhead.

    Layout:
        ┌─────────── 240 ───────────┬──────────── lanes ────────────┐
        │ TRACKS                    │ ruler  1   2   3   4 ...      │  30px
        ├───────────────────────────┼───────────────────────────────┤
        │ TrackHeader               │ lane  [region] [region]       │  76px
        │ TrackHeader               │ lane                          │  each
        │                           │ ┌ drop zone ┐                 │
        └───────────────────────────┴───────────────────────────────┘
    Headers + lanes scroll vertically together inside a Viewport; the ruler
    stays put. The playhead is a transparent overlay drawn over both.

    For this milestone the timeline zooms to fit the window (no horizontal
    scrolling). TODO(milestone-2): horizontal zoom + scroll.
*/
class ArrangementView  : public juce::Component
{
public:
    ArrangementView (AudioEngine&, Selection&);
    ~ArrangementView() override;

    /** Re-reads tracks and regions from the engine. Call after model changes. */
    void refresh();

    /** Updates the playhead and loop bar. Called by the owner's ~30 Hz timer. */
    void updateTransport();

    std::function<void (RegionId)> onOpenRegionInEditor;
    std::function<void()> onAddTrackRequested;

    void paint (juce::Graphics&) override;
    void resized() override;

    /** Maps between beats and x-positions within a lane. */
    struct Geometry
    {
        double pixelsPerBeat = 20.0;
        double beatsPerBar = 4.0;
        int numBars = 16;

        float beatToX (double beat) const   { return (float) (beat * pixelsPerBeat); }
        double xToBeat (float x) const      { return x / pixelsPerBeat; }
    };

private:
    class Ruler;
    class Lane;
    class DropZone;
    class Playhead;
    class Content;

    AudioEngine& engine;
    Selection& selection;
    Geometry geometry;

    std::unique_ptr<Ruler> ruler;
    std::unique_ptr<Content> content;
    std::unique_ptr<Playhead> playhead;
    juce::Viewport viewport;

    std::vector<TrackInfo> tracks;
    double contentEndBeat = 0.0;

    void updateGeometry();
    void layoutContent();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ArrangementView)
};

} // namespace ikaros
