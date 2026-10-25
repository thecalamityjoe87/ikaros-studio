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
                resized();   // the box fits the name, and Tune's Key starts after it
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

int PluginEditor::columnsFor (int innerWidth) const
{
    return juce::jmax (1, (innerWidth + gap) / (Knob::preferredWidth + gap));
}

int PluginEditor::getTitleWidth() const
{
    return powerSize + titleGap + juce::GlyphArrangement::getStringWidthInt (getTitleFont(), getTitleText());
}

juce::Rectangle<int> PluginEditor::getBoxBounds (int width) const
{
    // The box reaches at most to just short of Done. It's as wide as its
    // contents need (Tune's as wide as its design) and as tall.
    const int maxInner = juce::jmax (1, width - padding - doneWidth - 16 - padding - boxPadding * 2);
    int inner = 0, contentHeight = 0;

    if (tuneControls != nullptr)
    {
        inner = TuneControls::panelWidth - boxPadding * 2;
        contentHeight = TuneControls::getBodyHeight();
    }
    else if (knobs.isEmpty())
    {
        inner = juce::jmax (getTitleWidth(), juce::GlyphArrangement::getStringWidthInt (fonts::ui (textSize::md), noSettingsText));
        contentHeight = 20;
    }
    else
    {
        const int cols = juce::jmin (columnsFor (maxInner), knobs.size());
        const int rows = (knobs.size() + cols - 1) / cols;
        inner = juce::jmax (getTitleWidth(), cols * Knob::preferredWidth + (cols - 1) * gap);
        contentHeight = rows * Knob::preferredHeight + (rows - 1) * gap;
    }

    return { padding, padding, juce::jmin (inner, maxInner) + boxPadding * 2,
             boxPadding * 2 + titleRowHeight + gap + contentHeight };
}

int PluginEditor::getIdealHeight (int width) const
{
    return getBoxBounds (width).getBottom() + padding;
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
    // The box everything but Done sits in.
    const auto b = box.toFloat();
    g.setColour (theme().panelBoxBg);
    g.fillRoundedRectangle (b, size::radiusCard);
    g.setColour (theme().border);
    g.drawRoundedRectangle (b.reduced (0.5f), size::radiusCard, 1.0f);

    g.setColour (slot.enabled ? theme().text : theme().textSecondary);
    g.setFont (getTitleFont());
    g.drawText (getTitleText(), titleArea, juce::Justification::centredLeft, true);

    if (knobs.isEmpty())
    {
        g.setColour (theme().textSecondary);
        g.setFont (fonts::ui (textSize::md));
        g.drawText (noSettingsText, contentArea, juce::Justification::topLeft);
    }
}

void PluginEditor::resized()
{
    // In the box: on/off and the name along the top, the knobs (or Tune's
    // panel) under them. Done sits outside it, level with the name.
    box = getBoxBounds (getWidth());
    contentArea = box.reduced (boxPadding);
    auto titleRow = contentArea.removeFromTop (titleRowHeight);
    contentArea.removeFromTop (gap);

    powerButton.setBounds (titleRow.removeFromLeft (powerSize).withSizeKeepingCentre (powerSize, powerSize));
    titleRow.removeFromLeft (titleGap);
    titleArea = titleRow;
    doneButton.setBounds (juce::Rectangle<int> (getWidth() - padding - doneWidth, 0, doneWidth, 30)
                              .withY (titleRow.getCentreY() - 15));

    if (tuneControls != nullptr)
    {
        // Tune's panel (see TuneControls): Key and Lock to Key in the rest
        // of the top row, after the name; the rest under it.
        const int titleRight = juce::jmin (titleRow.getX() + juce::GlyphArrangement::getStringWidthInt (getTitleFont(), getTitleText()) + 16,
                                           titleRow.getRight());
        tuneControls->setBounds (getLocalBounds());
        const auto knobArea = tuneControls->layOut (titleRow.withLeft (titleRight), contentArea);

        for (int i = 0; i < knobs.size(); ++i)
            knobs[i]->setBounds (knobArea.getX() + i * (Knob::preferredWidth + gap), knobArea.getY(),
                                 Knob::preferredWidth, Knob::preferredHeight);

        return;
    }

    const int cols = columnsFor (contentArea.getWidth());

    for (int i = 0; i < knobs.size(); ++i)
        knobs[i]->setBounds (contentArea.getX() + (i % cols) * (Knob::preferredWidth + gap),
                             contentArea.getY() + (i / cols) * (Knob::preferredHeight + gap),
                             Knob::preferredWidth, Knob::preferredHeight);
}

} // namespace ikaros
