#include "ui/bottompanel/BottomPanel.h"
#include "ui/widgets/AutomationHint.h"
#include "ui/widgets/Knob.h"
#include "ui/bottompanel/PianoKeyboard.h"
#include "ui/widgets/LevelMeter.h"
#include "ui/theme/Fonts.h"
#include "ui/theme/Tokens.h"

namespace ikaros
{

using namespace tokens;

namespace
{
    constexpr int headerHeight = 49;
    constexpr int tabWidth = 76;
    constexpr int modeWidth = 70;      // the Track | Master switch
    constexpr int nameHeight = 34;     // the selected track's name, under the switch
    constexpr int segmentInset = 3;    // a segmented control's pills sit this far inside its outline
    constexpr float segmentRadius = 7.0f, segmentOutlineRadius = 10.0f;
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

            // Automated, it moves with the automation, and a grab only moves
            // it until it's let go (see followAutomation).
            const auto target = automationTargets::soundControl (i);
            followAutomation (knob->getSlider(), [this, target] { return automated.contains (target); },
                              [this, knob, target] { showAutomationHint (engine, *knob, selection.getTrack(), target); });
            addAndMakeVisible (knob);
        }

        addAndMakeVisible (keyboard);

        auto colours = IconButton::raisedColours();
        colours.border = colour::border;
        openWindowButton.setColours (colours);
        openWindowButton.setFont (fonts::uiSemiBold (textSize::md));
        openWindowButton.setIconSize (16.0f);
        openWindowButton.onClick = [this]
        {
            if (instrument.has_value())
                engine.getTracks().effects().showPluginWindow (selection.getTrack(), instrument->id);
        };
        addChildComponent (openWindowButton);

