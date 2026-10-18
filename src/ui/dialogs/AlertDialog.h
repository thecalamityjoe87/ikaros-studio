#pragma once

#include "ui/widgets/IconButton.h"
#include <functional>

namespace ikaros
{

//==============================================================================
/**
    The app's question / warning box ("Save changes?", "Recover unsaved
    work?", "Couldn't remove the loop"): a title, a message and a row of
    buttons, drawn like our other dialogs instead of JUCE's AlertWindow.

    Shown inside the window of the options' associated component (the main
    window if there's none), over a dimmed background, and modal: nothing
    else in the app takes clicks or keys until it's answered. Deletes itself
    when closed.

    Takes the same juce::MessageBoxOptions as AlertWindow::showAsync, and the
    callback gets the same result: 1 for the first button, 2 for the second,
    and 0 for the last one (also Escape). The first button is the main action
    and has the keyboard focus, so Return picks it. The icon type is ignored.
*/
class AlertDialog  : public juce::Component,
                     private juce::ComponentListener
{
public:
    static void show (const juce::MessageBoxOptions&, std::function<void (int result)> onClose = nullptr);

    /** The same, with a tick box under the message (e.g. "Remember my choice"),
        whose state is passed to the callback. */
    static void show (const juce::MessageBoxOptions&, const juce::String& tickBoxText,
                      std::function<void (int result, bool ticked)> onClose);

    ~AlertDialog() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    AlertDialog (const juce::MessageBoxOptions&, const juce::String& tickBoxText,
                 std::function<void (int, bool)> onClose);

    void close (int result);

    // Follows the window it's shown in: its size, and its deletion.
    void componentMovedOrResized (juce::Component&, bool wasMoved, bool wasResized) override;
    void componentBeingDeleted (juce::Component&) override;

    juce::String title;
    juce::AttributedString messageText;
    juce::TextLayout message;   // laid out for the panel's width in resized()
    juce::OwnedArray<IconButton> buttons;   // in the options' order; buttons[0] is the main action
    std::unique_ptr<juce::ToggleButton> tickBox;
    std::function<void (int, bool)> onClose;

    juce::Component::SafePointer<juce::Component> host;
    juce::Rectangle<int> panel, titleArea, messageArea;
    bool closing = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AlertDialog)
};

} // namespace ikaros
