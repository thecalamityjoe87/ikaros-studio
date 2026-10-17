#pragma once

// Internal to ArrangementView: only its own .cpp files include this.

#include "ui/arrangement/ArrangementView.h"

namespace ikaros
{

//==============================================================================
// The tempo lane, under the ruler: the song's tempo as a line that steps up
// and down at tempo changes, with the time signature written at the bar
// where it starts.
//
//   - Double-click: a new tempo change at the nearest bar line.
//   - Drag a point: move the change to another bar and/or change its tempo
//     (up and down; hold Shift for finer steps). Drag a flat part of the
//     line up or down to change the tempo there.
//   - Double-click a point: type its tempo.
//   - Click a point and press Delete, or right-click it: delete it.
//   - Click a time signature: change or delete it. Right-click anywhere for
//     the same menu as the ruler's (tempo and time-signature changes).
//
// Each gesture is one undo step. Everything comes from owner.geometry.tempo,
// which ArrangementView::refresh re-reads after every change.
class ArrangementView::TempoLane  : public juce::Component,
                                   public juce::SettableTooltipClient
{
public:
    explicit TempoLane (ArrangementView&);

    void paint (juce::Graphics&) override;
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

    // What's under the mouse.
    struct Hit
    {
        enum class Kind { none, point, line, signature } kind = Kind::none;
        int index = -1;   // tempo change (point or line) or time-signature change
    };

    Hit hover, selected;   // selected: a clicked point, for the Delete key

    // A drag on a point or a line. The tempo range on screen is frozen while
    // it lasts, so the line doesn't rescale under the mouse.
    struct Drag
    {
        int index = -1;
        bool movesBar = false;           // points move along the bars; lines only change tempo
        double startBpm = 0.0, startBeat = 0.0;
        double lowBpm = 0.0, highBpm = 0.0;
        bool changed = false;
    };

    std::optional<Drag> drag;

    // The tempo field, shown over a point when it's double-clicked.
    juce::TextEditor tempoField;
    int editingIndex = -1;

    juce::Rectangle<int> laneArea() const;   // right of the header part
    juce::Rectangle<float> graphArea() const;   // where the line goes, in lane coordinates

    // The tempo range the line is drawn in: the song's lowest and highest
    // tempos with some room around them.
    std::pair<double, double> bpmRange() const;
    float bpmToY (double bpm, std::pair<double, double> range) const;

    float beatToLaneX (double beat) const;
    double laneXToBeat (float x) const;

    Hit findHit (juce::Point<float> laneXY) const;
    juce::Rectangle<float> signatureBounds (int index) const;   // the "3/4" label, in lane coordinates
    juce::Point<float> laneXY (const juce::MouseEvent&) const;

    void showSignatureMenu (int index);
    void showTempoField (int index);
    void commitTempoField();
    void hideTempoField();
};

} // namespace ikaros