        downloadButton.setColours (IconButton::primaryColours());
        downloadButton.setFont (fonts::uiSemiBold (textSize::md));
        downloadButton.onClick = [this]
        {
            if (instrument.has_value() && onDownloadPack)
                onDownloadPack (instrument->missingPackId);
        };
        addChildComponent (downloadButton);
    }

    /** The Download button on a track whose sound pack isn't installed. */
    std::function<void (const juce::String& packId)> onDownloadPack;

    void refresh (const std::optional<TrackInfo>& track, bool showDetails)
    {
        detailsVisible = showDetails;
        auto id = selection.getTrack();
        auto& tc = engine.getTracks();
        auto arc = track.has_value() ? track->colour : colour::master;

        // Audio tracks have no synth: no knobs or keyboard, just a pointer
        // to where their settings are.
        isAudioTrack = track.has_value() && track->kind == TrackKind::audio;
        keyboard.setVisible (! isAudioTrack);

        const auto soundKey = juce::String ((juce::int64) id) + "|" + (track.has_value() ? track->soundId : juce::String());

        if (soundKey != shownSound)
        {
            shownSound = soundKey;
            rangePending = ! isAudioTrack;
        }

        auto controls = tc.effects().getSoundControls (id);
        automated = track.has_value() ? track->automated : juce::StringArray();
        instrument = isAudioTrack ? std::nullopt : tc.effects().getInstrument (id);
        soundName = track.has_value() ? track->soundName : juce::String();

        // The built-in synth and sampled instruments have the five knobs.
        // Plug-in instruments get a card with a button for their own window.
        hasKnobs = ! controls.empty();

        // Most sounds have five knobs; some (drum kits) have fewer.
        for (int i = 0; i < knobs.size(); ++i)
            knobs[i]->setVisible (hasKnobs && i < (int) controls.size());

        shownKnobs = hasKnobs ? juce::jmin (knobs.size(), (int) controls.size()) : knobs.size();

        openWindowButton.setVisible (! hasKnobs && instrument.has_value() && instrument->isThirdParty && ! instrument->missing);
        downloadButton.setVisible (! hasKnobs && instrument.has_value() && instrument->missingPackId.isNotEmpty());

        for (int i = 0; i < knobs.size() && i < (int) controls.size(); ++i)
        {
            knobs[i]->setName (controls[(size_t) i].name);
            knobs[i]->setValue (controls[(size_t) i].value, controls[(size_t) i].valueText);
            knobs[i]->setArcColour (arc);
            knobs[i]->setEnabled (track.has_value());
            setAutomatedLook (knobs[i]->getSlider(), automated.contains (automationTargets::soundControl (i)));
        }

        details = tc.getDetails (id);
        resized();
        repaint();
    }

    // ~30 Hz. A newly chosen sampled instrument loads in the background (big ones take a while, and
    // are silent until then): say so, and once it has loaded, move the keyboard to the keys it
    // plays, so the first key a user presses makes a sound.
    void update()
    {
        auto id = selection.getTrack();
        auto& effects = engine.getTracks().effects();
        const bool nowLoading = ! isAudioTrack && ! effects.isInstrumentReady (id);

        if (nowLoading || loading)
        {
            loading = nowLoading;
            ++loadingTicks;
            repaint (keyboard.getBounds());
        }

        if (! rangePending)
            return;

        if (auto range = effects.getInstrumentKeyRange (id))
        {
            keyboard.showRange (range->first, range->second);
            rangePending = false;
        }
        else if (effects.isInstrumentReady (id))
        {
            rangePending = false;   // not a sampled instrument, or it plays nothing
        }
    }

    void paintOverChildren (juce::Graphics& g) override
    {
        if (! loading || ! keyboard.isVisible())
            return;

        auto area = keyboard.getBounds().toFloat();
        g.setColour (colour::toolbarBg.withAlpha (0.82f));
        g.fillRoundedRectangle (area, size::radius);

        const auto dots = juce::String ("...").substring (0, 1 + (loadingTicks / 10) % 3);
        g.setColour (colour::text);
        g.setFont (fonts::uiSemiBold (textSize::xl));
        g.drawText ("Loading " + (soundName.isNotEmpty() ? soundName : juce::String ("sound")) + dots,
                    area.removeFromTop (area.getHeight() * 0.55f), juce::Justification::centredBottom);
        g.setColour (colour::textSecondary);
        g.setFont (fonts::ui (textSize::md));
        g.drawText ("Big sounds can take a little while. You can play as soon as this goes away.",
                    area, juce::Justification::centredTop);
    }

    void paint (juce::Graphics& g) override
    {
        if (! isAudioTrack && ! hasKnobs)
        {
            auto r = cardArea;
            g.setColour (colour::cardBg);
            g.fillRoundedRectangle (r.toFloat(), size::radiusCard);

            auto text = r.reduced (20, 16);
            const auto name = ! instrument.has_value() ? juce::String ("No instrument")
                            : instrument->isThirdParty ? instrument->name
                                                       : soundName;   // e.g. "Soft Piano" rather than "Sampler"
            g.setColour (colour::text);
            g.setFont (fonts::uiBold (textSize::xl));
            g.drawText (name, text.removeFromTop (24), juce::Justification::centredLeft, true);
            g.setColour (colour::textSecondary);
            g.setFont (fonts::ui (textSize::md));

            juce::String message;

            if (! instrument.has_value())
                message = "Pick a sound in the library on the left.";
            else if (instrument->missingPackId.isNotEmpty())
                message = "This sound comes from " + (instrument->missingPackName.isNotEmpty() ? "the \"" + instrument->missingPackName + "\""
                                                                                              : juce::String ("a"))
                          + " sound pack, which isn't installed on this computer. Download it to hear this track again, "
                          "or pick another sound in the library.";
            else if (instrument->missing)
                message = instrument->name + " isn't installed on this computer. Its settings are kept, so it will "
                          "work again once it's installed. Or pick another sound in the library.";
            else if (instrument->isThirdParty)
                message = "This plug-in has its own controls. Open its window to change the sound.";
            else
                message = "This instrument has no settings here. Shape its sound with effects under "
                          "Plug-ins on the left, or pick another sound in the library.";

            // (The Download button sits at the top right, so the message keeps the full height beside it.)
            g.drawFittedText (message, text.withTrimmedBottom (openWindowButton.isVisible() ? 40 : 0)
                                           .withTrimmedRight (downloadButton.isVisible() ? 170 : 0),
                              juce::Justification::topLeft, 3, 1.0f);
        }

        if (isAudioTrack)
        {
            g.setColour (colour::cardBg);
            g.fillRoundedRectangle (cardArea.toFloat(), size::radiusCard);

            auto text = cardArea.reduced (20, 16);
            g.setColour (colour::text);
            g.setFont (fonts::uiBold (textSize::xl));
            g.drawText ("Audio track", text.removeFromTop (24), juce::Justification::centredLeft);
            g.setColour (colour::textSecondary);
            g.setFont (fonts::ui (textSize::md));
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
            g.setFont (fonts::uiBold (textSize::md));
            g.drawText ("Details", r.removeFromTop (20), juce::Justification::centredLeft);

            g.setColour (colour::textSecondary);
            g.setFont (fonts::ui (textSize::md));
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

        // A taller panel gives the keyboard longer keys, up to a point; the
        // knobs get the rest of the height (up to 160px).
        keyboard.setBounds (r.removeFromBottom (juce::jlimit (92, 150, r.getHeight() - Knob::preferredHeight - gap)));
        r.removeFromBottom (gap);

        // The knobs share the width (leaving room for Details when shown), so
        // on a wide window each tile is wide and its dial big. Past 260px a
        // tile stops growing.
        auto top = r.withHeight (juce::jlimit (Knob::preferredHeight, 160, r.getHeight()));
        const int rowWidth = top.getWidth() - (detailsVisible ? detailsWidth + 20 : 0);
        const int tileWidth = juce::jlimit (Knob::preferredWidth, 260, (rowWidth - gap * (shownKnobs - 1)) / shownKnobs);

        // The instrument and audio-track cards take the knobs' place (but
        // don't stretch a few lines of text across the whole window).
        cardArea = top.withWidth (juce::jmin (top.getWidth(), 640, tileWidth * shownKnobs + gap * (shownKnobs - 1)));
        openWindowButton.setBounds (cardArea.reduced (20, 14).removeFromBottom (32).removeFromLeft (190));
        downloadButton.setBounds (cardArea.reduced (20, 14).removeFromTop (32).removeFromRight (150));

        for (auto* k : knobs)
        {
            if (! k->isVisible())
                continue;

            k->setBounds (top.removeFromLeft (tileWidth));
            top.removeFromLeft (gap);
        }

        top.removeFromLeft (20 - gap);
        detailsArea = top.withHeight (juce::jmin (top.getHeight(), 112));
    }

private:
    AudioEngine& engine;
    Selection& selection;
    juce::OwnedArray<Knob> knobs;
    PianoKeyboard keyboard;
    juce::String shownSound;       // the track and sound the keyboard's range was last set for
    bool rangePending = false;     // a new sound is waiting to load, then the keyboard moves to its keys
    bool loading = false;          // the selected track's instrument is still loading its samples
    int loadingTicks = 0;          // counts updates while loading, to animate the dots
    static constexpr int gap = 14;            // between knob tiles
    static constexpr int detailsWidth = 300;  // room kept for the Details card

    TrackDetails details;
    bool detailsVisible = false;
    bool isAudioTrack = false;
    bool hasKnobs = true;
    int shownKnobs = 5;   // how many knob tiles are laid out (the card takes the same width)
    std::optional<PluginSlot> instrument;
    juce::String soundName;
    juce::StringArray automated;   // the knobs the track's automation moves (TrackInfo::automated)
    IconButton openWindowButton { "Open plug-in window", Icon::openWindow, "Open Plug-in Window" };
    IconButton downloadButton { "Download the sound pack", std::nullopt, "Download Pack" };
    juce::Rectangle<int> detailsArea, cardArea;

    void refreshKnobText()
    {
        auto controls = engine.getTracks().effects().getSoundControls (selection.getTrack());

        for (int i = 0; i < knobs.size() && i < (int) controls.size(); ++i)
            knobs[i]->setValue (controls[(size_t) i].value, controls[(size_t) i].valueText);
    }
};

