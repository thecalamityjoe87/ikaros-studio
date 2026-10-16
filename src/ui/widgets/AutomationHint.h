#pragma once

#include "engine/AudioEngine.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace ikaros
{

//==============================================================================
/**
    A small callout that points at a control and explains something, with an
    optional button. Only one shows at a time; it goes away by itself after a
    few seconds, or when clicked.

    It's added to the window the control is in, and deleted when it goes
    away (DeletedAtShutdown tidies up one still showing when the app quits).
*/
class HintBubble  : public juce::Component,
                    public juce::DeletedAtShutdown,
                    private juce::Timer
{
public:
    static void show (juce::Component& target, const juce::String& text,
                      const juce::String& actionName = {}, std::function<void()> action = {});

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    HintBubble (juce::Component& target, const juce::String& text, const juce::String& actionName, std::function<void()> action);

    juce::TextLayout layout;
    juce::String actionName;
    std::function<void()> action;
    juce::Rectangle<float> bubble, actionArea;
    float arrowX = 0.0f;
    bool pointsUp = true;      // the bubble sits below the control, its arrow pointing up at it

    void timerCallback() override;
    void dismiss();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HintBubble)
};

//==============================================================================
/**
    Makes a slider follow automation, the way GarageBand does. While
    isAutomated() returns true the slider can still be dragged, but the move
    is only temporary: its own callbacks aren't called (the automation curve
    sets the value), it springs back when it's let go, and onAutomatedGrab
    runs (to explain, see showAutomationHint).

    Call it after setting the slider's onDragStart, onValueChange and
    onDragEnd: it wraps them.
*/
void followAutomation (juce::Slider&, std::function<bool()> isAutomated, std::function<void()> onAutomatedGrab);

/** The colour an automation curve is drawn in: one per kind of setting
    (volume, pan, each send, Sound knobs, plug-in parameters). */
juce::Colour getAutomationColour (const juce::String& targetId);

/** Marks a slider as automated: the LookAndFeel draws its thumb amber. */
void setAutomatedLook (juce::Slider&, bool automated);

/** The hint shown when someone grabs an automated control: what's going on,
    and a button that opens the track's automation lane on that setting. */
void showAutomationHint (AudioEngine&, juce::Component& control, TrackId, const juce::String& targetId);

} // namespace ikaros
