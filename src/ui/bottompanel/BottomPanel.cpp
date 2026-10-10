#include "ui/bottompanel/BottomPanel.h"
#include "ui/widgets/Knob.h"
#include "ui/bottompanel/PianoKeyboard.h"
#include "ui/theme/Fonts.h"
#include "ui/theme/Tokens.h"

namespace ikaros
{

using namespace tokens;

namespace
{
    constexpr int headerHeight = 49;
    constexpr int tabWidth = 76;
}

//==============================================================================
// The "Sound" tab.
class BottomPanel::SoundPage  : public juce::Component
{
public:
    SoundPage (AudioEngine& e, Selection& s) : engine (e), selection (s), keyboard (e)
    {
        for (int i = 0; i < 5; ++i)
        {
            auto* knob = knobs.add (new Knob());
            knob->onDragStart = [this, knob] { engine.beginUndoStep (knob->getName()); };
            knob->onValueChange = [this, i] (float v)
            {
                engine.getTracks().setSoundControl (selection.getTrack(), i, v);
                refreshKnobText();
            };
            addAndMakeVisible (knob);
        }

        for (int i = 0; i < 3; ++i)
        {
            auto* pill = effectPills.add (new IconButton ("Effect", std::nullopt, "OFF"));
            pill->setColours ({ colour::controlFill, colour::textSecondary, colour::effectOn, colour::textOnAccent });
            pill->setCornerRadius (6.0f);
            pill->setFont (fonts::uiBold (11.0f));
            pill->onClick = [this, i, pill]
            {
                engine.getTracks().setEffectEnabled (selection.getTrack(), i, ! pill->getToggleState());
            };
            addAndMakeVisible (pill);
        }

        addAndMakeVisible (keyboard);
    }

    void refresh (const std::optional<TrackInfo>& track, bool showDetails)
    {
        detailsVisible = showDetails;
        auto id = selection.getTrack();
        auto& tc = engine.getTracks();
        auto arc = track.has_value() ? track->colour : colour::accent;

        auto controls = tc.getSoundControls (id);

        for (int i = 0; i < knobs.size() && i < (int) controls.size(); ++i)
        {
            knobs[i]->setName (controls[(size_t) i].name);
            knobs[i]->setValue (controls[(size_t) i].value, controls[(size_t) i].valueText);
            knobs[i]->setArcColour (arc);
            knobs[i]->setEnabled (track.has_value());
        }

        effects = tc.getEffects (id);

        for (int i = 0; i < effectPills.size() && i < (int) effects.size(); ++i)
        {
            auto& fx = effects[(size_t) i];
            auto* pill = effectPills[i];
            pill->setToggleState (fx.enabled, juce::dontSendNotification);
            pill->setText (fx.enabled ? "ON" : "OFF");
            pill->setTitle (fx.name + (fx.enabled ? " on" : " off"));
            pill->setTooltip ("Turn " + fx.name + (fx.enabled ? " off" : " on"));
            pill->setEnabled (track.has_value());
        }

        details = tc.getDetails (id);
        resized();
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        // Effects card
        g.setColour (colour::textSecondary);
        g.setFont (fonts::uiBold (11.0f).withExtraKerningFactor (0.1f));
        g.drawText ("EFFECTS", effectsArea.withHeight (14), juce::Justification::centredLeft);

        for (int i = 0; i < (int) effects.size() && i < effectRows.size(); ++i)
        {
            auto row = effectRows.getReference (i);
            g.setColour (colour::cardBg);
            g.fillRoundedRectangle (row.toFloat(), size::radius);
            g.setColour (colour::text);
            g.setFont (fonts::ui (14.0f));
            g.drawText (effects[(size_t) i].name, row.reduced (10, 0), juce::Justification::centredLeft);
        }

        if (detailsVisible)
        {
            auto r = detailsArea;
            g.setColour (colour::cardBg);
            g.fillRoundedRectangle (r.toFloat(), size::radiusLarge);
            r.reduce (12, 10);

            g.setColour (colour::text);
            g.setFont (fonts::uiBold (13.0f));
            g.drawText ("Details", r.removeFromTop (20), juce::Justification::centredLeft);

            g.setColour (colour::textSecondary);
            g.setFont (fonts::ui (13.0f));
            auto midi = details.midiInputs.isEmpty() ? juce::String ("No MIDI keyboard connected")
                                                     : details.midiInputs.joinIntoString (", ");
            g.drawText ("Plugin: " + details.pluginName, r.removeFromTop (20), juce::Justification::centredLeft, true);
            g.drawText ("Output: " + details.outputName, r.removeFromTop (20), juce::Justification::centredLeft, true);
            g.drawText ("MIDI input: " + midi, r.removeFromTop (20), juce::Justification::centredLeft, true);
            // TODO(milestone-3): "Open plugin window" for hosted VST3/AU instruments.
        }
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (18, 14);

        keyboard.setBounds (r.removeFromBottom (92));
        r.removeFromBottom (14);

        auto top = r;

        for (auto* k : knobs)
        {
            k->setBounds (top.removeFromLeft (Knob::preferredWidth).withHeight (Knob::preferredHeight));
            top.removeFromLeft (14);
        }

        top.removeFromLeft (6);
        effectsArea = top.removeFromLeft (220);
        auto rows = effectsArea.withTrimmedTop (22);
        effectRows.clearQuick();

        for (auto* pill : effectPills)
        {
            auto row = rows.removeFromTop (30);
            rows.removeFromTop (6);
            effectRows.add (row);
            pill->setBounds (row.reduced (6, 3).removeFromRight (48));
        }

        top.removeFromLeft (20);
        detailsArea = top.withHeight (112);
    }

private:
    AudioEngine& engine;
    Selection& selection;
    juce::OwnedArray<Knob> knobs;
    juce::OwnedArray<IconButton> effectPills;
    PianoKeyboard keyboard;

