#include "ui/dialogs/NewTrackDialog.h"
#include "ui/dialogs/AlertDialog.h"
#include "ui/dialogs/MonitoringWarning.h"
#include "ui/theme/Fonts.h"
#include "ui/theme/IkarosLookAndFeel.h"
#include "ui/theme/Tokens.h"

namespace ikaros
{

using namespace tokens;

//==============================================================================
// One big choice tile: icon, title, one-line description.
class NewTrackDialog::Card  : public juce::Button
{
public:
    Card (Icon i, const juce::String& title, const juce::String& text, Family f)
        : juce::Button (title), icon (i), description (text), family (f)
    {
        setTitle (title);
        setDescription (text);
        setClickingTogglesState (false);
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    std::function<void()> onDoubleClick;

    void mouseDoubleClick (const juce::MouseEvent&) override   { if (onDoubleClick) onDoubleClick(); }

    // Return means "Create" for the whole dialog, so don't treat it as a
    // click on this card (Space still selects it).
    bool keyPressed (const juce::KeyPress& key) override
    {
        if (key == juce::KeyPress::returnKey)
            return false;

        return juce::Button::keyPressed (key);
    }

    void paintButton (juce::Graphics& g, bool over, bool) override
    {
        auto r = getLocalBounds().toFloat().reduced (1.0f);
        const bool on = getToggleState();

        g.setColour (on ? theme().selectedRow : (over ? theme().raised : theme().cardBg));
        g.fillRoundedRectangle (r, size::radiusCard);
        g.setColour (on ? theme().selectedBorder : theme().borderSoft);
        g.drawRoundedRectangle (r, size::radiusCard, on ? 2.0f : 1.0f);

        auto content = getLocalBounds().reduced (20);
        auto iconArea = content.removeFromTop (64);
        g.setColour (theme().wellBg);
        g.fillRoundedRectangle (iconArea.withSizeKeepingCentre (64, 64).toFloat(), size::radiusPill);
        icons::draw (g, icon, iconArea.toFloat(), theme().family (family), 36.0f, 1.6f);

        content.removeFromTop (14);
        g.setColour (theme().text);
        g.setFont (fonts::uiBold (textSize::xxl));
        g.drawText (getName(), content.removeFromTop (22), juce::Justification::centred);

        content.removeFromTop (6);
        g.setColour (theme().textSecondary);
        g.setFont (fonts::ui (textSize::md));
        g.drawFittedText (description, content, juce::Justification::centredTop, 3, 1.0f);

        if (hasKeyboardFocus (false))
            IkarosLookAndFeel::drawFocusRing (g, r, size::radiusCard);
    }

private:
    Icon icon;
    juce::String description;
    Family family;   // the icon's colour, looked up when painting
};

//==============================================================================
void NewTrackDialog::show (juce::Component& parent, AudioEngine& engine, std::function<void (TrackId)> onCreated)
{
    // Owned by its parent from here on; it deletes itself in close().
    auto* dialog = new NewTrackDialog (engine, std::move (onCreated));
    parent.addAndMakeVisible (dialog);
    dialog->setBounds (parent.getLocalBounds());
    dialog->instrumentCard->grabKeyboardFocus();
}

NewTrackDialog::NewTrackDialog (AudioEngine& e, std::function<void (TrackId)> created)
    : engine (e), onCreated (std::move (created))
{
    setTitle ("New track");
    setWantsKeyboardFocus (true);
    setAlwaysOnTop (true);

    instrumentCard = std::make_unique<Card> (Icon::keyboards, "Software Instrument",
                                             "Play and record with a MIDI keyboard, your computer keyboard or the on-screen keyboard.",
                                             Family::keys);
    audioCard = std::make_unique<Card> (Icon::mic, "Audio",
                                        "Record a microphone, or anything else plugged into your audio interface.",
                                        Family::audio);   // audio tracks are always the audio blue
    vocalsCard = std::make_unique<Card> (Icon::vocals, "Vocals",
                                         "Sing or rap into a microphone, or record a voice-over.",
                                         Family::audio);
    guitarCard = std::make_unique<Card> (Icon::guitar, "Guitar or Bass",
                                         "Plug in a guitar or bass and play it through a pedalboard of effects.",
                                         Family::guitar);
    drummerCard = std::make_unique<Card> (Icon::drummer, "Drummer",
                                          "A drummer who plays along in the style you pick, as busy and loud as you like.",
                                          Family::drums);
    samplerCard = std::make_unique<Card> (Icon::waveform, "Sampler",
                                          "Play any sound on the keyboard: a sung note, a clap, a word.",
                                          Family::synths);   // the colour sampler tracks get (see TrackColours.h)

    for (auto kind : { Kind::instrument, Kind::audio, Kind::vocals, Kind::guitar, Kind::drummer, Kind::sampler })
    {
        auto& card = cardFor (kind);
        card.onClick = [this, kind] { choose (kind); };
        card.onDoubleClick = [this, kind] { choose (kind); create(); };
        addAndMakeVisible (card);
    }

    // Audio, Vocals and Guitar or Bass options
    inputs = engine.getTracks().inputs().getAudioInputs();

    for (int i = 0; i < (int) inputs.size(); ++i)
        inputBox.addItem (inputs[(size_t) i].name, i + 1);

    if (inputs.empty())
        inputBox.addItem ("No audio inputs found", 1);

    inputBox.setSelectedItemIndex (0, juce::dontSendNotification);
    inputBox.setEnabled (! inputs.empty());
    inputBox.setTitle ("Input");
    addChildComponent (inputBox);

    // Guitar or Bass: a bass track starts with the amp's Bass voice.
    playsBox.addItem ("Guitar", 1);
    playsBox.addItem ("Bass", 2);
    playsBox.setSelectedId (1, juce::dontSendNotification);
    playsBox.setTitle ("Plays");
    playsBox.setTooltip ("A bass track's amp starts on the Bass voice");
    addChildComponent (playsBox);

    monitorToggle.setTooltip ("Use headphones, or a microphone near your speakers will feed back.");
    addChildComponent (monitorToggle);

    // Ticking it shows the feedback warning the first time; Cancel unticks it.
    monitorToggle.onClick = [this]
    {
        if (! monitorToggle.getToggleState())
            return;

        // SafePointer: the dialog might be closed while the warning is up.
        confirmMonitoring (engine, this, [safeThis = juce::Component::SafePointer<NewTrackDialog> (this)] (bool goAhead)
        {
            if (safeThis != nullptr && ! goAhead)
                safeThis->monitorToggle.setToggleState (false, juce::dontSendNotification);
        });
    };

    cancelButton.setColours (IconButton::borderedColours);
    cancelButton.onClick = [this] { close(); };
    addAndMakeVisible (cancelButton);

    createButton.setColours (IconButton::primaryColours);
    createButton.onClick = [this] { create(); };
    addAndMakeVisible (createButton);

    choose (Kind::instrument);
}

NewTrackDialog::Card& NewTrackDialog::cardFor (Kind kind) const
{
    return kind == Kind::instrument ? *instrumentCard
         : kind == Kind::audio      ? *audioCard
         : kind == Kind::vocals     ? *vocalsCard
         : kind == Kind::guitar     ? *guitarCard
         : kind == Kind::drummer    ? *drummerCard
                                    : *samplerCard;
}

void NewTrackDialog::choose (Kind kind)
{
    chosen = kind;

    for (auto k : { Kind::instrument, Kind::audio, Kind::vocals, Kind::guitar, Kind::drummer, Kind::sampler })
        cardFor (k).setToggleState (k == chosen, juce::dontSendNotification);

    // Audio, vocals and guitar tracks record an input.
    inputBox.setVisible (recordsInput());
    monitorToggle.setVisible (recordsInput());
    playsBox.setVisible (chosen == Kind::guitar);

    // A sampler needs its sound first: Create asks for it.
    createButton.setText (chosen == Kind::sampler ? "Choose Sound..." : "Create");
    resized();
    repaint();
}

void NewTrackDialog::create()
{
    auto& tracks = engine.getTracks();
    TrackId id = 0;

    if (recordsInput())
    {
        id = chosen == Kind::guitar ? tracks.addGuitarTrack (playsBox.getSelectedId() == 2)
           : chosen == Kind::vocals ? tracks.addVocalTrack()
                                    : tracks.addAudioTrack();

        if (id != 0)
        {
            const auto index = inputBox.getSelectedItemIndex();

            if (juce::isPositiveAndBelow (index, (int) inputs.size()))
                tracks.inputs().setTrackInput (id, inputs[(size_t) index].id);

            if (monitorToggle.getToggleState())
                tracks.inputs().setMonitoring (id, true);
        }
    }
    else if (chosen == Kind::drummer)
    {
        id = tracks.addDrummerTrack();
    }
    else if (chosen == Kind::sampler)
    {
        chooseSamplerSound();   // creates the track once there's a sound (the dialog stays open till then)
        return;
    }
    else
    {
        id = tracks.addInstrumentTrack();
    }

    finish (id);
}

void NewTrackDialog::chooseSamplerSound()
{
    soundChooser = std::make_unique<juce::FileChooser> ("Choose a Sound to Play",
                                                        juce::File::getSpecialLocation (juce::File::userMusicDirectory),
                                                        "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
    soundChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                               [safeThis = SafePointer<NewTrackDialog> (this)] (const juce::FileChooser& fc)
    {
        // Cancelled: back to the dialog, nothing made.
        if (safeThis == nullptr || ! fc.getResult().existsAsFile())
            return;

        const auto file = fc.getResult();

        if (const auto id = safeThis->engine.getTracks().addSamplerTrack (file); id != 0)
        {
            safeThis->finish (id);
            return;
        }

        AlertDialog::show (juce::MessageBoxOptions()
                           .withTitle ("Couldn't use that sound")
                           .withMessage ("\"" + file.getFileName() + "\" couldn't be read as audio. "
                                         "Try a WAV, AIFF, FLAC, Ogg or MP3 file.")
                           .withButton ("OK")
                           .withAssociatedComponent (safeThis.getComponent()));
    });
}

void NewTrackDialog::finish (TrackId id)
{
    auto callback = onCreated;
    close();

    if (id != 0 && callback)
        callback (id);
}

void NewTrackDialog::close()
{
    // Remove ourselves from the parent and delete on the next message-loop
    // turn: we may be inside one of our own button callbacks right now.
    // Drop keyboard focus too, so it doesn't land on a random button.
    setVisible (false);
    juce::Component::unfocusAllComponents();

    juce::MessageManager::callAsync ([safeThis = SafePointer<NewTrackDialog> (this)]
    {
        if (safeThis != nullptr)
            delete safeThis.getComponent();
    });
}

bool NewTrackDialog::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey)            { close(); return true; }
    if (key == juce::KeyPress::returnKey)            { create(); return true; }

