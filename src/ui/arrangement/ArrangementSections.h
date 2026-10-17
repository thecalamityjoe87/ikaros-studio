#pragma once

// Internal to ArrangementView: only its own .cpp files include this.

#include "ui/arrangement/ArrangementView.h"
#include "engine/SectionEditor.h"

namespace ikaros
{

//==============================================================================
// The marker strip, above the tracks: the song's sections ("Intro",
// "Verse"...) as coloured blocks along the timeline.
//
//   - Double-click an empty part (or right-click it): a new section from
//     that bar. The + in the header adds one after the last section.
//   - Double-click a section: type its name. Right-click it: rename,
//     recolour, delete.
//   - Drag a section's left or right edge: resize it, to bar lines. Only
//     the marker changes, not the music.
//   - Drag a section: its music on every track goes with it, and the
//     sections in between slide over to make room. Hold Alt (Option on a
//     Mac; Ctrl or Cmd work too) to copy it there instead.
//   - Click a section and press Delete: delete it and the music in it.
//
// Each gesture is one undo step. The sections come from SectionEditor;
// ArrangementView::refresh re-reads them after every change.
class ArrangementView::SectionStrip  : public juce::Component,
                                      public juce::SettableTooltipClient
{
public:
    explicit SectionStrip (ArrangementView&);

    /** Re-reads the sections from the engine. */
    void sync();

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed (const juce::KeyPress&) override;
    void focusLost (FocusChangeType) override;

private:
    ArrangementView& owner;
    std::vector<SectionInfo> sections;   // in song order

    // What's under the mouse: a section's body or one of its edges.
    struct Hit
    {
        enum class Kind { none, body, startEdge, endEdge } kind = Kind::none;
        SectionId id = 0;
    };

    Hit hover;
    SectionId selectedId = 0;   // a clicked section, for the Delete key

    // A drag: an edge (resizing) or the body (moving or copying).
    struct Drag
    {
        Hit::Kind kind = Hit::Kind::none;
        SectionInfo section;          // as it was when the drag started (resizes keep it up to date)
        bool changed = false;         // a resize has made its undo step
        bool moving = false;          // the body has moved far enough to count as a drag
        double ghostStart = 0.0;      // where the dragged section is drawn
        std::optional<double> target; // where it would go (see SectionEditor::moveSection)
        bool copy = false;
    };

    std::optional<Drag> drag;

    IconButton addButton { "Add section", Icon::plus };

    // The name field, shown over a section while it's renamed.
    juce::TextEditor nameField;
    SectionId editingId = 0;

    float beatToLaneX (double beat) const;
    double laneXToBeat (float x) const;
    juce::Point<float> laneXY (const juce::MouseEvent&) const;
    juce::Rectangle<float> blockBounds (const SectionInfo&) const;   // in lane coordinates

    const SectionInfo* find (SectionId) const;
    Hit findHit (juce::Point<float> laneXY) const;

    void updateMoveTarget (const juce::MouseEvent&);
    void addSectionAt (int bar);
    void addSectionAfterLast();
    void showSectionMenu (SectionId);
    void showEmptyMenu (double beat);
    juce::PopupMenu createColourMenu (SectionId);

    void showNameField (SectionId);
    void commitNameField();
    void hideNameField();
};

} // namespace ikaros