    std::vector<EffectInfo> effects;
    TrackDetails details;
    bool detailsVisible = false;
    juce::Rectangle<int> effectsArea, detailsArea;
    juce::Array<juce::Rectangle<int>> effectRows;

    void refreshKnobText()
    {
        auto controls = engine.getTracks().getSoundControls (selection.getTrack());

        for (int i = 0; i < knobs.size() && i < (int) controls.size(); ++i)
            knobs[i]->setValue (controls[(size_t) i].value, controls[(size_t) i].valueText);
    }
};

//==============================================================================
BottomPanel::BottomPanel (AudioEngine& e, Selection& s)
    : engine (e), selection (s), mixer (e, s)
{
    setTitle ("Track controls");

    soundPage = std::make_unique<SoundPage> (engine, selection);
    addAndMakeVisible (*soundPage);
    addChildComponent (pianoRoll);
    addChildComponent (mixer);

    auto tabColours = IconButton::Colours { juce::Colour (0xff15181d), colour::text, colour::accent, colour::textOnAccent };

    int index = 0;

    for (auto* b : { &soundTab, &editorTab, &mixerTab })
    {
        b->setColours (tabColours);
        b->setCornerRadius (0.0f);
        b->setFont (fonts::uiBold (13.0f));
        b->setRadioGroupId (1);   // radio group: turning one on turns the others off
        b->onClick = [this, index] { setTab ((Tab) index); if (onViewStateChanged) onViewStateChanged(); };
        addAndMakeVisible (b);
        ++index;
    }

    auto detailsColours = IconButton::raisedColours();
    detailsColours.border = colour::border;
    detailsColours.backgroundOn = colour::raised;
    detailsColours.foregroundOn = colour::text;
    detailsButton.setColours (detailsColours);
    detailsButton.setFont (fonts::ui (13.0f));
    detailsButton.onClick = [this]
    {
        setDetailsVisible (! detailsVisible);
        if (onViewStateChanged) onViewStateChanged();
    };
    addAndMakeVisible (detailsButton);

    updateTabButtons();
    refresh();
}

BottomPanel::~BottomPanel() = default;

void BottomPanel::setTab (Tab t)
{
    tab = t;
    soundPage->setVisible (tab == Tab::sound);
    pianoRoll.setVisible (tab == Tab::editor);
    mixer.setVisible (tab == Tab::mixer);
    updateTabButtons();
    refresh();
}

void BottomPanel::setDetailsVisible (bool shouldShow)
{
    detailsVisible = shouldShow;
    auto label = detailsVisible ? juce::String ("Hide details") : juce::String ("Show details");
    detailsButton.setText (label);
    detailsButton.setTitle (label);
    detailsButton.setToggleState (detailsVisible, juce::dontSendNotification);
    refresh();
}

void BottomPanel::updateTabButtons()
{
    soundTab.setToggleState (tab == Tab::sound, juce::dontSendNotification);
    editorTab.setToggleState (tab == Tab::editor, juce::dontSendNotification);
    mixerTab.setToggleState (tab == Tab::mixer, juce::dontSendNotification);
}

void BottomPanel::refresh()
{
    auto& tc = engine.getTracks();
    track = tc.getTrack (selection.getTrack());

    soundPage->refresh (track, detailsVisible);

    // Editor: the selected region, or else the selected track's first MIDI region.
    std::optional<RegionInfo> region;

    if (selection.getRegion() != 0)
        region = tc.getRegion (selection.getRegion());

    if (! region.has_value() && track.has_value())
        for (auto& r : tc.getRegions (track->id))
            if (r.isMidi) { region = r; break; }

    pianoRoll.setRegion (region, track.has_value() ? track->colour : colour::accent, engine.beatsPerBar());

    if (mixer.isVisible())
        mixer.refresh();

    repaint (headerArea);
}

void BottomPanel::update()
{
    if (tab == Tab::mixer)
        mixer.updateMeters();
    else if (tab == Tab::editor)
        pianoRoll.setPlayheadBeat (engine.getTransportState().positionBeats);
}

void BottomPanel::paint (juce::Graphics& g)
{
    g.fillAll (colour::toolbarBg);
    g.setColour (colour::divider);
    g.fillRect (getLocalBounds().removeFromTop (1));
    g.setColour (colour::wellBg);
    g.fillRect (0, headerHeight, getWidth(), 1);

    auto r = headerArea;

    if (track.has_value())
    {
        g.setColour (track->colour);
        g.fillRoundedRectangle (r.removeFromLeft (10).withSizeKeepingCentre (10, 10).toFloat(), 3.0f);
        r.removeFromLeft (8);

        g.setColour (colour::text);
        g.setFont (fonts::uiBold (14.0f));
        auto nameWidth = (int) std::ceil (juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), track->name));
        g.drawText (track->name, r.removeFromLeft (nameWidth + 2), juce::Justification::centredLeft);
        r.removeFromLeft (8);

