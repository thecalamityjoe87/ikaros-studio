#include "ui/bottompanel/BottomPanel.h"
#include "engine/Tuner.h"
#include "ui/widgets/AutomationHint.h"
#include "ui/widgets/Knob.h"
#include "ui/bottompanel/PianoKeyboard.h"
#include "ui/widgets/LevelMeter.h"
#include "ui/theme/Fonts.h"
#include "ui/theme/Tokens.h"
#include "ui/theme/TrackColours.h"

namespace ikaros
{

using namespace tokens;

namespace
{
    constexpr int headerHeight = 44;     // under the grip: the switch, the track's name and the buttons
    constexpr int dividerGap = 14;       // either side of the header's divider
    constexpr int tabHeight = 30;
    constexpr int switchRowHeight = 40;  // the top of the inspector column
    constexpr int modeWidth = 62;        // one segment of the inspector's small Track | Master switch
    constexpr int modeHeight = 26;
    constexpr int segmentInset = 3;      // a segmented control's pills sit this far inside its outline
    constexpr float segmentRadius = 7.0f, segmentOutlineRadius = 10.0f;
    constexpr float smallSegmentRadius = 5.0f, smallSegmentOutlineRadius = 7.0f;
    constexpr int padding = size::panelPadding;
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

        openWindowButton.setColours (IconButton::borderedColours);
        openWindowButton.setFont (fonts::uiSemiBold (textSize::md));
        openWindowButton.setIconSize (16.0f);
        openWindowButton.onClick = [this]
        {
            if (instrument.has_value())
                engine.getTracks().effects().showPluginWindow (selection.getTrack(), instrument->id);
        };
        addChildComponent (openWindowButton);

        downloadButton.setColours (IconButton::primaryColours);
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

    void refresh (const std::optional<TrackInfo>& track)
    {
        auto id = selection.getTrack();
        auto& tc = engine.getTracks();
        auto arc = track.has_value() ? colour::familyOf (*track) : Family::master;   // the Smart Controls follow the selected track's family

        // Audio tracks have no synth: no knobs or keyboard, just a pointer
        // to where their settings are.
        isAudioTrack = track.has_value() && track->kind == TrackKind::audio;   // (resized() shows or hides the keyboard)

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
            knobs[i]->setArcFamily (arc);
            knobs[i]->setEnabled (track.has_value());
            setAutomatedLook (knobs[i]->getSlider(), automated.contains (automationTargets::soundControl (i)));
        }

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
        g.setColour (theme().bottomPanelBg.withAlpha (0.82f));
        g.fillRoundedRectangle (area, size::radius);

        const auto dots = juce::String ("...").substring (0, 1 + (loadingTicks / 10) % 3);
        g.setColour (theme().text);
        g.setFont (fonts::uiSemiBold (textSize::xl));
        g.drawText ("Loading " + (soundName.isNotEmpty() ? soundName : juce::String ("sound")) + dots,
                    area.removeFromTop (area.getHeight() * 0.55f), juce::Justification::centredBottom);
        g.setColour (theme().textSecondary);
        g.setFont (fonts::ui (textSize::md));
        g.drawText ("Big sounds can take a little while. You can play as soon as this goes away.",
                    area, juce::Justification::centredTop);
    }

