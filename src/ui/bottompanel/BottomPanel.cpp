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
                engine.getTracks().effects().setSoundControl (selection.getTrack(), i, v);
                refreshKnobText();
            };
            addAndMakeVisible (knob);
        }

        addAndMakeVisible (keyboard);

        auto colours = IconButton::raisedColours();
        colours.border = colour::border;
        openWindowButton.setColours (colours);
        openWindowButton.setFont (fonts::uiSemiBold (13.0f));
        openWindowButton.setIconSize (16.0f);
        openWindowButton.onClick = [this]
        {
            if (instrument.has_value())
                engine.getTracks().effects().showPluginWindow (selection.getTrack(), instrument->id);
        };
        addChildComponent (openWindowButton);
    }

    void refresh (const std::optional<TrackInfo>& track, bool showDetails)
    {
        detailsVisible = showDetails;
        auto id = selection.getTrack();
        auto& tc = engine.getTracks();
        auto arc = track.has_value() ? track->colour : colour::accent;

        // Audio tracks have no synth: no knobs or keyboard, just a pointer
        // to where their settings are.
        isAudioTrack = track.has_value() && track->kind == TrackKind::audio;
        keyboard.setVisible (! isAudioTrack);

        auto controls = tc.effects().getSoundControls (id);
        instrument = isAudioTrack ? std::nullopt : tc.effects().getInstrument (id);
        soundName = track.has_value() ? track->soundName : juce::String();

        // Only the built-in synth has the five knobs. Other instruments get a
        // card explaining where their controls are.
        hasKnobs = ! controls.empty();

        for (auto* k : knobs)
            k->setVisible (hasKnobs);

        openWindowButton.setVisible (! hasKnobs && instrument.has_value() && instrument->isThirdParty && ! instrument->missing);

        for (int i = 0; i < knobs.size() && i < (int) controls.size(); ++i)
        {
            knobs[i]->setName (controls[(size_t) i].name);
            knobs[i]->setValue (controls[(size_t) i].value, controls[(size_t) i].valueText);
            knobs[i]->setArcColour (arc);
            knobs[i]->setEnabled (track.has_value());
        }

        details = tc.getDetails (id);
        resized();
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        if (! isAudioTrack && ! hasKnobs)
        {
            auto r = cardArea;
            g.setColour (colour::cardBg);
            g.fillRoundedRectangle (r.toFloat(), 12.0f);

            auto text = r.reduced (20, 16);
            const auto name = ! instrument.has_value() ? juce::String ("No instrument")
                            : instrument->isThirdParty ? instrument->name
                                                       : soundName;   // e.g. "Soft Piano" rather than "Sampler"
            g.setColour (colour::text);
            g.setFont (fonts::uiBold (15.0f));
            g.drawText (name, text.removeFromTop (24), juce::Justification::centredLeft, true);
            g.setColour (colour::textSecondary);
            g.setFont (fonts::ui (13.0f));

            juce::String message;

            if (! instrument.has_value())
                message = "Pick a sound in the library on the left.";
            else if (instrument->missing)
                message = instrument->name + " isn't installed on this computer. Its settings are kept, so it will "
                          "work again once it's installed. Or pick another sound in the library.";
            else if (instrument->isThirdParty)
                message = "This plug-in has its own controls. Open its window to change the sound.";
            else
                message = "A sampled instrument. Shape its sound with effects under Plug-ins on the left, "
                          "or pick another sound in the library.";

            g.drawFittedText (message, text.withTrimmedBottom (openWindowButton.isVisible() ? 40 : 0),
                              juce::Justification::topLeft, 3, 1.0f);
        }

        if (isAudioTrack)
        {
            auto r = getLocalBounds().reduced (18, 14).withWidth (5 * (Knob::preferredWidth + 14));
            g.setColour (colour::cardBg);
            g.fillRoundedRectangle (r.withHeight (Knob::preferredHeight).toFloat(), 12.0f);

            auto text = r.withHeight (Knob::preferredHeight).reduced (20, 16);
            g.setColour (colour::text);
            g.setFont (fonts::uiBold (15.0f));
            g.drawText ("Audio track", text.removeFromTop (24), juce::Justification::centredLeft);
            g.setColour (colour::textSecondary);
            g.setFont (fonts::ui (13.0f));
            g.drawFittedText ("Choose the input and monitoring under Recording on the left, and shape the sound "
                              "with Plug-ins. Press R or the Record button to record.",
                              text, juce::Justification::topLeft, 3, 1.0f);
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
            g.drawText ((isAudioTrack ? "Input: " : "MIDI input: ") + midi, r.removeFromTop (20), juce::Justification::centredLeft, true);
        }
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (18, 14);

        // A taller panel gives the keyboard longer keys, up to a point.
        keyboard.setBounds (r.removeFromBottom (juce::jlimit (92, 150, r.getHeight() - Knob::preferredHeight - 14)));
        r.removeFromBottom (14);

        auto top = r;
        cardArea = top.withWidth (juce::jmin (top.getWidth(), 5 * (Knob::preferredWidth + 14))).withHeight (Knob::preferredHeight);
        openWindowButton.setBounds (cardArea.reduced (20, 14).removeFromBottom (32).removeFromLeft (190));

        for (auto* k : knobs)
        {
            k->setBounds (top.removeFromLeft (Knob::preferredWidth).withHeight (Knob::preferredHeight));
            top.removeFromLeft (14);
        }

        top.removeFromLeft (20);
        detailsArea = top.withHeight (112);
    }