        g.setColour (colour::textSecondary);
        g.setFont (fonts::ui (14.0f));
        g.drawText (track->soundName, r, juce::Justification::centredLeft, true);
    }
    else
    {
        g.setColour (colour::textSecondary);
        g.setFont (fonts::ui (14.0f));
        g.drawText ("No track selected", r, juce::Justification::centredLeft);
    }

    // Outline around the segmented control.
    g.setColour (colour::border);
    g.drawRoundedRectangle (tabsArea.toFloat().expanded (0.5f), 9.0f, 1.0f);
}

void BottomPanel::resized()
{
    auto r = getLocalBounds();
    auto header = r.removeFromTop (headerHeight).withTrimmedTop (1).reduced (14, 8);

    tabsArea = header.withSizeKeepingCentre (tabWidth * 3, 32);
    auto t = tabsArea;
    soundTab.setBounds (t.removeFromLeft (tabWidth));
    editorTab.setBounds (t.removeFromLeft (tabWidth));
    mixerTab.setBounds (t);

    detailsButton.setBounds (header.removeFromRight (120).withSizeKeepingCentre (120, 32));
    headerArea = header.withRight (tabsArea.getX() - 12);

    r.removeFromTop (1);
    soundPage->setBounds (r);
    pianoRoll.setBounds (r);
    mixer.setBounds (r);
}

} // namespace ikaros