    void paint (juce::Graphics& g) override
    {
        for (auto* k : knobs)
            if (k->isVisible())
                Knob::drawShadow (g, *k);

        if (! isAudioTrack && ! hasKnobs)
        {
            auto r = cardArea;
            g.setColour (theme().cardBg);
            g.fillRoundedRectangle (r.toFloat(), size::radiusCard);

            auto text = r.reduced (20, 16);
            const auto name = ! instrument.has_value() ? juce::String ("No instrument")
                            : instrument->isThirdParty ? instrument->name
                                                       : soundName;   // e.g. "Soft Piano" rather than "Sampler"
            g.setColour (theme().text);
            g.setFont (fonts::uiBold (textSize::xl));
            g.drawText (name, text.removeFromTop (24), juce::Justification::centredLeft, true);
            g.setColour (theme().textSecondary);
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
            g.setColour (theme().cardBg);
            g.fillRoundedRectangle (cardArea.toFloat(), size::radiusCard);

            auto text = cardArea.reduced (20, 16);
            g.setColour (theme().text);
            g.setFont (fonts::uiBold (textSize::xl));
            g.drawText ("Audio track", text.removeFromTop (24), juce::Justification::centredLeft);
            g.setColour (theme().textSecondary);
            g.setFont (fonts::ui (textSize::md));
            g.drawFittedText ("Choose the input and monitoring under Recording on the left, and shape the sound "
                              "with Plug-ins. Press R or the Record button to record.",
                              text, juce::Justification::topLeft, 3, 1.0f);
        }
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (padding, 12);

        // The keyboard goes under the knobs when the panel is tall enough for
        // both; a taller panel gives it longer keys, up to a point. A short
        // panel (it can be dragged down to BottomPanel::minHeight) keeps just
        // the knobs.
        const int keyboardHeight = juce::jmin (maxKeyboardHeight, r.getHeight() - Knob::preferredHeight - gap);
        const bool keyboardFits = keyboardHeight >= minKeyboardHeight;
        keyboard.setVisible (! isAudioTrack && keyboardFits);

        if (keyboardFits)
        {
            keyboard.setBounds (r.removeFromBottom (keyboardHeight));
            r.removeFromBottom (gap);
        }

        // The knob cards sit in a grid of (at least) four equal columns
        // across the whole width; a sound with five knobs gets a fifth
        // column, and one with fewer leaves the last columns empty. The
        // cards get the rest of the height, up to 160px.
        auto top = r.withHeight (juce::jmin (r.getHeight(), 160));
        const int columns = juce::jmax (4, shownKnobs);
        const int tileWidth = (top.getWidth() - gap * (columns - 1)) / columns;

        // The instrument and audio-track cards take the knobs' place (but
        // don't stretch a few lines of text across the whole window).
        cardArea = top.withWidth (juce::jmin (top.getWidth(), 640));
        openWindowButton.setBounds (cardArea.reduced (20, 14).removeFromBottom (32).removeFromLeft (190));
        downloadButton.setBounds (cardArea.reduced (20, 14).removeFromTop (32).removeFromRight (150));

        for (auto* k : knobs)
        {
            if (! k->isVisible())
                continue;

            k->setBounds (top.removeFromLeft (tileWidth));
            top.removeFromLeft (gap);
        }
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
    static constexpr int gap = 14;                  // between knob tiles, and above the keyboard
    static constexpr int minKeyboardHeight = 72;    // shorter than this, the keyboard is left out
    static constexpr int maxKeyboardHeight = 150;

    bool isAudioTrack = false;
    bool hasKnobs = true;
    int shownKnobs = 5;   // how many knob tiles are laid out
    std::optional<PluginSlot> instrument;
    juce::String soundName;
    juce::StringArray automated;   // the knobs the track's automation moves (TrackInfo::automated)
    IconButton openWindowButton { "Open plug-in window", Icon::openWindow, "Open Plug-in Window" };
    IconButton downloadButton { "Download the sound pack", std::nullopt, "Download Pack" };
    juce::Rectangle<int> cardArea;

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
        volume.setArcFamily (Family::master);
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
        Knob::drawShadow (g, volume);

        g.setColour (theme().cardBg);
        g.fillRoundedRectangle (cardArea.toFloat(), size::radiusCard);

        auto text = cardArea.reduced (20, 16);
        g.setColour (theme().text);
        g.setFont (fonts::uiBold (textSize::xl));
        g.drawText ("Master", text.removeFromTop (24), juce::Justification::centredLeft);
        g.setColour (theme().textSecondary);
        g.setFont (fonts::ui (textSize::md));
        g.drawFittedText ("Everything you hear, mixed together. Effects under Plug-ins on the left work on the "
                          "whole mix, and the EQ tab shapes its tone.",
                          text.withTrimmedBottom (32), juce::Justification::topLeft, 3, 1.0f);

        g.setFont (fonts::ui (textSize::sm));
        g.drawText ("Level", meterLabelArea, juce::Justification::centredLeft);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (padding, 12);
        auto top = r.withHeight (juce::jmin (r.getHeight(), 160));

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
    addChildComponent (pedalboard);
    addChildComponent (pianoRoll);
    addChildComponent (audioEditor);
    addChildComponent (mixer);

    // Segmented controls (Sound | Editor | EQ | Pedals | Mixer, and the inspector's
    // Track | Master): the panel draws the shared rounded background (see
    // paint), and the chosen segment is a rounded pill inside it, like the
    // rest of the app's buttons. Chosen segments are neutral
    // (neutralActive), never the accent.

    int index = 0;

    for (auto* b : { &soundTab, &editorTab, &eqTab })
    {
        const auto tabForButton = index == 0 ? Tab::sound : index == 1 ? Tab::editor : Tab::eq;
        b->onClick = [this, tabForButton] { setTab (tabForButton); if (onViewStateChanged) onViewStateChanged(); };
        ++index;
    }

    // Pedals only shows while an audio track is selected (see showTabPage).
    pedalsTab.setTooltip ("The track's pedalboard: stompbox effects for guitar and bass");
    pedalsTab.onClick = [this] { setTab (Tab::pedals); if (onViewStateChanged) onViewStateChanged(); };

    // Mixer is the switch's last segment, but a view of its own rather than
    // a tab: it covers the whole panel until a tab is picked again.
    mixerButton.setTooltip ("Every track's fader at once");
    mixerButton.onClick = [this] { setMixerShown (true); if (onViewStateChanged) onViewStateChanged(); };

    for (auto* b : { &soundTab, &editorTab, &eqTab, &pedalsTab, &mixerButton })
    {
        b->setColours (IconButton::segmentColours);
        b->setCornerRadius (segmentRadius);
        b->setFont (fonts::uiBold (textSize::md));
        b->setRadioGroupId (1);   // radio group: turning one on turns the others off
        addAndMakeVisible (b);
    }

    // Track | Master: the same look, smaller, a radio group of its own.
    for (auto* b : { &trackModeButton, &masterModeButton, &regionModeButton })
    {
        b->setColours (IconButton::segmentColours);
        b->setCornerRadius (smallSegmentRadius);
        b->setFont (fonts::uiSemiBold (textSize::sm));
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

    // Inspector: a neutral toggle (neutralActive while the column shows).
    inspectorButton.setColours (IconButton::neutralToggleColours);
    inspectorButton.setFont (fonts::ui (textSize::md));
    inspectorButton.setTooltip ("Show or hide the inspector");
    inspectorButton.setToggleState (inspectorVisible, juce::dontSendNotification);
    inspectorButton.onClick = [this]
    {
        setInspectorVisible (! inspectorVisible);
        if (onViewStateChanged) onViewStateChanged();
    };
    addAndMakeVisible (inspectorButton);

    // The Pedals tab's own buttons, in the header (so the board below is all
    // pedals). Tuner lights up while the tuner is on (see update()).
    for (auto* b : { &tunerButton, &addPedalButton })
    {
        b->setColours (IconButton::raisedColours);
        b->setFont (fonts::ui (textSize::md));
        b->setIconSize (16.0f);
        addChildComponent (b);
    }

    tunerButton.setTooltip ("Tune up: the transport readout becomes a tuner for this track's input");
    tunerButton.onClick = [this] { engine.getTuner().setActive (! engine.getTuner().isActive()); };
    addPedalButton.onClick = [this] { pedalboard.showAddPedal (addPedalButton); };

    // An ordinary button, like the rest of the header's.
    collapseButton.setColours (IconButton::raisedColours);
    collapseButton.setTooltip ("Hide panel");
    collapseButton.onClick = [this] { if (onCollapseRequested) onCollapseRequested (! collapsed); };
    addAndMakeVisible (collapseButton);

    updateTabButtons();
    refresh();
}

BottomPanel::~BottomPanel() = default;

void BottomPanel::setTab (Tab t)
{
    // (The pedalboard may have just been shown from the menu, before the
    // engine's change notice has come through.)
    updatePedalboardAvailable();

    if (t == Tab::pedals && ! isPedalboardAvailable())
        t = Tab::sound;

    tab = t;
    mixerShown = false;   // picking a tab leaves the full mixer...

    // The pedalboard is the selected track's, like the Editor.
    if (tab == Tab::pedals)
        masterMode = false;

    if (isShowingEffect())
        closeEffect();    // ...and the effect settings

    showTabPage();
    refresh();
}

void BottomPanel::setMasterMode (bool shouldShowMaster)
{
    masterMode = shouldShowMaster;
    mixerShown = false;   // Track and Master are about one channel: leave the full mixer

    // The master has no pedalboard: Master shows its Sound page instead.
    if (masterMode && tab == Tab::pedals)
        tab = Tab::sound;

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

void BottomPanel::updatePedalboardAvailable()
{
    const auto id = track.has_value() ? track->id : TrackId (0);
    const bool sameTrack = id == pedalboardTrack;
    pedalboardTrack = id;

    // A guitar or bass track (or one with pedals) has a pedalboard. The
    // board also stays while it's showing for the same track (see
    // isPedalboardAvailable).
    const bool stays = sameTrack && pedalboardAvailable && tab == Tab::pedals
                         && track.has_value() && track->kind == TrackKind::audio;
    pedalboardAvailable = id != 0 && (engine.getTracks().effects().hasPedalboard (id) || stays);
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
    pedalboard.setVisible (tabPage && tab == Tab::pedals);
    // The Editor is about a track's regions: a MIDI region's notes, or an audio region's waveform.
    pianoRoll.setVisible (tabPage && tab == Tab::editor && ! editingAudio);
    audioEditor.setVisible (tabPage && tab == Tab::editor && editingAudio);
    effectViewport.setVisible (open && ! mixerShown && effect);
    mixer.setVisible (open && mixerShown);

    // The header stays the same in the full mixer (picking a tab leaves it);
    // only the inspector column makes way for it.
    const bool column = showsInspectorColumn();
    inspector.setVisible (column && ! showsRegionInspector());
    regionInspector.setVisible (column && showsRegionInspector());

    // The Editor tab has Track | Region where the others have Track | Master,
    // and its Track is never the master (see showsMaster).
    if (inspectorShowsMaster != showsMaster())
    {
        inspectorShowsMaster = showsMaster();
        inspector.setMasterMode (inspectorShowsMaster);
        inspector.refresh();
    }

    for (auto* c : std::initializer_list<juce::Component*> { &inspectorButton, &soundTab, &eqTab, &editorTab, &mixerButton })
        c->setVisible (open);

    pedalsShown = isPedalboardAvailable();
    pedalsTab.setVisible (open && pedalsShown);
    tunerButton.setVisible (tabPage && tab == Tab::pedals);
    addPedalButton.setVisible (tabPage && tab == Tab::pedals);

    trackModeButton.setVisible (column);
    masterModeButton.setVisible (column && tab != Tab::editor);
    regionModeButton.setVisible (column && tab == Tab::editor);
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
    collapseButton.setIconSize (collapsed ? 12.0f : 16.0f);   // the collapsed strip is only 16px tall
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
    effectEditor = std::make_unique<PluginEditor> (engine, getTarget(), id, showsMaster() ? Family::master : colour::familyOf (*t));

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

void BottomPanel::setInspectorVisible (bool shouldShow)
{
    inspectorVisible = shouldShow;
    inspectorButton.setToggleState (inspectorVisible, juce::dontSendNotification);
    inspectorButton.setTitle (inspectorVisible ? "Hide inspector" : "Show inspector");
    showTabPage();
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
    pedalsTab.setToggleState (tabShowing && tab == Tab::pedals, juce::dontSendNotification);
    trackModeButton.setToggleState (tab == Tab::editor ? ! regionMode : ! masterMode, juce::dontSendNotification);
    // Master and Region share a radio group with Track, so only the one that's
    // showing for this tab may be switched on (turning one on turns Track off).
    masterModeButton.setToggleState (tab != Tab::editor && masterMode, juce::dontSendNotification);
    regionModeButton.setToggleState (tab == Tab::editor && regionMode, juce::dontSendNotification);
    mixerButton.setToggleState (mixerShown, juce::dontSendNotification);   // (no tab is chosen then)
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

    // The Pedals segment comes and goes with guitar and bass tracks; off
    // one, the Pedals tab gives way to Sound.
    updatePedalboardAvailable();

    if (tab == Tab::pedals && ! isPedalboardAvailable())
    {
        tab = Tab::sound;
        showTabPage();

        if (onViewStateChanged)
            onViewStateChanged();
    }
    else if (pedalsShown != isPedalboardAvailable())
    {
        showTabPage();
    }

    inspector.refresh();
    soundPage->refresh (track);
    pedalboard.refresh();
    addPedalButton.setEnabled (pedalboard.canAddPedal());
    addPedalButton.setTooltip (pedalboard.canAddPedal() ? juce::String ("Add a pedal at the end of the board")
                                                        : "This track has " + juce::String (EffectsController::maxEffectsPerTrack)
                                                            + " effects, the most a track can have");
    masterPage->refresh();
    eqView.setTarget (getTarget(), ! showsMaster() && track.has_value() ? colour::familyOf (*track) : Family::master);

    if (mixer.isVisible())
        mixer.refresh();

    repaint (titleArea);
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
    const auto family = track.has_value() ? colour::familyOf (*track) : Family::master;

    // Only one of them has the region, so the other forgets its drag and view.
    const auto tempo = engine.getTempoMap();
    pianoRoll.setRegion (audio ? std::nullopt : region, family, tempo);
    audioEditor.setRegion (audio ? region : std::nullopt, family, tempo);
    regionInspector.setRegion (region, family);
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

    repaint (regionNameArea);
}

void BottomPanel::update()
{
    if (collapsed)
        return;

    inspector.update();
    soundPage->update();

    if (tunerButton.isVisible())
        tunerButton.setToggleState (engine.getTuner().isActive(), juce::dontSendNotification);

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
    g.fillAll (theme().bottomPanelBg);
    g.setColour (theme().bottomPanelBorder);
    g.fillRect (getLocalBounds().removeFromTop (1));

    // Collapsed, the strip is all grip (the ResizeHandle laid over it draws
    // that) and the show button.
    if (collapsed)
        return;

    g.setColour (theme().wellBg);
    g.fillRect (0, handleHeight + headerHeight, getWidth(), 1);

    // The segmented controls' shared background and outline; the buttons
    // inside draw only the chosen (or hovered) segment.
    auto drawSegmentBackground = [&g] (juce::Rectangle<int> area, float radius)
    {
        g.setColour (theme().segmentWell);
        g.fillRoundedRectangle (area.toFloat(), radius);
        g.setColour (theme().segmentWellBorder);
        g.drawRoundedRectangle (area.toFloat().reduced (0.5f), radius, 1.0f);
    };

    drawSegmentBackground (tabsArea, segmentOutlineRadius);

    for (auto* b : { &soundTab, &editorTab, &eqTab, &pedalsTab, &mixerButton })
        if (b->isVisible())
            IconButton::drawSegmentShadow (g, *b);

    g.setColour (theme().border);
    g.fillRect (headerDivider);

    // The title: the master, or the selected track (a dot in its colour,
    // its name, then its sound). The same track the headers, lane and
    // regions highlight; see Selection.
    auto r = titleArea;

    auto drawTitle = [&] (const juce::String& title, const juce::String& note)
    {
        g.setColour (theme().text);
        g.setFont (fonts::uiBold (textSize::lg));
        const auto titleWidth = (int) std::ceil (juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), title));
        g.drawText (title, r.removeFromLeft (juce::jmin (titleWidth + 2, r.getWidth())), juce::Justification::centredLeft, true);
        r.removeFromLeft (8);
        g.setColour (theme().textSecondary);
        g.setFont (fonts::ui (textSize::md));
        g.drawText (note, r, juce::Justification::centredLeft, true);
    };

    if (showsMaster())
    {
        g.setColour (theme().family (Family::master));
        g.fillEllipse (r.removeFromLeft (10).withSizeKeepingCentre (10, 10).toFloat());
        r.removeFromLeft (8);
        drawTitle ("Master", "The whole mix");
    }
    else if (track.has_value())
    {
        g.setColour (colour::forTrack (*track));
        g.fillEllipse (r.removeFromLeft (10).withSizeKeepingCentre (10, 10).toFloat());
        r.removeFromLeft (8);
        drawTitle (track->name, track->soundName);
    }
    else
    {
        g.setColour (theme().textSecondary);
        g.setFont (fonts::ui (textSize::lg));
        g.drawText ("No track selected", r, juce::Justification::centredLeft);
    }

    // The top of the inspector column: its switch, and in Region mode the
    // region's name beside it.
    if (showsInspectorColumn())
    {
        g.setColour (theme().inspectorBg);
        g.fillRect (switchRow);
        g.setColour (theme().divider);
        g.fillRect (switchRow.getRight() - 1, switchRow.getY(), 1, switchRow.getHeight());
        g.setColour (theme().borderSoft);
        g.fillRect (switchRow.getX(), switchRow.getBottom() - 1, switchRow.getWidth() - 1, 1);

        drawSegmentBackground (modeArea, smallSegmentOutlineRadius);

        for (auto* b : { &trackModeButton, &masterModeButton, &regionModeButton })
            IconButton::drawSegmentShadow (g, *b);

        if (showsRegionInspector())
        {
            g.setColour (editorRegion.has_value() ? theme().text : theme().textSecondary);
            g.setFont (fonts::ui (textSize::md));
            g.drawText (editorRegion.has_value() ? editorRegion->name : juce::String ("No region selected"),
                        regionNameArea, juce::Justification::centredLeft, true);   // true: "..." if it's too long
        }
    }
}

juce::Rectangle<int> BottomPanel::getHandleBounds() const
{
    if (collapsed)
        return getLocalBounds().withTrimmedRight (getWidth() - collapseButton.getX() + 8);

    return getLocalBounds().removeFromTop (handleHeight);
}

void BottomPanel::resized()
{
    auto r = getLocalBounds();

    if (collapsed)
    {
        // The slim strip: the grip (see getHandleBounds) and the show button on the right.
        collapseButton.setBounds (r.reduced (padding, 0).removeFromRight (32).withTrimmedTop (1));
        return;
    }

    r.removeFromTop (handleHeight);

    // The header row: Sound | Editor | EQ | (Pedals |) Mixer on the left, a
    // divider and the selected track's name, then Inspector and hide on the
    // right. The divider sits on the inspector column's edge (and so the
    // track headers' edge above), and the switch's segments share the room
    // to its left.
    auto header = r.removeFromTop (headerHeight).reduced (padding, 0);
    const int dividerX = TrackInspector::preferredWidth - 1;   // where the inspector column draws its edge
    auto beforeDivider = header.removeFromLeft (dividerX - header.getX());

    auto tabs = beforeDivider.withTrimmedRight (dividerGap);
    tabsArea = tabs.withSizeKeepingCentre (tabs.getWidth(), tabHeight);
    auto t = tabsArea.reduced (segmentInset);
    const int tabWidth = t.getWidth() / (pedalsShown ? 5 : 4);

    for (auto* b : { &soundTab, &editorTab, &eqTab })
        b->setBounds (t.removeFromLeft (tabWidth));

    if (pedalsShown)
        pedalsTab.setBounds (t.removeFromLeft (tabWidth));

    mixerButton.setBounds (t);   // the last segment takes any spare pixel

    headerDivider = header.removeFromLeft (1).withSizeKeepingCentre (1, 20);
    header.removeFromLeft (dividerGap);

    collapseButton.setBounds (header.removeFromRight (30).withSizeKeepingCentre (30, 30));
    header.removeFromRight (8);
    inspectorButton.setBounds (header.removeFromRight (96).withSizeKeepingCentre (96, 30));
    header.removeFromRight (14);

    // On the Pedals tab: Tuner and Add Pedal, left of Inspector.
    if (addPedalButton.isVisible())
    {
        addPedalButton.setBounds (header.removeFromRight (120).withSizeKeepingCentre (120, 30));
        header.removeFromRight (8);
        tunerButton.setBounds (header.removeFromRight (88).withSizeKeepingCentre (88, 30));
        header.removeFromRight (14);
    }

    titleArea = header;

    r.removeFromTop (1);   // the line under the header

    // The inspector column (320px, as wide as the track headers above): its
    // switch, then the track's or region's settings. The full mixer has the
    // whole width.
    if (showsInspectorColumn())
    {
        auto column = r.removeFromLeft (TrackInspector::preferredWidth);
        switchRow = column.removeFromTop (switchRowHeight);

        auto row = switchRow.reduced (padding, 0);
        modeArea = row.removeFromLeft (modeWidth * 2 + segmentInset * 2).withSizeKeepingCentre (modeWidth * 2 + segmentInset * 2, modeHeight);
        auto m = modeArea.reduced (segmentInset);
        trackModeButton.setBounds (m.removeFromLeft (modeWidth));
        masterModeButton.setBounds (m);
        regionModeButton.setBounds (m);
        row.removeFromLeft (12);
        regionNameArea = row;

        inspector.setBounds (column);
        regionInspector.setBounds (column);
    }

    contentArea = r;

    for (auto* page : std::initializer_list<juce::Component*> { soundPage.get(), masterPage.get(), &eqView, &pedalboard, &pianoRoll, &audioEditor, &mixer })
        page->setBounds (r);

    layoutEffectEditor();
}

} // namespace ikaros