//==============================================================================
// The "Sound" tab in Master mode: the whole mix's volume and level.
class BottomPanel::MasterPage  : public juce::Component
{
public:
    explicit MasterPage (AudioEngine& e) : engine (e)
    {
        volume.setName ("Volume");
        volume.setArcColour (colour::master);
        volume.onDragStart = [this] { engine.beginUndoStep ("Master Volume"); };
        volume.onValueChange = [this] (float v)
        {
            engine.getTracks().setMasterVolume (v);
            refresh();
        };
        addAndMakeVisible (volume);
        addAndMakeVisible (meter);
    }

    void refresh()
    {
        auto& tc = engine.getTracks();
        const auto db = tc.getMasterVolumeDb();
        volume.setValue (tc.getMasterVolume(), db <= -99.0f ? juce::String ("Off") : juce::String (db, 1) + " dB");
    }

    void updateMeter()      { meter.setLevel (engine.getMasterLevel()); }

    void paint (juce::Graphics& g) override
    {
        g.setColour (colour::cardBg);
        g.fillRoundedRectangle (cardArea.toFloat(), size::radiusCard);

        auto text = cardArea.reduced (20, 16);
        g.setColour (colour::text);
        g.setFont (fonts::uiBold (textSize::xl));
        g.drawText ("Master", text.removeFromTop (24), juce::Justification::centredLeft);
        g.setColour (colour::textSecondary);
        g.setFont (fonts::ui (textSize::md));
        g.drawFittedText ("Everything you hear, mixed together. Effects under Plug-ins on the left work on the "
                          "whole mix, and the EQ tab shapes its tone.",
                          text.withTrimmedBottom (32), juce::Justification::topLeft, 3, 1.0f);

        g.setFont (fonts::ui (textSize::sm));
        g.drawText ("Level", meterLabelArea, juce::Justification::centredLeft);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (18, 14);
        auto top = r.withHeight (juce::jlimit (Knob::preferredHeight, 160, r.getHeight()));

        volume.setBounds (top.removeFromLeft (juce::jlimit (Knob::preferredWidth, 200, top.getWidth() / 5)));
        top.removeFromLeft (14);
        cardArea = top.withWidth (juce::jmin (top.getWidth(), 520));

        // The level meter along the bottom of the card.
        auto meterRow = cardArea.reduced (20, 16).removeFromBottom (16);
        meterLabelArea = meterRow.removeFromLeft (40);
        meter.setBounds (meterRow);
    }

private:
    AudioEngine& engine;
    Knob volume;
    LevelMeter meter;
    juce::Rectangle<int> cardArea, meterLabelArea;
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
    soundPage->onDownloadPack = [this] (const juce::String& packId)
    {
        if (onDownloadPackRequested)
            onDownloadPackRequested (packId);
    };
    addAndMakeVisible (*soundPage);
    masterPage = std::make_unique<MasterPage> (engine);
    addChildComponent (*masterPage);
    addChildComponent (eqView);
    addChildComponent (pianoRoll);
    addChildComponent (audioEditor);
    addChildComponent (mixer);

