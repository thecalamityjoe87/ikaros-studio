#pragma once

#include "engine/EngineTypes.h"
#include "engine/FlexEditor.h"
#include "engine/WaveformCache.h"
#include "ui/bottompanel/EditorGrid.h"
#include "ui/theme/Family.h"
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

    With Flex on (see FlexEditor), the hits found in the recording show as
    faint lines: drag one to move it, stretching the audio between its
    neighbours. A moved hit gets a marker (a brighter line with a handle at
    the bottom); double-click a marker to take it away again.

    Quantize (Q, the Region inspector's button, or the right-click menu)
    lines the hits up with the grid, like the piano roll's Quantize: those
    in the selection (drag across the waveform to select some), or all of
    them. It turns Flex on for the track if it's off; the hits are found
    first if they haven't been, and the toolbar says so meanwhile.

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

    void setRegion (std::optional<RegionInfo>, tokens::Family, const TempoMap&);
    void setPlayheadBeat (double editBeat);

    /** Where the region's edges snap to when they're dragged, and what
        Quantize lines the hits up with (see RegionInspector). */
    void setGrid (EditorGrid);

    /** Quantizes the hits in the selection, or all of them, as one undo step
        (see FlexEditor::quantize). */
    void quantize();

    /** Quantize's strength and swing, 0..1 each (see QuantizeSettings). */
    void setQuantizeFeel (double strength, double swing);

    /** Called by the panel's ~30 Hz timer: picks up the waveform once it's been read. */
    void update();

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    // What a mouse drag is doing. While it lasts, the region is drawn with
    // `start`/`end`/the fades as they'll end up.
    struct Drag
    {
        enum class Mode { none, trimStart, trimEnd, fadeIn, fadeOut, playhead, select, flex };

        Mode mode = Mode::none;
        int startX = 0;
        double start = 0.0, end = 0.0;           // the region's edges, in beats on the timeline
        double fadeIn = 0.0, fadeOut = 0.0;      // in beats
        double selectFrom = 0.0;                 // select: where it started, in beats from the region's start

        // Flex: the hit (or marker) being dragged, as a moment of the
        // recording, and where it's going, in beats on the timeline (of the
        // loop's first time round, for a looped region: `repeat` is how far
        // after that the grabbed repeat is).
        double flexSource = 0.0, flexBeat = 0.0, repeat = 0.0;
        std::vector<FlexMarker> preview;         // the markers as they'll end up
    };

    // What's under the mouse in Flex: a hit or a marker (as a moment of the
    // recording), and which repeat of a looped region it's in.
    struct FlexTarget
    {
        double source = 0.0, beat = 0.0, repeat = 0.0;
        bool isMarker = false;
    };

    AudioEngine& engine;
    std::optional<RegionInfo> region;
    tokens::Family family = tokens::Family::master;   // the track's
    juce::Colour trackColour() const;
    TempoMap tempo;   // the song's bars (they can differ in length)
    double playheadBeat = -1.0;     // on the timeline; <0 = hidden

    // View: beats are measured from the region's start (see the class comment).
    double pixelsPerBeat = 60.0;
    double scrollBeats = 0.0;       // the beat at the left edge
    bool needsFit = true;           // zoom to fit the region on the next layout

    EditorGrid grid = EditorGrid::sixteenth;
    double quantizeStrength = 1.0, quantizeSwing = 0.0;   // see setQuantizeFeel
    Drag drag;

    // The part of the region that's selected, in beats from its start (so
    // it stays on the same audio when the region moves). Quantize only
    // moves the hits in it.
    std::optional<juce::Range<double>> selection;

    // A short message in the toolbar (e.g. "already on the grid"), until noticeUntil (ms counter).
    juce::String notice;
    juce::uint32 noticeUntil = 0;
    FlexInfo flex;   // the region's Flex state (re-read while it's busy, see update())

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

    // Flex
    void refreshFlex();
    std::optional<FlexTarget> flexTargetAt (juce::Point<int>, bool markersOnly) const;
    template <typename Fn> void forEachRepeat (double firstBeat, Fn&&) const;
    juce::String flexStatus() const;

    // Editing
    void updateDrag (const juce::MouseEvent&);
    void finishDrag();
    void showMenu();
    void showNotice (const juce::String&);

    // Painting
    void paintToolbar (juce::Graphics&);
    void paintRuler (juce::Graphics&);
    void paintWaveform (juce::Graphics&);
    void paintFlex (juce::Graphics&, juce::Rectangle<float> area);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioEditor)
};

} // namespace ikaros
