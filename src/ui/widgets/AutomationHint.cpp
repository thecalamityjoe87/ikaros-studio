#include "ui/widgets/AutomationHint.h"
#include "engine/TrackController.h"
#include "ui/theme/Fonts.h"
#include "ui/theme/Tokens.h"

namespace ikaros
{

using namespace tokens;

namespace
{
    constexpr float bubbleWidth = 300.0f;
    constexpr float padding = 12.0f;
    constexpr float arrowSize = 8.0f;
    constexpr float actionHeight = 26.0f;
    constexpr int showForMs = 7000;

    // The one showing now. (SafePointer turns null by itself when it's deleted.)
    juce::Component::SafePointer<HintBubble>& currentBubble()
    {
        static juce::Component::SafePointer<HintBubble> current;
        return current;
    }
}

//==============================================================================
void HintBubble::show (juce::Component& target, const juce::String& text,
                       const juce::String& actionName, std::function<void()> action)
{
    delete currentBubble().getComponent();

    if (target.getTopLevelComponent() == nullptr || ! target.isShowing())
        return;

    currentBubble() = new HintBubble (target, text, actionName, std::move (action));
}

HintBubble::HintBubble (juce::Component& target, const juce::String& text,
                        const juce::String& name, std::function<void()> fn)
    : actionName (name), action (std::move (fn))
{
    juce::AttributedString s;
    s.append (text, fonts::ui (textSize::md), colour::text);
    s.setWordWrap (juce::AttributedString::byWord);
    layout.createLayout (s, bubbleWidth - padding * 2.0f);

    const float height = padding * 2.0f + layout.getHeight() + (actionName.isNotEmpty() ? actionHeight + 8.0f : 0.0f);

    // Below the control if it fits, else above; centred on it, but inside the window.
    auto* window = target.getTopLevelComponent();
    const auto targetArea = window->getLocalArea (&target, target.getLocalBounds()).toFloat();
    pointsUp = targetArea.getBottom() + arrowSize + height + 8.0f < (float) window->getHeight();

    const float x = juce::jlimit (8.0f, juce::jmax (8.0f, (float) window->getWidth() - bubbleWidth - 8.0f),
                                  targetArea.getCentreX() - bubbleWidth * 0.5f);
    const float y = pointsUp ? targetArea.getBottom() : targetArea.getY() - height - arrowSize;
    arrowX = juce::jlimit (padding + arrowSize, bubbleWidth - padding - arrowSize, targetArea.getCentreX() - x);

    setBounds (juce::Rectangle<float> (x, y, bubbleWidth, height + arrowSize).getSmallestIntegerContainer());
    setAlwaysOnTop (true);
    window->addAndMakeVisible (this);
    setTitle ("Hint");
    setDescription (text);

    startTimer (showForMs);
}

void HintBubble::resized()
{
    bubble = getLocalBounds().toFloat();

    if (pointsUp)
        bubble.removeFromTop (arrowSize);
    else
        bubble.removeFromBottom (arrowSize);

    actionArea = {};

    if (actionName.isNotEmpty())
        actionArea = bubble.reduced (padding).removeFromBottom (actionHeight).removeFromRight (140.0f);
}

void HintBubble::paint (juce::Graphics& g)
{
    // The bubble and its arrow as one outline, so the border runs round both.
    juce::Path outline;
    outline.addRoundedRectangle (bubble, size::radius);

    juce::Path arrow;
    const float tipY = pointsUp ? 0.0f : (float) getHeight();
    const float baseY = pointsUp ? bubble.getY() + 1.0f : bubble.getBottom() - 1.0f;
    arrow.addTriangle (arrowX - arrowSize, baseY, arrowX + arrowSize, baseY, arrowX, tipY);
    outline.addPath (arrow);
    outline.setUsingNonZeroWinding (true);

    g.setColour (colour::raised);
    g.fillPath (outline);
    g.setColour (colour::automated);
    g.strokePath (outline, juce::PathStrokeType (1.0f));

    layout.draw (g, bubble.reduced (padding).withHeight (layout.getHeight()));

    if (! actionArea.isEmpty())
    {
        g.setColour (colour::automated);
        g.fillRoundedRectangle (actionArea, size::radiusSmall);
        g.setColour (colour::textOnAccent);
        g.setFont (fonts::uiBold (textSize::sm));
        g.drawText (actionName, actionArea, juce::Justification::centred, true);
    }
}

void HintBubble::mouseDown (const juce::MouseEvent& e)
{
    // Copy the action first: dismiss() deletes this (soon, not right away).
    auto fn = action;
    const bool hitAction = actionArea.contains (e.position);
    dismiss();

    if (hitAction && fn)
        fn();
}

void HintBubble::timerCallback()
{
    // Stay while the mouse is on it: someone's reading it.
    if (! isMouseOver (true))
        dismiss();
}

void HintBubble::dismiss()
{
    stopTimer();
    setVisible (false);

    // Deleted from the message loop, not from inside one of our own callbacks.
    juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<HintBubble> (this)]
    {
        delete safe.getComponent();
    });
}

//==============================================================================
void followAutomation (juce::Slider& s, std::function<bool()> isAutomated, std::function<void()> onAutomatedGrab)
{
    // Shared by the three callbacks below (shared_ptr, since std::function
    // copies what it captures): whether this drag is a temporary one, and
    // the value to spring back to.
    struct Grab { bool active = false; double valueBefore = 0.0; };
    auto grab = std::make_shared<Grab>();

    auto start = s.onDragStart, change = s.onValueChange, end = s.onDragEnd;
    auto* slider = &s;   // the callbacks belong to the slider, so it outlives them

    s.onDragStart = [=]
    {
        grab->active = isAutomated();

        if (grab->active)
        {
            grab->valueBefore = slider->getValue();

            if (onAutomatedGrab)
                onAutomatedGrab();
        }
        else if (start)
        {
            start();
        }
    };

    s.onValueChange = [=]
    {
        if (! grab->active && change)
            change();
    };

    s.onDragEnd = [=]
    {
        if (std::exchange (grab->active, false))
            slider->setValue (grab->valueBefore, juce::dontSendNotification);
        else if (end)
            end();
    };
}

juce::Colour getAutomationColour (const juce::String& id)
{
    if (id == automationTargets::volume)                     return colour::automationVolume;
    if (id == automationTargets::pan)                        return colour::automationPan;
    if (id == automationTargets::send (SendBuses::reverb))   return colour::automationReverb;
    if (id == automationTargets::send (SendBuses::delay))    return colour::automationDelay;
    if (id.startsWith ("sound:"))                            return colour::automationSound;
    return colour::automationPlugin;
}

void setAutomatedLook (juce::Slider& s, bool automated)
{
    if ((bool) s.getProperties()["automated"] != automated)
    {
        s.getProperties().set ("automated", automated);   // read by IkarosLookAndFeel
        s.repaint();
    }
}

void showAutomationHint (AudioEngine& engine, juce::Component& control, TrackId track, const juce::String& targetId)
{
    const auto target = engine.getTracks().automation().getTarget (track, targetId);
    const auto name = target ? target->name : juce::String ("This setting");

    HintBubble::show (control,
                      name + " follows this track's automation, so moving it here won't stick. "
                             "To change it, edit its curve on the track.",
                      "Show Automation",
                      [&engine, track, targetId]
                      {
                          auto& automation = engine.getTracks().automation();
                          automation.setLaneTarget (track, targetId);
                          automation.setLaneShown (track, true);
                      });
}

} // namespace ikaros
