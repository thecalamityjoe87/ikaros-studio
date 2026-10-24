#include "ui/bottompanel/PluginEditor.h"
#include "ui/theme/Fonts.h"
#include "ui/theme/Tokens.h"
#include <algorithm>

namespace ikaros
{

using namespace tokens;

PluginEditor::PluginEditor (AudioEngine& e, TrackId t, PluginId p, Family family)
    : engine (e), trackId (t), pluginId (p)
{
    powerButton.setColours (IconButton::powerColours);
    powerButton.setCornerRadius (6.0f);
    powerButton.setIconSize (14.0f);
    powerButton.onClick = [this] { engine.getTracks().effects().setEffectEnabled (trackId, pluginId, ! slot.enabled); };
    addAndMakeVisible (powerButton);

    doneButton.setColours (IconButton::borderedColours);
    doneButton.setFont (fonts::uiSemiBold (textSize::md));
    doneButton.setTooltip ("Close the effect settings");
    doneButton.onClick = [this] { if (onClose) onClose(); };
    addAndMakeVisible (doneButton);

    auto params = engine.getTracks().effects().getEffectParameters (trackId, pluginId);

    for (int i = 0; i < (int) params.size(); ++i)
    {
        auto* knob = knobs.add (new Knob (params[(size_t) i].name));
        knob->setArcFamily (family);
        knob->onDragStart = [this, knob] { engine.beginUndoStep (slot.name + " " + knob->getName()); };
        knob->onValueChange = [this, i] (float v)
        {
            engine.getTracks().effects().setEffectParameter (trackId, pluginId, i, v);
            refreshValues();
        };
        addAndMakeVisible (knob);
    }

    if (engine.getTracks().effects().getTune (trackId, pluginId).exists)
    {
        tuneControls = std::make_unique<TuneControls> (engine, trackId, pluginId, family);
        addAndMakeVisible (*tuneControls);
    }

    refreshValues();
    startTimerHz (10);
}

void PluginEditor::refreshValues()
{
    auto& tracks = engine.getTracks();

    for (auto& s : tracks.effects().getEffectChain (trackId))
    {
        if (s.id == pluginId)
        {
            if (s.name != slot.name || s.enabled != slot.enabled)
            {
                slot = s;
                setTitle (slot.name + " settings");
                powerButton.setToggleState (slot.enabled, juce::dontSendNotification);
                powerButton.setTitle ((slot.enabled ? "Turn off " : "Turn on ") + slot.name);
                powerButton.setTooltip (slot.enabled ? "On (click to bypass)" : "Off (click to turn on)");

                if (tuneControls != nullptr)
                    resized();   // Tune's header controls start after the name, which just changed

                repaint();
            }
            break;
        }
    }

    auto params = tracks.effects().getEffectParameters (trackId, pluginId);

    for (int i = 0; i < knobs.size() && i < (int) params.size(); ++i)
        knobs[i]->setValue (params[(size_t) i].value, params[(size_t) i].valueText);

    if (tuneControls != nullptr)
        tuneControls->refresh();
}

void PluginEditor::timerCallback()
{
    // If the effect was removed (e.g. by Undo) while we're showing it, close.
    // (Look for it in the chain: an effect with no settings is still there.)
    auto chain = engine.getTracks().effects().getEffectChain (trackId);

    if (std::none_of (chain.begin(), chain.end(), [this] (const PluginSlot& s) { return s.id == pluginId; }))
    {
        stopTimer();

        if (onClose)
            onClose();

        return;
    }

    refreshValues();
}

int PluginEditor::columnsFor (int width) const
{
    return juce::jmax (1, (width - padding * 2 + gap) / (Knob::preferredWidth + gap));
}

int PluginEditor::knobsHeight (int width) const
{
    const int rows = juce::jmax (1, (knobs.size() + columnsFor (width) - 1) / columnsFor (width));
    return rows * Knob::preferredHeight + (rows - 1) * gap;
}

int PluginEditor::getIdealHeight (int width) const
{
    if (tuneControls != nullptr)
        return headerHeight + padding + TuneControls::getBodyHeight() + padding;

    return headerHeight + padding + knobsHeight (width) + padding;
}

juce::String PluginEditor::getTitleText() const
{
    // Tune's panel follows its design: the name in spaced capitals.
    const auto name = tuneControls != nullptr ? slot.name.toUpperCase() : slot.name;
    return name + (slot.enabled ? "" : " (off)");
}

juce::Font PluginEditor::getTitleFont() const
{
    return tuneControls != nullptr ? fonts::uiBold (textSize::xl).withExtraKerningFactor (0.12f) : fonts::uiBold (textSize::xl);
}

void PluginEditor::paint (juce::Graphics& g)
{
    auto header = getLocalBounds().removeFromTop (headerHeight).withTrimmedLeft (padding + 32);

    g.setColour (slot.enabled ? theme().text : theme().textSecondary);
    g.setFont (getTitleFont());
    g.drawText (getTitleText(), header, juce::Justification::centredLeft, true);

    if (knobs.isEmpty())
    {
        g.setColour (theme().textSecondary);
        g.setFont (fonts::ui (textSize::md));
        g.drawText ("This effect has no settings.", getLocalBounds().withTrimmedTop (headerHeight).reduced (padding),
                    juce::Justification::topLeft);
    }
}

void PluginEditor::resized()
{
    auto header = getLocalBounds().removeFromTop (headerHeight).reduced (padding, 0).withTrimmedTop (10);
    powerButton.setBounds (header.removeFromLeft (24).withSizeKeepingCentre (24, 24));
    doneButton.setBounds (header.removeFromRight (76).withSizeKeepingCentre (76, 30));

    const int left = padding, top = headerHeight + padding;

    if (tuneControls != nullptr)
    {
        // Tune's panel (see TuneControls): the header's free part, from after
        // the name to the panel's right edge (and short of Done), and the
        // body under it, at most the panel's width.
        const int panelRight = juce::jmin (left + TuneControls::panelWidth, doneButton.getX() - 16);
        const int titleRight = padding + 32 + juce::GlyphArrangement::getStringWidthInt (getTitleFont(), getTitleText()) + 16;
        const auto headerFree = juce::Rectangle<int>::leftTopRightBottom (titleRight, header.getY(), juce::jmax (titleRight, panelRight),
                                                                          header.getBottom());
        const auto body = juce::Rectangle<int>::leftTopRightBottom (left, top, juce::jmax (left + 1, panelRight),
                                                                    top + TuneControls::getBodyHeight());

        tuneControls->setBounds (getLocalBounds());
        const auto knobArea = tuneControls->layOut (headerFree, body);

        for (int i = 0; i < knobs.size(); ++i)
            knobs[i]->setBounds (knobArea.getX() + i * (Knob::preferredWidth + gap), knobArea.getY(),
                                 Knob::preferredWidth, Knob::preferredHeight);

        return;
    }

    const int cols = columnsFor (getWidth());

    for (int i = 0; i < knobs.size(); ++i)
        knobs[i]->setBounds (left + (i % cols) * (Knob::preferredWidth + gap),
                             top + (i / cols) * (Knob::preferredHeight + gap),
                             Knob::preferredWidth, Knob::preferredHeight);
}

} // namespace ikaros
