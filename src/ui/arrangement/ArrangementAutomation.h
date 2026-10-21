#pragma once

#include "ui/arrangement/ArrangementView.h"

namespace ikaros
{

//==============================================================================
/**
    A track's automation: one setting's curve, drawn over the track's lane
    (whose regions fade back while it shows) on the same beat grid, so it
    follows zoom and scroll. The track header picks which setting it shows.

        click            add a point (on the grid, if snap is on)
        click a point    select it (Shift or Ctrl/Cmd: add to the selection)
        drag points      move the selected points (one undo step per drag)
        drag elsewhere   select the points in a box
        double-click     delete a point
        Delete           delete the selected points
        Ctrl/Cmd+A       select every point

    Without points, the line shows the setting's value; the first point
    starts automating it.
*/
class ArrangementView::AutomationLane  : public juce::Component,
                                         public juce::SettableTooltipClient
{
public:
    AutomationLane (ArrangementView&, TrackId);

    void update (const TrackInfo&);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    ArrangementView& owner;
    const TrackId trackId;
    juce::String targetId;
    bool editable = true, dimmed = false;
    float opacity = 1.0f;                  // less when the track is muted

    std::vector<AutomationPoint> points;   // sorted by beat
    std::vector<bool> selected;            // one per point
    float currentValue = 0.0f;             // the setting's value now (the line, when there are no points)

    // The gesture in progress.
    enum class Gesture { none, pending, movePoints, selectBox };
    Gesture gesture = Gesture::none;
    std::vector<AutomationPoint> pointsAtStart;
    std::vector<bool> selectedAtStart;
    int grabbedPoint = -1;                 // the point under the mouse when the drag started (in pointsAtStart)
    int labelPoint = -1;                   // where it is in `points` now (they're re-sorted as they move)
    bool undoStepStarted = false;
    juce::Rectangle<float> selectBox;

    float valueToY (float value) const;
    float yToValue (float y) const;
    juce::Point<float> pointPosition (const AutomationPoint&) const;
    int pointAt (juce::Point<float>) const;   // -1 if none
    int numSelected() const;

    /** Sorts points (keeping each one's selection), shows them and sends them to the engine. */
    void apply (std::vector<AutomationPoint>, std::vector<bool>, const juce::String& undoName);
    void deleteSelected();
    void showMenu (juce::Point<float>);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AutomationLane)
};

} // namespace ikaros
