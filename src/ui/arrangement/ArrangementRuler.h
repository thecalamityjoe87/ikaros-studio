#pragma once

// Internal to ArrangementView: only its own .cpp files include this.

#include "ui/arrangement/ArrangementView.h"

namespace ikaros
{

//==============================================================================
// Ruler: bar numbers + loop range. Click/drag the lower part to move the
// playhead; drag along the top strip to set the loop range; right-click for
// tempo and time-signature changes. The left part (above the track headers)
// holds the snap, automation, tempo lane and zoom controls.
class ArrangementView::Ruler  : public juce::Component,
                               public juce::SettableTooltipClient   // adds setTooltip()
{
public:
    explicit Ruler (ArrangementView&);

    void setLoop (bool on, double start, double end);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    ArrangementView& owner;
    bool looping = false, draggingLoop = false, ignoreDrag = false;
    double loopStart = 0.0, loopEnd = 0.0, dragStartBeat = 0.0;

    double beatAt (int x) const;
};

} // namespace ikaros