    // Left and Right step through the cards, Up and Down move between the rows.
    const int step = key == juce::KeyPress::leftKey  ? -1
                   : key == juce::KeyPress::rightKey ?  1
                   : key == juce::KeyPress::upKey    ? -columns
                   : key == juce::KeyPress::downKey  ?  columns
                                                     :  0;

    if (step != 0)
    {
        choose (static_cast<Kind> (juce::jlimit (0, numKinds - 1, (int) chosen + step)));
        cardFor (chosen).grabKeyboardFocus();
        return true;
    }

    return false;
}

void NewTrackDialog::mouseDown (const juce::MouseEvent& e)
{
    if (! panel.contains (e.getPosition()))
        close();
}

void NewTrackDialog::paint (juce::Graphics& g)
{
    // Dim the app behind the dialog.
    g.fillAll (theme().scrim);

    theme().dialogShadow.drawForRectangle (g, panel);
    g.setColour (theme().dialogBg);
    g.fillRoundedRectangle (panel.toFloat(), size::radiusPill);
    g.setColour (theme().border);
    g.drawRoundedRectangle (panel.toFloat().reduced (0.5f), size::radiusPill, 1.0f);

    auto r = panel.reduced (28, 24);
    g.setColour (theme().text);
    g.setFont (fonts::uiBold (textSize::title));
    g.drawText ("Choose a track type", r.removeFromTop (26), juce::Justification::centredLeft);

    if (recordsInput())
    {
        g.setColour (theme().textSecondary);
        g.setFont (fonts::ui (textSize::md));
        g.drawText ("Input", audioOptionsArea.withWidth (60), juce::Justification::centredLeft);

        if (chosen == Kind::guitar)
            g.drawText ("Plays", playsLabelArea, juce::Justification::centredRight);

        if (inputs.empty())
            g.drawText ("Connect an audio interface, or pick one in File > Audio & MIDI Settings.",
                        audioOptionsArea.translated (0, 34), juce::Justification::centredLeft, true);
    }
}

