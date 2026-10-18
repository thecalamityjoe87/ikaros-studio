#include "ui/dialogs/NewTrackDialog.h"
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
    Card (Icon i, const juce::String& title, const juce::String& text, juce::Colour c)
        : juce::Button (title), icon (i), description (text), iconColour (c)
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

        g.setColour (on ? colour::selectedRow : (over ? colour::raised : colour::cardBg));
        g.fillRoundedRectangle (r, size::radiusCard);
        g.setColour (on ? colour::selectedBorder : colour::borderSoft);
        g.drawRoundedRectangle (r, size::radiusCard, on ? 2.0f : 1.0f);

        auto content = getLocalBounds().reduced (20);
        auto iconArea = content.removeFromTop (64);
        g.setColour (colour::wellBg);
        g.fillRoundedRectangle (iconArea.withSizeKeepingCentre (64, 64).toFloat(), size::radiusPill);
        icons::draw (g, icon, iconArea.toFloat(), iconColour, 36.0f, 1.6f);

        content.removeFromTop (14);
        g.setColour (colour::text);
        g.setFont (fonts::uiBold (textSize::xxl));
        g.drawText (getName(), content.removeFromTop (22), juce::Justification::centred);

        content.removeFromTop (6);
        g.setColour (colour::textSecondary);
        g.setFont (fonts::ui (textSize::md));
        g.drawFittedText (description, content, juce::Justification::centredTop, 3, 1.0f);

        if (hasKeyboardFocus (false))
            IkarosLookAndFeel::drawFocusRing (g, r, size::radiusCard);
    }

private:
    Icon icon;
    juce::String description;
    juce::Colour iconColour;
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
                                             colour::trackBlue);
    audioCard = std::make_unique<Card> (Icon::mic, "Real Instrument",
                                        "Record a microphone, guitar or anything plugged into your audio interface.",
                                        colour::trackOrange);

    for (auto* card : { instrumentCard.get(), audioCard.get() })
    {
        const bool audio = card == audioCard.get();
        card->onClick = [this, audio] { choose (audio); };
        card->onDoubleClick = [this, audio] { choose (audio); create(); };
        addAndMakeVisible (card);
    }

    // Real Instrument options
    inputs = engine.getTracks().inputs().getAudioInputs();

    for (int i = 0; i < (int) inputs.size(); ++i)
        inputBox.addItem (inputs[(size_t) i].name, i + 1);

    if (inputs.empty())
        inputBox.addItem ("No audio inputs found", 1);

    inputBox.setSelectedItemIndex (0, juce::dontSendNotification);
    inputBox.setEnabled (! inputs.empty());
    inputBox.setTitle ("Input");
    addChildComponent (inputBox);

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

    auto cancelColours = IconButton::raisedColours();
    cancelColours.border = colour::border;
    cancelButton.setColours (cancelColours);
    cancelButton.onClick = [this] { close(); };
    addAndMakeVisible (cancelButton);

    createButton.setColours (IconButton::primaryColours());
    createButton.onClick = [this] { create(); };
    addAndMakeVisible (createButton);

    choose (false);
}

void NewTrackDialog::choose (bool audio)
{
    audioChosen = audio;
    instrumentCard->setToggleState (! audio, juce::dontSendNotification);
    audioCard->setToggleState (audio, juce::dontSendNotification);
    inputBox.setVisible (audio);
    monitorToggle.setVisible (audio);
    repaint();
}

void NewTrackDialog::create()
{
    auto& tracks = engine.getTracks();
    TrackId id = 0;

    if (audioChosen)
    {
        id = tracks.addAudioTrack();

        if (id != 0)
        {
            const auto index = inputBox.getSelectedItemIndex();

            if (juce::isPositiveAndBelow (index, (int) inputs.size()))
                tracks.inputs().setTrackInput (id, inputs[(size_t) index].id);

            if (monitorToggle.getToggleState())
                tracks.inputs().setMonitoring (id, true);
        }
    }
    else
    {
        id = tracks.addInstrumentTrack();
    }

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
    if (key == juce::KeyPress::leftKey)              { choose (false); instrumentCard->grabKeyboardFocus(); return true; }
    if (key == juce::KeyPress::rightKey)             { choose (true); audioCard->grabKeyboardFocus(); return true; }
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
    g.fillAll (colour::scrim);

    shadow::dialog.drawForRectangle (g, panel);
    g.setColour (colour::toolbarBg);
    g.fillRoundedRectangle (panel.toFloat(), size::radiusPill);
    g.setColour (colour::border);
    g.drawRoundedRectangle (panel.toFloat().reduced (0.5f), size::radiusPill, 1.0f);

    auto r = panel.reduced (28, 24);
    g.setColour (colour::text);
    g.setFont (fonts::uiBold (textSize::title));
    g.drawText ("Choose a track type", r.removeFromTop (26), juce::Justification::centredLeft);

    if (audioChosen)
    {
        g.setColour (colour::textSecondary);
        g.setFont (fonts::ui (textSize::md));
        g.drawText ("Input", audioOptionsArea.withWidth (60), juce::Justification::centredLeft);

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

    auto cards = r.removeFromTop (210);
    const int gap = 16;
    const int cardWidth = (cards.getWidth() - gap) / 2;
    instrumentCard->setBounds (cards.removeFromLeft (cardWidth));
    cards.removeFromLeft (gap);
    audioCard->setBounds (cards);

    r.removeFromTop (16);
    audioOptionsArea = r.removeFromTop (30);
    auto options = audioOptionsArea;
    options.removeFromLeft (60);
    inputBox.setBounds (options.removeFromLeft (160));
    options.removeFromLeft (16);
    monitorToggle.setBounds (options);

    auto buttons = panel.reduced (28, 24).removeFromBottom (36);
    createButton.setBounds (buttons.removeFromRight (110));
    buttons.removeFromRight (10);
    cancelButton.setBounds (buttons.removeFromRight (100));
}

} // namespace ikaros
