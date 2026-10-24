#include "ui/bottompanel/TuneSectionView.h"
#include "ui/theme/Fonts.h"
#include "ui/theme/Tokens.h"
#include "ui/theme/IkarosLookAndFeel.h"
#include "ui/theme/TrackColours.h"

namespace ikaros
{

using namespace tokens;

TuneSectionView::TuneSectionView (AudioEngine& e, Selection& s) : engine (e), selection (s)
{
    setTitle ("Tune");

    // Like the Noise Gate's: green while it's on.
    onButton.setColours (IconButton::powerColours);
    onButton.setFont (fonts::uiSemiBold (textSize::sm));
    onButton.setCornerRadius (6.0f);
    onButton.onClick = [this]
    {
        engine.getTracks().effects().setTuneOn (trackId, ! onButton.getToggleState());
    };
    addAndMakeVisible (onButton);

    settingsButton.setColours (IconButton::borderedColours);
    settingsButton.setFont (fonts::ui (textSize::sm));
    settingsButton.setCornerRadius (size::radiusSmall);
    settingsButton.setTooltip ("Tune's key, the notes it can land on, Follow MIDI and its readout");
    settingsButton.onClick = [this]
    {
        if (tuneId != 0 && onOpenEffect)
            onOpenEffect (tuneId);
    };
    addAndMakeVisible (settingsButton);

    setUpSlider (correction, 0, "Tune correction", "Tune Correction", "How far each note is moved to the nearest note in the key");
    setUpSlider (speed, 1, "Tune speed", "Tune Speed", "How quickly the correction follows the voice: Natural to Robotic");
}

void TuneSectionView::setUpSlider (juce::Slider& slider, int parameterIndex, const juce::String& title,
                                   const juce::String& undoName, const juce::String& tip)
{
    // The slider covers 0..1 of the knob of the same name in Tune's settings.
    slider.setTitle (title);
    slider.setTooltip (tip);
    slider.setRange (0.0, 1.0);
    slider.setScrollWheelEnabled (false);   // every notch would be an undo step of its own

    // A whole drag is one undo step (see AudioEngine::beginUndoStep).
    slider.onDragStart = [this, &slider, undoName]
    {
        dragging = &slider;
        engine.beginUndoStep (undoName);
    };
    slider.onDragEnd = [this] { dragging = nullptr; };
    slider.onValueChange = [this, &slider, parameterIndex, undoName]
    {
        // A click on the track (not a drag) is a step of its own.
        if (dragging != &slider)
            engine.beginUndoStep (undoName);

        auto& effects = engine.getTracks().effects();
        effects.setEffectParameter (trackId, tuneId, parameterIndex, (float) slider.getValue());

        const auto params = effects.getEffectParameters (trackId, tuneId);

        if (params.size() > 1)
        {
            correctionText = params[0].valueText;
            speedText = params[1].valueText;
        }

        repaint();
    };
    addAndMakeVisible (slider);
}

void TuneSectionView::refresh()
{
    auto& tracks = engine.getTracks();
    auto& effects = tracks.effects();

    trackId = selection.getTrack();
    auto track = tracks.getTrack (trackId);
    shown = track.has_value() && effects.showsTuneSection (trackId);

    if (shown)
    {
        tuneId = effects.getFirstTune (trackId);
        const auto chain = effects.getEffectChain (trackId);
        const auto slot = std::find_if (chain.begin(), chain.end(), [this] (const PluginSlot& p) { return p.id == tuneId; });
        const bool on = slot != chain.end() && slot->enabled;
        const bool chainFull = (int) chain.size() >= EffectsController::maxEffectsPerTrack;

        onButton.setToggleState (on, juce::dontSendNotification);
        onButton.setText (on ? "Tune on" : "Tune off");

        // With no Tune yet and no room in the chain, there's nowhere to put one.
        onButton.setEnabled (tuneId != 0 || ! chainFull);
        onButton.setTooltip (onButton.isEnabled() ? "Keep the voice in the song's key as you sing"
                                                  : "The effect chain is full: remove an effect to make room");
        settingsButton.setEnabled (tuneId != 0);

        const auto params = tuneId != 0 ? effects.getEffectParameters (trackId, tuneId) : std::vector<ParameterInfo>();

        // dontSendNotification: showing the values isn't a change to make.
        // (Not during a drag: the drag is ahead of the session.)
        if (params.size() > 1)
        {
            if (dragging != &correction)
                correction.setValue (params[0].value, juce::dontSendNotification);

            if (dragging != &speed)
                speed.setValue (params[1].value, juce::dontSendNotification);

            correctionText = params[0].valueText;
            speedText = params[1].valueText;
        }
        else
        {
            correctionText = speedText = {};
        }

        for (auto* slider : { &correction, &speed })
        {
            slider->setEnabled (on);
            IkarosLookAndFeel::setFamilyColour (*slider, colour::familyOf (*track));
        }
    }

    resized();
    repaint();
}

int TuneSectionView::getIdealHeight() const
{
    return 8 + 30 + 10 + 30 + 6 + 30 + 6 + 32 + 8;
}

void TuneSectionView::paint (juce::Graphics& g)
{
    if (! shown)
        return;

    auto label = [&] (const juce::Slider& slider, const juce::String& name, juce::Rectangle<int> row,
                      const juce::String& value, juce::Rectangle<int> valueArea)
    {
        g.setFont (fonts::ui (textSize::sm));
        g.setColour (slider.isEnabled() ? theme().textSecondary : theme().textSecondary.withMultipliedAlpha (0.5f));
        g.drawText (name, row, juce::Justification::centredLeft);

        g.setColour (slider.isEnabled() ? theme().text : theme().textSecondary);
        g.setFont (fonts::mono (textSize::sm));
        g.drawText (value, valueArea, juce::Justification::centredRight);
    };

    label (correction, "Correction", correctionRow, correctionText, correctionValue);
    label (speed, "Speed", speedRow, speedText, speedValue);

    g.setColour (theme().textSecondary);
    g.setFont (fonts::ui (textSize::sm));
    g.drawFittedText ("Keeps the voice in the song's key. Settings has the key, the notes and Follow MIDI.",
                      hintArea, juce::Justification::topLeft, 2, 1.0f);
}

void TuneSectionView::resized()
{
    auto r = getLocalBounds().reduced (sidePadding, 0);
    r.removeFromTop (8);

    auto buttons = r.removeFromTop (30);
    onButton.setBounds (buttons.removeFromLeft (110));
    buttons.removeFromLeft (8);
    settingsButton.setBounds (buttons.removeFromLeft (84));
    r.removeFromTop (10);

    auto sliderRow = [&] (juce::Slider& slider, juce::Rectangle<int>& row, juce::Rectangle<int>& value)
    {
        row = r.removeFromTop (30);
        auto area = row.withTrimmedLeft (80);
        value = area.removeFromRight (64);
        slider.setBounds (area.withTrimmedRight (6));
        r.removeFromTop (6);
    };

    sliderRow (correction, correctionRow, correctionValue);
    sliderRow (speed, speedRow, speedValue);

    hintArea = r.removeFromTop (32);
}

} // namespace ikaros
