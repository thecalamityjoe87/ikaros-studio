#include "ui/library/FreesoundSetupDialog.h"
#include "engine/FreesoundStore.h"
#include "ui/theme/Fonts.h"
#include "ui/theme/Tokens.h"

namespace ikaros
{

using namespace tokens;

void FreesoundSetupDialog::show (juce::Component& parent, AudioEngine& engine)
{
    // Owned by its parent from here on; it deletes itself in close().
    auto* dialog = new FreesoundSetupDialog (engine);
    parent.addAndMakeVisible (dialog);
    dialog->setBounds (parent.getLocalBounds());
    dialog->keyField.grabKeyboardFocus();
}

FreesoundSetupDialog::FreesoundSetupDialog (AudioEngine& e)
    : engine (e)
{
    setTitle ("Freesound API key");
    setWantsKeyboardFocus (true);
    setAlwaysOnTop (true);

    auto grey = IconButton::raisedColours();
    grey.border = colour::border;

    for (auto* b : { &signUpButton, &applyButton, &closeButton, &forgetButton })
    {
        b->setColours (grey);
        b->setFont (fonts::uiSemiBold (textSize::md));
        b->setIconSize (14.0f);
        addAndMakeVisible (b);
    }

    signUpButton.onClick = [] { juce::URL (FreesoundStore::getRegisterUrl()).launchInDefaultBrowser(); };
    applyButton.onClick  = [] { juce::URL (FreesoundStore::getApplyUrl()).launchInDefaultBrowser(); };
    closeButton.onClick  = [this] { close(); };

    forgetButton.setTooltip ("Stop using this key and remove it from this computer");
    forgetButton.onClick = [this]
    {
        engine.getFreesound().forgetKey();
        keyField.clear();
    };

    connectButton.setColours (IconButton::accentColours());
    connectButton.setFont (fonts::uiSemiBold (textSize::md));
    connectButton.onClick = [this] { connect(); };
    addAndMakeVisible (connectButton);

    keyField.setTextToShowWhenEmpty ("Paste your key here", colour::textSecondary);
    keyField.setFont (fonts::ui (textSize::lg));
    keyField.setTitle ("Freesound API key");
    keyField.setIndents (10, 9);
    keyField.setSelectAllWhenFocused (true);
    keyField.onReturnKey = [this] { connect(); };
    keyField.onEscapeKey = [this] { close(); };
    addAndMakeVisible (keyField);

    engine.getFreesound().addChangeListener (this);
    changeListenerCallback (nullptr);
}

FreesoundSetupDialog::~FreesoundSetupDialog()
{
    engine.getFreesound().removeChangeListener (this);
}

void FreesoundSetupDialog::connect()
{
    engine.getFreesound().checkKey (keyField.getText());
}

void FreesoundSetupDialog::changeListenerCallback (juce::ChangeBroadcaster*)
{
    auto& store = engine.getFreesound();
    const bool checking = store.getKeyCheck() == FreesoundStore::KeyCheck::checking;

    connectButton.setEnabled (! checking);
    forgetButton.setVisible (store.hasKey());

    if (store.getKeyCheck() == FreesoundStore::KeyCheck::accepted)
        close();

    repaint();
}

void FreesoundSetupDialog::close()
{
    if (! isVisible())
        return;   // already closing

    setVisible (false);
    juce::Component::unfocusAllComponents();

    juce::MessageManager::callAsync ([safeThis = SafePointer<FreesoundSetupDialog> (this)]
    {
        delete safeThis.getComponent();
    });
}

bool FreesoundSetupDialog::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey)
    {
        close();
        return true;
    }

    return false;
}

void FreesoundSetupDialog::mouseDown (const juce::MouseEvent& e)
{
    if (! panel.contains (e.getPosition()))
        close();
}