    // Segmented controls (the tabs, Track | Master): the panel draws the
    // shared rounded background (see paint), and the chosen segment is a
    // rounded pill inside it, like the rest of the app's buttons.
    // Chosen segments are neutral (neutralActive), never the accent.
    auto tabColours = IconButton::segmentColours();

    int index = 0;

    for (auto* b : { &soundTab, &editorTab, &eqTab })
    {
        b->setColours (tabColours);
        b->setCornerRadius (segmentRadius);
        b->setFont (fonts::uiBold (textSize::md));
        b->setRadioGroupId (1);   // radio group: turning one on turns the others off
        b->onClick = [this, index] { setTab ((Tab) index); if (onViewStateChanged) onViewStateChanged(); };
        addAndMakeVisible (b);
        ++index;
    }

    // Track | Master: the same look, a radio group of its own.
    for (auto* b : { &trackModeButton, &masterModeButton, &regionModeButton })
    {
        b->setColours (tabColours);
        b->setCornerRadius (segmentRadius);
        b->setFont (fonts::uiBold (textSize::md));
        b->setRadioGroupId (2);
        addAndMakeVisible (b);
    }

    trackModeButton.setTooltip ("Controls for the selected track");
    masterModeButton.setTooltip ("Controls for the master channel: the whole mix");
    trackModeButton.onClick = [this]
    {
        // On the Editor tab, Track is the other side of Track | Region.
        if (tab == Tab::editor)
            setRegionMode (false);
        else
            setMasterMode (false);

        if (onViewStateChanged)
            onViewStateChanged();
    };
    masterModeButton.onClick = [this] { setMasterMode (true); if (onViewStateChanged) onViewStateChanged(); };
    regionModeButton.setTooltip ("Settings for the region in the Editor");
    regionModeButton.setVisible (tab == Tab::editor);   // it sits in Master's place (see showTabPage)
    regionModeButton.onClick = [this] { setRegionMode (true); };