private:
    AudioEngine& engine;
    Selection& selection;
    juce::OwnedArray<Knob> knobs;
    PianoKeyboard keyboard;

    TrackDetails details;
    bool detailsVisible = false;
    bool isAudioTrack = false;
    bool hasKnobs = true;
    std::optional<PluginSlot> instrument;
    juce::String soundName;
    IconButton openWindowButton { "Open plug-in window", Icon::openWindow, "Open Plug-in Window" };
    juce::Rectangle<int> detailsArea, cardArea;

    void refreshKnobText()
    {
        auto controls = engine.getTracks().effects().getSoundControls (selection.getTrack());

        for (int i = 0; i < knobs.size() && i < (int) controls.size(); ++i)
            knobs[i]->setValue (controls[(size_t) i].value, controls[(size_t) i].valueText);
    }
};

//==============================================================================
BottomPanel::BottomPanel (AudioEngine& e, Selection& s)
    : engine (e), selection (s), inspector (e, s), mixer (e, s)
{
    setTitle ("Track controls");
    addAndMakeVisible (inspector);
    inspector.onOpenEffect = [this] (PluginId id) { openEffect (id); };

    effectViewport.setScrollBarsShown (true, false);
    addChildComponent (effectViewport);

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

    collapseButton.setColours (IconButton::ghostColours());
    collapseButton.setTooltip ("Hide panel");
    collapseButton.onClick = [this] { if (onCollapseRequested) onCollapseRequested (! collapsed); };
    addAndMakeVisible (collapseButton);

    updateTabButtons();
    refresh();
}

BottomPanel::~BottomPanel() = default;

void BottomPanel::setTab (Tab t)
{
    tab = t;

    if (isShowingEffect())
        closeEffect();   // picking a tab leaves the effect settings

    showTabPage();
    refresh();
}

void BottomPanel::showTabPage()
{
    const bool effect = isShowingEffect();
    soundPage->setVisible (! collapsed && ! effect && tab == Tab::sound);
    pianoRoll.setVisible (! collapsed && ! effect && tab == Tab::editor);
    mixer.setVisible (! collapsed && ! effect && tab == Tab::mixer);
    effectViewport.setVisible (! collapsed && effect);
    updateTabButtons();
}

