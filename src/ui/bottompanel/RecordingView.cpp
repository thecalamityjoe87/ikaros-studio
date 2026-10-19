#include "ui/bottompanel/RecordingView.h"
#include "ui/dialogs/MonitoringWarning.h"
#include "ui/theme/Fonts.h"
#include "ui/theme/Tokens.h"

namespace ikaros
{

using namespace tokens;

RecordingView::RecordingView (AudioEngine& e, Selection& s) : engine (e), selection (s)
{
    setTitle ("Recording");

    inputBox.setTitle ("Input");
    inputBox.onChange = [this]
    {
        const auto index = inputBox.getSelectedItemIndex();

        if (juce::isPositiveAndBelow (index, (int) inputs.size()))
            engine.getTracks().inputs().setTrackInput (trackId, inputs[(size_t) index].id);
    };
    addChildComponent (inputBox);

    monitorButton.setColours (IconButton::powerColours);
    monitorButton.setFont (fonts::uiSemiBold (textSize::sm));
    monitorButton.setCornerRadius (6.0f);
    monitorButton.setTooltip ("Hear the input through your speakers or headphones");
    monitorButton.onClick = [this]
    {
        setMonitoringWithWarning (engine, trackId, ! monitorButton.getToggleState(), this);
    };
    addChildComponent (monitorButton);

    addChildComponent (meter);

    clipLight.onClear = [this] { engine.getTracks().inputs().clearClipIndicator (trackId); };
    addChildComponent (clipLight);
}

void RecordingView::refresh()
{
    const auto oldHeight = getIdealHeight();
    auto& tracks = engine.getTracks();

    trackId = selection.getTrack();
    auto track = tracks.getTrack (trackId);
    isAudio = track.has_value() && track->kind == TrackKind::audio;

    inputBox.setVisible (isAudio);
    monitorButton.setVisible (isAudio);
    meter.setVisible (isAudio);
    clipLight.setVisible (isAudio);

    if (isAudio)
    {
        auto settings = tracks.inputs().getRecordingSettings (trackId);
        inputs = tracks.inputs().getAudioInputs();

        inputBox.clear (juce::dontSendNotification);

        for (int i = 0; i < (int) inputs.size(); ++i)
        {
            inputBox.addItem (inputs[(size_t) i].name, i + 1);

            if (inputs[(size_t) i].id == settings.inputId)
                inputBox.setSelectedItemIndex (i, juce::dontSendNotification);
        }

        if (inputs.empty())
            inputBox.setTextWhenNothingSelected ("No audio inputs");

        monitorButton.setToggleState (settings.monitoring, juce::dontSendNotification);
        monitorButton.setText (settings.monitoring ? "Monitoring on" : "Monitoring off");
    }
    else
    {
        meter.reset();
    }

    resized();
    repaint();

    if (getIdealHeight() != oldHeight && onHeightChanged)
        onHeightChanged();
}

void RecordingView::updateMeter()
{
    if (isAudio && isShowing())
    {
        auto reading = engine.getTracks().inputs().getInputMeter (trackId);
        meter.setLevel (reading.level);
        clipLight.setClipped (reading.clipped);
    }
}

int RecordingView::getIdealHeight() const
{
    if (trackId == 0)
        return 40;

    return isAudio ? 8 + 30 + 10 + 30 + 10 + 12 + 10 + 36 + 10
                   : 70;
}

void RecordingView::paint (juce::Graphics& g)
{
    g.setColour (theme().textSecondary);
    g.setFont (fonts::ui (textSize::sm));

    if (trackId == 0)
    {
        g.drawText ("Select a track.", getLocalBounds().reduced (sidePadding, 6), juce::Justification::topLeft);
        return;
    }

    if (! isAudio)
    {
        g.drawFittedText ("Records from your MIDI keyboard, computer keyboard and the on-screen keyboard. "
                          "Press R or the Record button.",
                          getLocalBounds().reduced (sidePadding, 6), juce::Justification::topLeft, 4, 1.0f);
        return;
    }

    g.drawText ("Input", inputLabelArea, juce::Justification::centredLeft);
    g.drawFittedText ("Use headphones when monitoring a microphone, or it will feed back.",
                      hintArea, juce::Justification::topLeft, 2, 1.0f);
}

void RecordingView::resized()
{
    auto r = getLocalBounds().reduced (sidePadding, 0);
    r.removeFromTop (8);

    auto row = r.removeFromTop (30);
    inputLabelArea = row.removeFromLeft (52);
    inputBox.setBounds (row);
    r.removeFromTop (10);

    monitorButton.setBounds (r.removeFromTop (30).withWidth (130));
    r.removeFromTop (10);

    auto meterRow = r.removeFromTop (12);
    clipLight.setBounds (meterRow.removeFromRight (12));
    meterRow.removeFromRight (6);
    meter.setBounds (meterRow);
    r.removeFromTop (10);

    hintArea = r.removeFromTop (36);
}

} // namespace ikaros
