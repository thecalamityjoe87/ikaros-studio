#pragma once

#include "engine/EngineTypes.h"
#include "ui/bottompanel/EditorGrid.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <optional>

namespace ikaros
{

class AudioEngine;

//==============================================================================
/**
    The "Editor" tab: a piano roll for the selected MIDI region.

        ┌ hints...                                         ⌕──○  ♪──○ ┐  toolbar
        │ keys │ note grid                                           │▲│
        │      │   ▬▬▬   ▬▬▬▬▬                                       │ │
        ├──────┼─────────────────────────────────────────────────────┤ │
        │ Vel. │ ╷   ╷  velocity lane                                │ │
        └──────┴ ◀═════════════ time scroll bar ════════════════▶ ───┘

    Mouse: click empty space to add a note (drag to set its length), drag a
    note to move it, drag its right end to resize, Shift-click or Shift-drag
    to select several, drag a stem in the velocity lane to change velocity,
    click a key to hear it. Right-click for more.
    Keys: Delete, Ctrl/Cmd+A, Escape (deselect, or cancel a drag), Q
    (quantize), arrows (move/transpose). The grid and Quantize button are in
    the Region inspector on the panel's left (see RegionInspector).

    The code is in three files: PianoRoll.cpp (the region, view and editing),
    PianoRollGestures.cpp (mouse, wheel and keys) and PianoRollPainting.cpp,
    with shared sizes in PianoRollLayout.h.

    Every change goes through TrackController as one undo step, and the roll
    re-reads the region after each edit. The selection is kept by note ID
    (see NoteInfo), so it follows notes that move. Mouse hit-testing works
    with indices into the region's current note list.
*/
class PianoRoll  : public juce::Component,
                   private juce::ScrollBar::Listener
{
public:
    explicit PianoRoll (AudioEngine&);
    ~PianoRoll() override;

    void setRegion (std::optional<RegionInfo>, juce::Colour trackColour, const TempoMap&);
    void setPlayheadBeat (double editBeat);

    /** Where notes snap to when they're drawn, moved or resized (see RegionInspector). */
    void setGrid (EditorGrid);

    /** Lines the selected notes (or all of them) up with the grid, as one undo step. */
    void quantize();

    /** Quantize's strength and swing, 0..1 each (see QuantizeSettings). */
    void setQuantizeFeel (double strength, double swing);

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed (const juce::KeyPress&) override;
    void focusLost (FocusChangeType) override;

private:
    // What a mouse drag is doing.
    struct Gesture
    {
        enum class Mode { none, previewKey, draw, pendingMove, move, resize, velocity, marquee };

        Mode mode = Mode::none;
        juce::Point<int> start;
        int anchor = -1;                       // index (in `notes`) of the note that was clicked
        bool selectOnlyAnchorOnClick = false;
        std::vector<NoteInfo> notes;           // the notes as they'll look if the gesture ends now
        NoteInfo newNote;                      // draw mode: the note being drawn
        juce::Rectangle<int> marquee;
    };

    AudioEngine& engine;
    std::optional<RegionInfo> region;
    juce::Colour colour;
    TempoMap tempo;   // the song's bars (they can differ in length)
    double playheadBeat = -1.0;     // relative to the region start; <0 = hidden

    // View. Rows count down from the top note (127): row r shows pitch 127 - r.
    double pixelsPerBeat = 60.0;
    double scrollBeats = 0.0;       // beat (from the region start) at the left edge
    float rowHeight = 12.0f;
    double topRow = 50.0;
    bool needsFit = true;           // zoom to fit the region on the next layout

    EditorGrid grid = EditorGrid::sixteenth;
    double quantizeStrength = 1.0, quantizeSwing = 0.0;   // see setQuantizeFeel
    double lastNoteLength = 0.25;   // new notes get the length of the last one drawn or resized
    std::vector<NoteId> selected;   // note IDs, sorted
    Gesture gesture;
    int previewNote = -1;           // the note sounding while the mouse is held, or -1

    juce::Slider timeZoom { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };
    juce::Slider noteZoom { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };
    juce::ScrollBar timeScroll { false }, noteScroll { true };

    juce::Rectangle<int> toolbarArea, hintArea, timeZoomIcon, noteZoomIcon;
    juce::Rectangle<int> keysArea, gridArea, velocityLabelArea, velocityArea;

    //==============================================================================
    // Coordinates. Beats are relative to the region start.
    float beatToX (double beat) const;
    double xToBeat (float x) const;
    float pitchToY (int pitch) const;
    int yToPitch (int y) const;
    double visibleBeats() const;
    double visibleRows() const;
    double regionLength() const;
    double contentStartBeat() const;
    juce::Rectangle<float> noteBounds (const NoteInfo&) const;

    // Grid
    double gridBeats() const;                              // 0 = off
    double snapToGrid (double beat, bool roundDown) const; // on the song's timeline

    // View
    void fitToRegion();
    void setTimeZoom (double pixelsPerBeat, float anchorX);
    void setNoteZoom (float rowHeight, float anchorY);
    void clampView();
    void updateScrollBars();
    void scrollBarMoved (juce::ScrollBar*, double newRangeStart) override;

    // Notes
    const std::vector<NoteInfo>& notesToDraw() const;
    int noteAt (juce::Point<int>) const;
    int stemAt (juce::Point<int>) const;
    bool isNearNoteEnd (int index, juce::Point<int>) const;
    bool isSelected (int index) const;
    void selectOnly (int index);
    void toggleSelected (int index);
    void selectAll();
    std::vector<int> selectedIndices() const;   // where the selected notes are in region->notes

    void rereadRegion();
    void applyChanges (const std::vector<NoteInfo>&, const juce::String& undoName);
    void deleteSelected();
    void nudgeSelected (double beats, int semitones);
    void showNoteMenu();

    // Gestures
    void updateMove (juce::Point<int>);
    void updateResize (juce::Point<int>);
    void updateVelocity (juce::Point<int>);
    void updateMarquee (juce::Point<int>);

    // Hearing notes
    void startPreview (int pitch, int velocity = 100);
    void stopPreview();

    // Painting
    void paintToolbar (juce::Graphics&);
    void paintKeys (juce::Graphics&);
    void paintGrid (juce::Graphics&);
    void paintNotes (juce::Graphics&);
    void paintVelocities (juce::Graphics&);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PianoRoll)
};

} // namespace ikaros