//==============================================================================
void FreesoundSetupDialog::paint (juce::Graphics& g)
{
    g.fillAll (colour::scrim);

    shadow::dialog.drawForRectangle (g, panel);
    g.setColour (colour::toolbarBg);
    g.fillRoundedRectangle (panel.toFloat(), size::radiusPill);
    g.setColour (colour::border);
    g.drawRoundedRectangle (panel.toFloat().reduced (0.5f), size::radiusPill, 1.0f);

    auto r = panel.reduced (28, 24);
    g.setColour (colour::text);
    g.setFont (fonts::uiBold (textSize::title));
    g.drawText ("Find loops on Freesound", r.removeFromTop (26), juce::Justification::centredLeft);

    g.setColour (colour::textSecondary);
    g.setFont (fonts::ui (textSize::md));
    g.drawFittedText ("Freesound is a free library of sounds shared by its community. To search it from here you need your "
                      "own free API key. It takes a couple of minutes to get.",
                      r.removeFromTop (46).withTrimmedTop (6), juce::Justification::topLeft, 3, 1.0f);

    auto drawStep = [&g] (int number, juce::Rectangle<int> area, const juce::String& title, const juce::String& detail)
    {
        auto badge = area.removeFromLeft (24).removeFromTop (22).toFloat().withSizeKeepingCentre (22.0f, 22.0f);
        g.setColour (colour::accent);
        g.fillEllipse (badge);
        g.setColour (colour::toolbarBg);
        g.setFont (fonts::uiBold (textSize::sm));
        g.drawText (juce::String (number), badge.toNearestInt(), juce::Justification::centred);

        area.removeFromLeft (8);
        g.setColour (colour::text);
        g.setFont (fonts::uiSemiBold (textSize::lg));
        g.drawText (title, area.removeFromTop (22), juce::Justification::centredLeft);
        g.setColour (colour::textSecondary);
        g.setFont (fonts::ui (textSize::sm));
        g.drawFittedText (detail, area, juce::Justification::topLeft, 3, 1.0f);
    };

    drawStep (1, step1Area, "Create a free Freesound account", "Already have one? Skip to step 2.");
    drawStep (2, step2Area, "Apply for an API key",
              "Log in, then fill in the form: any app name works, for example \"Ikaros Studio\".");
    drawStep (3, step3Area, "Paste your key",
              "On your API keys page, copy the value under \"Client secret/Api key\" (not the Client id).");

    auto& store = engine.getFreesound();
    juce::String status;
    auto statusColour = colour::textSecondary;

    switch (store.getKeyCheck())
    {
        case FreesoundStore::KeyCheck::checking:  status = "Checking your key with Freesound..."; break;
        case FreesoundStore::KeyCheck::rejected:
        case FreesoundStore::KeyCheck::failed:    status = store.getMessage(); statusColour = colour::record; break;
        default:
            if (store.hasKey())
            {
                status = "A key is saved. Paste a new one to replace it.";
                statusColour = colour::effectOn;
            }
            break;
    }

    g.setColour (statusColour);
    g.setFont (fonts::ui (textSize::md));
    g.drawFittedText (status, statusArea, juce::Justification::topLeft, 3, 1.0f);

    g.setColour (colour::textSecondary);
    g.setFont (fonts::ui (textSize::sm));
    g.drawFittedText ("Only loops shared under CC0 or CC BY are shown. Loops you add are credited to their authors on the "
                      "Acknowledgements page of the About window. Your key stays on this computer.",
                      footerArea, juce::Justification::topLeft, 3, 1.0f);
}

void FreesoundSetupDialog::resized()
{
    panel = getLocalBounds().withSizeKeepingCentre (juce::jmin (panelWidth, getWidth() - 40),
                                                    juce::jmin (panelHeight, getHeight() - 40));

    auto r = panel.reduced (28, 24);
    r.removeFromTop (26 + 46 + 10);

    const int buttonWidth = 108;

    step1Area = r.removeFromTop (52);
    signUpButton.setBounds (step1Area.removeFromRight (buttonWidth).withHeight (28).withY (step1Area.getY() + 4));
    step1Area.removeFromRight (8);

    r.removeFromTop (8);
    step2Area = r.removeFromTop (52);
    applyButton.setBounds (step2Area.removeFromRight (buttonWidth).withHeight (28).withY (step2Area.getY() + 4));
    step2Area.removeFromRight (8);

    r.removeFromTop (8);
    step3Area = r.removeFromTop (52);

    auto entry = r.removeFromTop (34).withTrimmedLeft (32);
    connectButton.setBounds (entry.removeFromRight (buttonWidth));
    entry.removeFromRight (8);
    keyField.setBounds (entry);

    r.removeFromTop (8);
    statusArea = r.removeFromTop (44).withTrimmedLeft (32);

    auto buttons = r.removeFromBottom (30);
    closeButton.setBounds (buttons.removeFromRight (buttonWidth));
    buttons.removeFromRight (8);
    forgetButton.setBounds (buttons.removeFromRight (buttonWidth));

    r.removeFromBottom (6);
    footerArea = r.removeFromBottom (48);
}

} // namespace ikaros