void NewTrackDialog::resized()
{
    panel = getLocalBounds().withSizeKeepingCentre (panelWidth, panelHeight);

    auto r = panel.reduced (28, 24);
    r.removeFromTop (26 + 18);

    // Rows of three cards; a shorter last row is centered.
    const int gap = 16, cardHeight = 200;
    const int cardWidth = (r.getWidth() - gap * (columns - 1)) / columns;

    for (int row = 0; row * columns < numKinds; ++row)
    {
        if (row > 0)
            r.removeFromTop (gap);

        auto cards = r.removeFromTop (cardHeight);
        const int first = row * columns, count = juce::jmin (columns, numKinds - first);
        cards = cards.withSizeKeepingCentre (cardWidth * count + gap * (count - 1), cardHeight);

        for (int i = first; i < first + count; ++i)
        {
            cardFor (static_cast<Kind> (i)).setBounds (cards.removeFromLeft (cardWidth));
            cards.removeFromLeft (gap);
        }
    }

    r.removeFromTop (16);
    audioOptionsArea = r.removeFromTop (30);
    auto options = audioOptionsArea;
    options.removeFromLeft (60);
    inputBox.setBounds (options.removeFromLeft (160));
    options.removeFromLeft (16);
    playsBox.setBounds (options.removeFromRight (110));
    playsLabelArea = options.removeFromRight (56).withTrimmedRight (10);
    monitorToggle.setBounds (options);

    auto buttons = panel.reduced (28, 24).removeFromBottom (36);
    createButton.setBounds (buttons.removeFromRight (chosen == Kind::sampler ? 140 : 110));
    buttons.removeFromRight (10);
    cancelButton.setBounds (buttons.removeFromRight (100));
}

} // namespace ikaros
