#pragma once

#include "engine/EngineTypes.h"
#include "engine/WaveformCache.h"
#include "ui/bottompanel/EditorGrid.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <optional>

namespace ikaros
{

class AudioEngine;

//==============================================================================
/**
    The "Editor" tab for an audio region: its waveform, close up.

        ┌ hint                                                                 ⌕──○ ┐  toolbar
        │  1         2         3         4                                           │  ruler
        │ ░░░░░░│▁▂▅▇█▇▅▃▂▁▂▅▇█▇▅▃▂▁▁▂▃▅▇█▇▅▃▂▁│░░░░░░░░                               │  waveform
        └ ◀═════════════ time scroll bar ════════════════▶ ────────────────────────────┘

    The region is shown with the rest of its recording (or its loop
    repeating) dimmed on either side. Drag the region's edges to trim it,
    the dots at the top to fade it in or out, and click to move the
    playhead. Ctrl/Cmd + wheel zooms, the wheel scrolls.

    The region's settings (gain, pitch, reverse, the grid) are in the
    Region inspector on the panel's left (see RegionInspector).

    The view is measured from the region's start, so it stays on the region
    when the region is moved in the arrangement. A drag only previews; the
    change reaches the session (as one undo step) when the mouse is let go.
    The waveform comes from RegionEditor::getWaveform, which reads the
    recording on a background thread: until it's ready, update() (on the
    panel's timer) keeps asking.
*/
class AudioEditor  : public juce::Component,
                     private juce::ScrollBar::Listener
{
public:
    explicit AudioEditor (AudioEngine&);
    ~AudioEditor() override;

    void setRegion (std::optional<RegionInfo>, juce::Colour trackColour, double beatsPerBar);
    void setPlayheadBeat (double editBeat);

    /** Where the region's edges snap to when they're dragged (see RegionInspector). */
    void setGrid (EditorGrid);

    /** Called by the panel's ~30 Hz timer: picks up the waveform once it's been read. */
    void update();

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    // What a mouse drag is doing. While it lasts, the region is drawn with
    // `start`/`end`/the fades as they'll end up.
    struct Drag
    {
        enum class Mode { none, trimStart, trimEnd, fadeIn, fadeOut, playhead };

        Mode mode = Mode::none;
        int startX = 0;
        double start = 0.0, end = 0.0;           // the region's edges, in beats on the timeline
        double fadeIn = 0.0, fadeOut = 0.0;      // in beats
    };

    AudioEngine& engine;
    std::optional<RegionInfo> region;
    juce::Colour colour;
    double beatsPerBar = 4.0;
    double playheadBeat = -1.0;     // on the timeline; <0 = hidden

    // View: beats are measured from the region's start (see the class comment).
    double pixelsPerBeat = 60.0;
    double scrollBeats = 0.0;       // the beat at the left edge
    bool needsFit = true;           // zoom to fit the region on the next layout

    EditorGrid grid = EditorGrid::sixteenth;
    Drag drag;

    // The waveform for the current view, one column per pixel. Re-read when
    // the view or the region changes (waveformDirty), and while the engine
    // is still reading the recording (waveformPending).
    std::vector<WaveformRange> columns;
    bool waveformDirty = true, waveformPending = false;

    juce::Slider timeZoom   { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };
    juce::ScrollBar timeScroll { false };

    juce::Rectangle<int> toolbarArea, hintArea, timeZoomIcon, rulerArea, waveArea;

    bool hasRegion() const noexcept     { return region.has_value() && ! region->isMidi; }

    //==============================================================================
    // Coordinates. `rel` beats are from the region's start; `edit` beats are on the timeline.
    float beatToX (double rel) const;
    double xToBeat (float x) const;
    double visibleBeats() const;
    juce::Range<double> viewLimits() const;   // how far the view can scroll, in rel beats

    // Grid
    double snapToGrid (double editBeat) const;

    // View
    void fitToRegion();
    void setTimeZoom (double pixelsPerBeat, float anchorX);
    void clampView();
    void scrollBarMoved (juce::ScrollBar*, double newRangeStart) override;
    void refreshWaveform();

    // The region as drawn: the drag's preview while there is one.
    double shownStart() const;
    double shownEnd() const;
    double shownFade (bool fadeIn) const;
    juce::Point<float> fadeHandle (bool fadeIn) const;
    Drag::Mode modeAt (juce::Point<int>) const;

    // Editing
    void updateDrag (const juce::MouseEvent&);
    void finishDrag();

    // Painting
    void paintToolbar (juce::Graphics&);
    void paintRuler (juce::Graphics&);
    void paintWaveform (juce::Graphics&);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioEditor)
};

} // namespace ikaros