    // The Editor's grid and Quantize live in the region inspector, but
    // they're the editors' to use.
    regionInspector.onGridChanged = [this] (EditorGrid grid)
    {
        pianoRoll.setGrid (grid);
        audioEditor.setGrid (grid);
    };
    regionInspector.onQuantize = [this]
    {
        if (editingAudio)
            audioEditor.quantize();
        else
            pianoRoll.quantize();
    };
    regionInspector.onQuantizeFeelChanged = [this] (double strength, double swing)
    {
        pianoRoll.setQuantizeFeel (strength, swing);
        audioEditor.setQuantizeFeel (strength, swing);
    };
    addChildComponent (regionInspector);

    auto mixerColours = IconButton::neutralToggleColours();
    mixerButton.setColours (mixerColours);
    mixerButton.setFont (fonts::ui (textSize::md));
    mixerButton.setTooltip ("Every track's fader at once");
    mixerButton.onClick = [this] { setMixerShown (! mixerShown); if (onViewStateChanged) onViewStateChanged(); };
    addAndMakeVisible (mixerButton);

    auto detailsColours = IconButton::raisedColours();
    detailsColours.border = colour::border;
    detailsColours.backgroundOn = colour::raised;
    detailsColours.foregroundOn = colour::text;
    detailsButton.setColours (detailsColours);
    detailsButton.setFont (fonts::ui (textSize::md));
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
    mixerShown = false;   // picking a tab leaves the full mixer...

    if (isShowingEffect())
        closeEffect();    // ...and the effect settings

    showTabPage();
    refresh();
}

void BottomPanel::setMasterMode (bool shouldShowMaster)
{
    masterMode = shouldShowMaster;
    mixerShown = false;   // Track and Master are about one channel: leave the full mixer

    if (isShowingEffect())
        closeEffect();   // it was the other channel's effect

    showTabPage();
    refresh();
}

void BottomPanel::setRegionMode (bool shouldShowRegion)
{
    regionMode = shouldShowRegion;

    if (regionMode && isShowingEffect())
        closeEffect();   // the effect belongs to the track

    showTabPage();
}

void BottomPanel::setMixerShown (bool shouldShow)
{
    mixerShown = shouldShow;
    showTabPage();
    refresh();
}