void BottomPanel::setCollapsed (bool shouldCollapse)
{
    if (collapsed == shouldCollapse)
        return;

    collapsed = shouldCollapse;
    const auto label = collapsed ? juce::String ("Show panel") : juce::String ("Hide panel");
    collapseButton.setIcon (collapsed ? Icon::chevronUp : Icon::chevronDown);
    collapseButton.setTitle (label);
    collapseButton.setTooltip (label);

    for (auto* c : std::initializer_list<juce::Component*> { &inspector, &detailsButton, &soundTab, &editorTab, &mixerTab })
        c->setVisible (! collapsed);

    // Collapsed, the whole strip is a button that opens the panel.
    setMouseCursor (collapsed ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
    showTabPage();
    resized();
    refresh();   // the mixer only refreshes while it's visible
    repaint();
}

//==============================================================================
void BottomPanel::openEffect (PluginId id)
{
    auto t = engine.getTracks().getTrack (selection.getTrack());

    if (! t.has_value())
        return;

    if (isShowingEffect() && effectEditor->getPluginId() == id)
        return;

    effectViewport.setViewedComponent (nullptr, false);
    effectEditor = std::make_unique<PluginEditor> (engine, t->id, id, t->colour);

    // onClose runs from inside the editor (its Done button or its timer), so
    // the editor can't be deleted right there; close on the next message-loop
    // turn instead. SafePointer guards against this panel going away first.
    effectEditor->onClose = [safeThis = SafePointer<BottomPanel> (this), id]
    {
        juce::MessageManager::callAsync ([safeThis, id]
        {
            if (safeThis != nullptr && safeThis->isShowingEffect() && safeThis->effectEditor->getPluginId() == id)
                safeThis->closeEffect();
        });
    };

    effectViewport.setViewedComponent (effectEditor.get(), false);
    inspector.setOpenEffect (id);
    showTabPage();
    layoutEffectEditor();

    if (onViewStateChanged)
        onViewStateChanged();
}

void BottomPanel::closeEffect()
{
    effectViewport.setViewedComponent (nullptr, false);
    effectEditor.reset();
    inspector.setOpenEffect (0);
    showTabPage();

    if (onViewStateChanged)
        onViewStateChanged();
}

void BottomPanel::layoutEffectEditor()
{
    effectViewport.setBounds (contentArea);

    if (effectEditor != nullptr)
    {
        const int w = effectViewport.getMaximumVisibleWidth();
        effectEditor->setSize (w, juce::jmax (effectEditor->getIdealHeight (w), effectViewport.getMaximumVisibleHeight()));
    }
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
    // While an effect's settings are showing, no tab is highlighted.
    const bool effect = isShowingEffect();
    soundTab.setToggleState (! effect && tab == Tab::sound, juce::dontSendNotification);
    editorTab.setToggleState (! effect && tab == Tab::editor, juce::dontSendNotification);
    mixerTab.setToggleState (! effect && tab == Tab::mixer, juce::dontSendNotification);
}

void BottomPanel::refresh()
{
    auto& tc = engine.getTracks();
    track = tc.getTrack (selection.getTrack());

    // Switching tracks leaves the previous track's effect settings.
    if (isShowingEffect() && effectEditor->getTrackId() != selection.getTrack())
        closeEffect();

    inspector.refresh();
    soundPage->refresh (track, detailsVisible);

    // Editor: the selected region, or else the selected track's first MIDI region.
    std::optional<RegionInfo> region;

    if (selection.getRegion() != 0)
        region = tc.regions().getRegion (selection.getRegion());

    if (! region.has_value() && track.has_value())
        for (auto& r : tc.regions().getRegions (track->id))
            if (r.isMidi) { region = r; break; }

    pianoRoll.setRegion (region, track.has_value() ? track->colour : colour::accent, engine.beatsPerBar());

    if (mixer.isVisible())
        mixer.refresh();

    repaint (headerArea);
}

void BottomPanel::update()
{
    if (collapsed)
        return;

    inspector.update();

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

    if (! collapsed)
    {
        g.setColour (colour::wellBg);
        g.fillRect (inspector.getRight(), headerHeight, getWidth() - inspector.getRight(), 1);
    }

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
    if (! collapsed)
    {
        g.setColour (colour::border);
        g.drawRoundedRectangle (tabsArea.toFloat().expanded (0.5f), 9.0f, 1.0f);
    }
}

void BottomPanel::mouseUp (const juce::MouseEvent& e)
{
    if (collapsed && e.mouseWasClicked() && ! e.mods.isPopupMenu() && onCollapseRequested)
        onCollapseRequested (false);
}

void BottomPanel::resized()
{
    auto r = getLocalBounds();

    if (collapsed)
    {
        // The slim strip: track name on the left, the show button on the right.
        auto strip = r.withTrimmedTop (1).reduced (14, 0);
        collapseButton.setBounds (strip.removeFromRight (26).withSizeKeepingCentre (26, 26));
        headerArea = strip.withTrimmedRight (12);
        return;
    }

    // Track inspector: full-height column on the left, visible on every tab.
    inspector.setBounds (r.removeFromLeft (TrackInspector::preferredWidth).withTrimmedTop (1));

    auto header = r.removeFromTop (headerHeight).withTrimmedTop (1).reduced (14, 8);

    tabsArea = header.withSizeKeepingCentre (tabWidth * 3, 32);
    auto t = tabsArea;
    soundTab.setBounds (t.removeFromLeft (tabWidth));
    editorTab.setBounds (t.removeFromLeft (tabWidth));
    mixerTab.setBounds (t);

    collapseButton.setBounds (header.removeFromRight (32).withSizeKeepingCentre (32, 32));
    header.removeFromRight (8);
    detailsButton.setBounds (header.removeFromRight (120).withSizeKeepingCentre (120, 32));
    headerArea = header.withRight (tabsArea.getX() - 12);

    r.removeFromTop (1);
    contentArea = r;
    soundPage->setBounds (r);
    pianoRoll.setBounds (r);
    mixer.setBounds (r);
    layoutEffectEditor();
}

} // namespace ikaros
