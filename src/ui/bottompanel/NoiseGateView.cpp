#include "ui/bottompanel/NoiseGateView.h"
#include "ui/theme/Fonts.h"
#include "ui/theme/Tokens.h"

namespace ikaros
{

using namespace tokens;

NoiseGateView::NoiseGateView (AudioEngine& e, Selection& s) : engine (e), selection (s)
{
    setTitle ("Noise Gate");

    // Like the Monitoring button: green while it's on.
    onButton.setColours ({ colour::controlFill, colour::textSecondary, colour::effectOn, colour::textOnAccent });
    onButton.setFont (fonts::uiSemiBold (textSize::sm));
    onButton.setCornerRadius (6.0f);
    onButton.onClick = [this]
    {
        engine.getTracks().effects().setNoiseGateOn (trackId, ! onButton.getToggleState());
    };
    addAndMakeVisible (onButton);

    // The slider covers 0..1 of the gate's threshold knob; the engine says
    // what that is in dB.
    threshold.setTitle ("Noise gate threshold");
    threshold.setTooltip ("Sound quieter than this is silenced");
    threshold.setRange (0.0, 1.0);
    threshold.setScrollWheelEnabled (false);   // every notch would be an undo step of its own

    // A whole drag is one undo step: it starts on mouse-down, and every
    // change until mouse-up goes into it (see AudioEngine::beginUndoStep).
    threshold.onDragStart = [this]
    {
        dragging = true;
        engine.beginUndoStep ("Noise Gate Threshold");
    };
    threshold.onDragEnd = [this] { dragging = false; };
    threshold.onValueChange = [this]
    {
        // A click on the track (not a drag) is a step of its own.
        if (! dragging)
            engine.beginUndoStep ("Noise Gate Threshold");

        auto& effects = engine.getTracks().effects();
        effects.setNoiseGateThreshold (trackId, (float) threshold.getValue());
        thresholdText = effects.getNoiseGate (trackId).thresholdText;
        repaint (valueArea);
    };
    addAndMakeVisible (threshold);
}

void NoiseGateView::refresh()
{
    auto& tracks = engine.getTracks();

    trackId = selection.getTrack();
    auto track = tracks.getTrack (trackId);
    isAudio = track.has_value() && track->kind == TrackKind::audio;

    if (isAudio)
    {
        const auto gate = tracks.effects().getNoiseGate (trackId);
        const bool chainFull = (int) tracks.effects().getEffectChain (trackId).size() >= EffectsController::maxEffectsPerTrack;

        onButton.setToggleState (gate.on, juce::dontSendNotification);
        onButton.setText (gate.on ? "Noise gate on" : "Noise gate off");

        // With no gate yet and no room in the chain, there's nowhere to put one.
        onButton.setEnabled (gate.exists || ! chainFull);
        onButton.setTooltip (onButton.isEnabled() ? "Silence hiss and room noise between phrases"
                                                  : "The effect chain is full: remove an effect to make room");

        // dontSendNotification: showing the gate's value isn't a change to
        // make. (Not during a drag: the drag is ahead of the session.)
        if (! dragging)
            threshold.setValue (gate.threshold, juce::dontSendNotification);

        threshold.setEnabled (gate.on);
        threshold.setColour (juce::Slider::trackColourId, track->colour);
        thresholdText = gate.thresholdText;
    }

    resized();
    repaint();
}

int NoiseGateView::getIdealHeight() const
{
    return 8 + 30 + 10 + 30 + 6 + 32 + 8;
}

void NoiseGateView::paint (juce::Graphics& g)
{
    if (! isAudio)
        return;

    g.setFont (fonts::ui (textSize::sm));
    g.setColour (threshold.isEnabled() ? colour::textSecondary : colour::textSecondary.withMultipliedAlpha (0.5f));
    g.drawText ("Threshold", thresholdRow, juce::Justification::centredLeft);

    g.setColour (threshold.isEnabled() ? colour::text : colour::textSecondary);
    g.setFont (fonts::mono (textSize::sm));
    g.drawText (thresholdText, valueArea, juce::Justification::centredRight);

    g.setColour (colour::textSecondary);
    g.setFont (fonts::ui (textSize::sm));
    g.drawFittedText ("Silences the track while it's quieter than the threshold.",
                      hintArea, juce::Justification::topLeft, 2, 1.0f);
}

void NoiseGateView::resized()
{
    auto r = getLocalBounds().reduced (sidePadding, 0);
    r.removeFromTop (8);

    onButton.setBounds (r.removeFromTop (30).withWidth (130));
    r.removeFromTop (10);

    thresholdRow = r.removeFromTop (30);
    auto sliderArea = thresholdRow.withTrimmedLeft (72);
    valueArea = sliderArea.removeFromRight (64);
    threshold.setBounds (sliderArea.withTrimmedRight (6));
    r.removeFromTop (6);

    hintArea = r.removeFromTop (32);
}

} // namespace ikaros