TrackId BottomPanel::getTarget() const
{
    return showsMaster() ? masterTrackId : selection.getTrack();
}

void BottomPanel::showTabPage()
{
    const bool open = ! collapsed;
    const bool effect = isShowingEffect();
    const bool tabPage = open && ! mixerShown && ! effect;   // one of the tabs is showing

    soundPage->setVisible (tabPage && tab == Tab::sound && ! masterMode);
    masterPage->setVisible (tabPage && tab == Tab::sound && masterMode);
    eqView.setVisible (tabPage && tab == Tab::eq);
    // The Editor is about a track's regions: a MIDI region's notes, or an audio region's waveform.
    pianoRoll.setVisible (tabPage && tab == Tab::editor && ! editingAudio);
    audioEditor.setVisible (tabPage && tab == Tab::editor && editingAudio);
    effectViewport.setVisible (open && ! mixerShown && effect);
    mixer.setVisible (open && mixerShown);

    // The header stays the same in the full mixer (picking a tab, or Track
    // or Master, leaves it); only the inspector column makes way for it.
    inspector.setVisible (open && ! mixerShown && ! showsRegionInspector());
    regionInspector.setVisible (open && showsRegionInspector());

    // The Editor tab has Track | Region where the others have Track | Master,
    // and its Track is never the master (see showsMaster).
    if (inspectorShowsMaster != showsMaster())
    {
        inspectorShowsMaster = showsMaster();
        inspector.setMasterMode (inspectorShowsMaster);
        inspector.refresh();
    }

    for (auto* c : std::initializer_list<juce::Component*> { &detailsButton, &soundTab, &eqTab, &editorTab, &mixerButton,
                                                             &trackModeButton })
        c->setVisible (open);

    masterModeButton.setVisible (open && tab != Tab::editor);
    regionModeButton.setVisible (open && tab == Tab::editor);
    updateTabButtons();
    resized();
    repaint();
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
    // A track's effect, or in Master mode one of the master's.
    auto t = engine.getTracks().getTrack (selection.getTrack());

    if (! showsMaster() && ! t.has_value())
        return;

    if (isShowingEffect() && effectEditor->getPluginId() == id)
        return;

    effectViewport.setViewedComponent (nullptr, false);
    effectEditor = std::make_unique<PluginEditor> (engine, getTarget(), id, showsMaster() ? colour::master : t->colour);

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
    // ...and none while the full mixer is.
    const bool tabShowing = ! effect && ! mixerShown;
    soundTab.setToggleState (tabShowing && tab == Tab::sound, juce::dontSendNotification);
    eqTab.setToggleState (tabShowing && tab == Tab::eq, juce::dontSendNotification);
    editorTab.setToggleState (tabShowing && tab == Tab::editor, juce::dontSendNotification);
    trackModeButton.setToggleState (tab == Tab::editor ? ! regionMode : ! masterMode, juce::dontSendNotification);
    // Master and Region share a radio group with Track, so only the one that's
    // showing for this tab may be switched on (turning one on turns Track off).
    masterModeButton.setToggleState (tab != Tab::editor && masterMode, juce::dontSendNotification);
    regionModeButton.setToggleState (tab == Tab::editor && regionMode, juce::dontSendNotification);
    mixerButton.setToggleState (mixerShown, juce::dontSendNotification);
}

void BottomPanel::refresh()
{
    refreshTrack();
    refreshEditor();
}

void BottomPanel::refresh (const ModelChanges& changes)
{
    if (changes.session || changes.tracks || changes.mixer || changes.transport)
        refreshTrack();

    // A pure knob turn can't change which region the Editor shows or what's in it.
    if (changes.session || changes.tracks || changes.regions || changes.transport || ! changes.changedRegions.empty())
        refreshEditor();
}

