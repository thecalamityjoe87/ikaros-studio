#pragma once

#include "engine/AudioEngine.h"
#include "ui/widgets/IconButton.h"

namespace ikaros
{

//==============================================================================
/**
    Helps the user get a Freesound API key and enter it. Searching Freesound
    needs one, and each user gets their own for free:

      1. Sign up at freesound.org
      2. Apply for an API key
      3. Paste the "Client secret/Api key" here

    The key is tried with Freesound before it's kept, and the window closes
    when Freesound accepts it. It's also how the key is changed later.

    Shown inside the main window over a dimmed background, like More
    Sounds. Deletes itself when closed.
*/
class FreesoundSetupDialog  : public juce::Component,
                              private juce::ChangeListener
{
public:
    static void show (juce::Component& parent, AudioEngine&);

    ~FreesoundSetupDialog() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    void mouseDown (const juce::MouseEvent&) override;   // click outside the panel = close

private:
    explicit FreesoundSetupDialog (AudioEngine&);

    AudioEngine& engine;
    juce::TextEditor keyField;
    IconButton signUpButton { "Open the Freesound sign-up page", Icon::openWindow, "Sign Up" };
    IconButton applyButton  { "Open the Freesound API key page", Icon::openWindow, "Get a Key" };
    IconButton connectButton { "Connect", std::nullopt, "Connect" };
    IconButton closeButton { "Close", std::nullopt, "Cancel" };
    IconButton forgetButton { "Forget the key", std::nullopt, "Forget Key" };
    juce::Rectangle<int> panel, step1Area, step2Area, step3Area, statusArea, footerArea;

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void connect();
    void close();

    static constexpr int panelWidth = 560, panelHeight = 490;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FreesoundSetupDialog)
};

} // namespace ikaros
