#pragma once

// Internal to ArrangementView: only its own .cpp files include this.

#include "ui/arrangement/ArrangementView.h"

namespace ikaros
{

//==============================================================================
// The dashed "Drop a sound, loop or audio file here" zone. It only shows
// where to drop: the ArrangementView handles the drop itself, and highlights
// the zone while something is dragged over it. Clicks pass through.
class ArrangementView::DropZone  : public juce::Component
{
public:
    DropZone();

    void paint (juce::Graphics&) override;
};

//==============================================================================
// The dashed "+ Add track" button under the last track header. It does what
// the toolbar's New Track button used to: opens the New Track window.
// (A juce::Button gives us clicks, keyboard focus, Space/Return and the
// screen-reader role for free; only the drawing is ours.)
class ArrangementView::AddTrackButton  : public juce::Button
{
public:
    AddTrackButton();

    void paintButton (juce::Graphics&, bool isMouseOver, bool isButtonDown) override;
};

//==============================================================================
// The playhead: amber line with a rounded head, drawn over the ruler and lanes.
// The part over the ruler can be dragged; clicks anywhere else pass through
// to the ruler and lanes underneath (see hitTest).
class ArrangementView::Playhead  : public juce::Component,
                                  private juce::Timer
{
public:
    explicit Playhead (ArrangementView&);

    bool hitTest (int px, int py) override;

    void mouseEnter (const juce::MouseEvent&) override   { setHighlighted (true); }
    void mouseExit (const juce::MouseEvent&) override    { setHighlighted (dragging); }

    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

    void setX (float newX);

    void paint (juce::Graphics&) override;

private:
    static constexpr float grabRadius = 8.0f;

    ArrangementView& owner;
    float x = -100.0f;
    bool highlighted = false, dragging = false;
    float grabOffset = 0.0f, dragX = 0.0f;

    void timerCallback() override;
    void setHighlighted (bool shouldHighlight);
    juce::Rectangle<int> stripAt (float px) const;
};

//==============================================================================
// Drawn over the lanes: outlines showing where dragged regions will land, and
// where dragged-in audio files will go.
class ArrangementView::Overlay  : public juce::Component
{
public:
    struct Ghost
    {
        juce::Rectangle<float> area;
        juce::Colour colour;
        juce::String name;
    };

    Overlay();

    void setGhosts (std::vector<Ghost> newGhosts);
    /** markerX: where a dropped file would start (negative = no marker). */
    void setDropTarget (juce::Rectangle<float> area, float markerX, const juce::String& text);
    void clear();

    void paint (juce::Graphics&) override;

private:
    std::vector<Ghost> ghosts;
    juce::Rectangle<float> dropArea;
    float dropMarkerX = 0.0f;
    juce::String dropText;
};

} // namespace ikaros