void BottomPanel::refreshTrack()
{
    track = engine.getTracks().getTrack (selection.getTrack());

    // Switching tracks leaves the previous track's effect settings.
    if (isShowingEffect() && effectEditor->getTrackId() != getTarget())
        closeEffect();

    inspector.refresh();
    soundPage->refresh (track, detailsVisible);
    masterPage->refresh();
    eqView.setTarget (getTarget(), showsMaster() ? colour::master : track.has_value() ? track->colour : colour::master);

    if (mixer.isVisible())
        mixer.refresh();

    repaint (headerArea);
}

void BottomPanel::refreshEditor()
{
    auto& tc = engine.getTracks();

    // Editor: the selected region, or else the selected track's first
    // region (MIDI on an instrument track, audio on an audio track).
    std::optional<RegionInfo> region;
    const bool audioTrack = track.has_value() && track->kind == TrackKind::audio;

    if (selection.getRegion() != 0)
        region = tc.regions().getRegion (selection.getRegion());

    if (! region.has_value() && track.has_value())
        for (auto& r : tc.regions().getRegions (track->id))
            if (r.isMidi != audioTrack) { region = r; break; }

    // The region picks the editor; with none, the track does.
    const bool audio = region.has_value() ? ! region->isMidi : audioTrack;
    const auto trackColour = track.has_value() ? track->colour : colour::master;

    // Only one of them has the region, so the other forgets its drag and view.
    const auto tempo = engine.getTempoMap();
    pianoRoll.setRegion (audio ? std::nullopt : region, trackColour, tempo);
    audioEditor.setRegion (audio ? region : std::nullopt, trackColour, tempo);
    regionInspector.setRegion (region, trackColour);
    editorRegion = region;

    // Picking a region (in the arrangement) shows its settings, like GarageBand.
    bool layoutChanged = audio != editingAudio;
    editingAudio = audio;

    if (selection.getRegion() != lastSelectedRegion)
    {
        lastSelectedRegion = selection.getRegion();

        if (lastSelectedRegion != 0 && ! regionMode)
        {
            regionMode = true;
            layoutChanged = true;
        }
    }

    if (layoutChanged)
        showTabPage();

    repaint (headerArea);
}

void BottomPanel::update()
{
    if (collapsed)
        return;

    inspector.update();
    soundPage->update();

    if (mixerShown)
        mixer.updateMeters();
    else if (masterPage->isVisible())
        masterPage->updateMeter();
    else if (tab == Tab::editor && editingAudio)
    {
        audioEditor.setPlayheadBeat (engine.getTransportState().positionBeats);
        audioEditor.update();
    }
    else if (tab == Tab::editor)
    {
        pianoRoll.setPlayheadBeat (engine.getTransportState().positionBeats);
    }
}

void BottomPanel::paint (juce::Graphics& g)
{
    g.fillAll (colour::toolbarBg);
    g.setColour (colour::divider);
    g.fillRect (getLocalBounds().removeFromTop (1));

    if (! collapsed)
    {
        g.setColour (colour::wellBg);
        g.fillRect (0, headerHeight, getWidth(), 1);

        // Under the Track | Master switch: what the panel is working on.
        if (! mixerShown)
        {
            g.setColour (colour::sidebarBg);
            g.fillRect (nameStrip);
            g.setColour (colour::divider);
            g.fillRect (nameStrip.getRight() - 1, nameStrip.getY(), 1, nameStrip.getHeight());
        }
    }

    auto r = headerArea;

    // A title and a note: the master, or the selected track.
    auto drawTitle = [&] (const juce::String& title, const juce::String& note)
    {
        g.setColour (colour::text);
        g.setFont (fonts::uiBold (textSize::lg));
        auto titleWidth = (int) std::ceil (juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), title));
        g.drawText (title, r.removeFromLeft (titleWidth + 2), juce::Justification::centredLeft);
        r.removeFromLeft (8);
        g.setColour (colour::textSecondary);
        g.setFont (fonts::ui (textSize::lg));
        g.drawText (note, r, juce::Justification::centredLeft, true);
    };

    // The selected track: a dot in its colour, then its name. (The same
    // track the headers, lane and regions highlight; see Selection.)
    auto drawTrack = [&] (const TrackInfo& t, const juce::String& note)
    {
        g.setColour (t.colour);
        g.fillEllipse (r.removeFromLeft (10).withSizeKeepingCentre (10, 10).toFloat());
        r.removeFromLeft (8);
        drawTitle (t.name, note);
    };

    if (! collapsed && mixerShown)
    {
        // The full mixer has the whole panel: no name to show.
    }
    else if (showsMaster())
    {
        drawTitle ("Master", "The whole mix");
    }
    else if (track.has_value())
    {
        // In Region mode, the note names the region in the Editor.
        if (! collapsed && showsRegionInspector())
            drawTrack (*track, editorRegion.has_value() ? editorRegion->name : juce::String ("No region selected"));
        else
            drawTrack (*track, track->soundName);
    }
    else
    {
        g.setColour (colour::textSecondary);
        g.setFont (fonts::ui (textSize::lg));
        g.drawText ("No track selected", r, juce::Justification::centredLeft);
    }

    // The segmented controls' shared background and outline; the buttons
    // inside draw only the chosen (or hovered) segment.
    if (! collapsed)
    {
        for (auto area : { tabsArea, modeArea })
        {
            g.setColour (colour::cardBg);
            g.fillRoundedRectangle (area.toFloat(), segmentOutlineRadius);
            g.setColour (colour::border);
            g.drawRoundedRectangle (area.toFloat().reduced (0.5f), segmentOutlineRadius, 1.0f);
        }
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

    // The header row: Track | Master over the left column, the tabs with the
    // Mixer button beside them in the middle, Show details and hide on the right.
    auto header = r.removeFromTop (headerHeight).withTrimmedTop (1);
    auto leftColumn = header.removeFromLeft (TrackInspector::preferredWidth);

    modeArea = leftColumn.reduced (14, 0).removeFromLeft (modeWidth * 2).withSizeKeepingCentre (modeWidth * 2, 32);
    auto m = modeArea.reduced (segmentInset);
    trackModeButton.setBounds (m.removeFromLeft (m.getWidth() / 2));
    masterModeButton.setBounds (m);
    regionModeButton.setBounds (m);

    header = header.reduced (14, 8);
    collapseButton.setBounds (header.removeFromRight (32).withSizeKeepingCentre (32, 32));
    header.removeFromRight (8);
    detailsButton.setBounds (header.removeFromRight (120).withSizeKeepingCentre (120, 32));
    header.removeFromRight (8);

    constexpr int mixerButtonWidth = 80, mixerGap = 12;
    auto group = header.withSizeKeepingCentre (tabWidth * 3 + mixerGap + mixerButtonWidth, 32);
    tabsArea = group.removeFromLeft (tabWidth * 3);
    auto t = tabsArea.reduced (segmentInset);
    const int segment = t.getWidth() / 3;
    soundTab.setBounds (t.removeFromLeft (segment));
    editorTab.setBounds (t.removeFromLeft (segment));
    eqTab.setBounds (t);
    mixerButton.setBounds (group.removeFromRight (mixerButtonWidth));

    // The left column: what the panel works on, then the inspector. (The
    // full mixer has the whole width.)
    if (! mixerShown)
    {
        auto column = r.removeFromLeft (TrackInspector::preferredWidth);
        nameStrip = column.removeFromTop (nameHeight).withTrimmedTop (1);
        headerArea = nameStrip.reduced (14, 0);
        inspector.setBounds (column);
        regionInspector.setBounds (column);
    }

    r.removeFromTop (1);
    contentArea = r;

    for (auto* page : std::initializer_list<juce::Component*> { soundPage.get(), masterPage.get(), &eqView, &pianoRoll, &audioEditor, &mixer })
        page->setBounds (r);

    layoutEffectEditor();
}

} // namespace ikaros
