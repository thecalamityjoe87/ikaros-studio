#include "ui/dialogs/AlertDialog.h"
#include "ui/theme/Fonts.h"
#include "ui/theme/Tokens.h"

namespace ikaros
{

using namespace tokens;

namespace
{
    constexpr int panelWidth = 440;
    constexpr int padX = 26, padTop = 22, padBottom = 20;
    constexpr int titleHeight = 24, titleGap = 10;
    constexpr int tickBoxHeight = 26, tickBoxGap = 12;
    constexpr int buttonsGap = 22, buttonHeight = 34, buttonSpacing = 10, minButtonWidth = 92;

    // The window's content (below its frame) for the associated component,
    // or the main window when there's none.
    juce::Component* findHost (juce::Component* associated)
    {
        juce::Component* top = associated != nullptr ? associated->getTopLevelComponent() : nullptr;

        if (top == nullptr)
            top = juce::TopLevelWindow::getActiveTopLevelWindow();

        if (top == nullptr && juce::TopLevelWindow::getNumTopLevelWindows() > 0)
            top = juce::TopLevelWindow::getTopLevelWindow (0);

        if (auto* window = dynamic_cast<juce::ResizableWindow*> (top))
            if (auto* content = window->getContentComponent())
                return content;

        return top;
    }
}

//==============================================================================
void AlertDialog::show (const juce::MessageBoxOptions& options, std::function<void (int)> onClose)
{
    show (options, {}, [onClose = std::move (onClose)] (int result, bool)
    {
        if (onClose)
            onClose (result);
    });
}

void AlertDialog::show (const juce::MessageBoxOptions& options, const juce::String& tickBoxText,
                        std::function<void (int, bool)> onClose)
{
    auto* host = findHost (options.getAssociatedComponent());

    if (host == nullptr)
    {
        jassertfalse;   // no window to show it in

        if (onClose)
            juce::MessageManager::callAsync ([onClose] { onClose (0, false); });

        return;
    }

    // Owned by the window from here on; it deletes itself in close(), or
    // with the window.
    auto* dialog = new AlertDialog (options, tickBoxText, std::move (onClose));
    dialog->host = host;
    host->addComponentListener (dialog);
    host->addAndMakeVisible (dialog);
    dialog->setBounds (host->getLocalBounds());

    // Modal: the rest of the app is blocked, and keys come here, until it closes.
    dialog->enterModalState (false);

    if (! dialog->buttons.isEmpty())
        dialog->buttons[0]->grabKeyboardFocus();
}

AlertDialog::AlertDialog (const juce::MessageBoxOptions& options, const juce::String& tickBoxText,
                          std::function<void (int, bool)> closed)
    : title (options.getTitle()), onClose (std::move (closed))
{
    setTitle (title);
    setDescription (options.getMessage());
    setWantsKeyboardFocus (true);
    setMouseClickGrabsKeyboardFocus (false);   // a click beside the buttons leaves the focus on them
    setAlwaysOnTop (true);
    setFocusContainerType (FocusContainerType::keyboardFocusContainer);

    messageText.append (options.getMessage(), fonts::ui (textSize::lg), theme().textSecondary);
    messageText.setLineSpacing (3.0f);

    if (tickBoxText.isNotEmpty())
    {
        tickBox = std::make_unique<juce::ToggleButton> (tickBoxText);
        addAndMakeVisible (*tickBox);
    }

    // Like AlertWindow: the last button answers 0, the others 1, 2, ...
    const int numButtons = juce::jmax (1, options.getNumButtons());

    for (int i = 0; i < numButtons; ++i)
    {
        const auto text = options.getNumButtons() > 0 ? options.getButtonText (i) : juce::String ("OK");
        const int result = i == numButtons - 1 ? 0 : i + 1;

        auto* button = buttons.add (new IconButton (text, std::nullopt, text));

        button->setColours (i == 0 ? IconButton::primaryColours : IconButton::borderedColours);

        button->onClick = [this, result] { close (result); };
        addAndMakeVisible (button);
    }
}

AlertDialog::~AlertDialog()
{
    if (host != nullptr)
        host->removeComponentListener (this);
}

void AlertDialog::close (int result)
{
    if (closing)
        return;

    closing = true;
    exitModalState();
    setVisible (false);

    // If another alert is waiting underneath, its main button takes the keys.
    if (auto* next = dynamic_cast<AlertDialog*> (juce::Component::getCurrentlyModalComponent()); next != nullptr && ! next->buttons.isEmpty())
        next->buttons[0]->grabKeyboardFocus();
    else
        juce::Component::unfocusAllComponents();

    // Delete ourselves and answer on the next message-loop turn: we're inside
    // one of our own button callbacks right now, and the answer may close the
    // window (quit) or open another dialog.
    juce::MessageManager::callAsync ([safeThis = SafePointer<AlertDialog> (this), result,
                                      ticked = tickBox != nullptr && tickBox->getToggleState(),
                                      callback = onClose]
    {
        delete safeThis.getComponent();

        if (callback)
            callback (result, ticked);
    });
}

void AlertDialog::componentMovedOrResized (juce::Component& c, bool, bool wasResized)
{
    if (wasResized)
        setBounds (c.getLocalBounds());
}

void AlertDialog::componentBeingDeleted (juce::Component&)
{
    // The window is going away without an answer.
    delete this;
}

//==============================================================================
bool AlertDialog::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey)
    {
        close (0);
    }
    else if (key == juce::KeyPress::returnKey)
    {
        // The focused button takes Return itself; this is for when none has focus.
        if (! buttons.isEmpty())
            buttons[0]->triggerClick();
    }
    else if (key.getKeyCode() == juce::KeyPress::tabKey)
    {
        if (auto* focused = juce::Component::getCurrentlyFocusedComponent(); focused != nullptr && isParentOf (focused))
            focused->moveKeyboardFocusToSibling (! key.getModifiers().isShiftDown());
        else if (! buttons.isEmpty())
            buttons[0]->grabKeyboardFocus();
    }

    // Every other key stops here too: shortcuts don't reach the app behind.
    return true;
}

//==============================================================================
void AlertDialog::resized()
{
    const int width = juce::jmin (panelWidth, getWidth() - 32);
    const int textWidth = width - 2 * padX;

    message.createLayout (messageText, (float) textWidth);
    const int messageHeight = (int) std::ceil (message.getHeight());

    const int height = padTop + titleHeight + titleGap + messageHeight
                     + (tickBox != nullptr ? tickBoxGap + tickBoxHeight : 0)
                     + buttonsGap + buttonHeight + padBottom;

    panel = getLocalBounds().withSizeKeepingCentre (width, juce::jmin (height, getHeight() - 32));

    auto r = panel.reduced (padX, 0);
    r.removeFromTop (padTop);
    titleArea = r.removeFromTop (titleHeight);
    r.removeFromTop (titleGap);
    messageArea = r.removeFromTop (messageHeight);

    if (tickBox != nullptr)
    {
        r.removeFromTop (tickBoxGap);
        // The look and feel draws the tick a few pixels in: line it up with the text.
        tickBox->setBounds (r.removeFromTop (tickBoxHeight).withTrimmedLeft (-4));
    }

    // Main action on the right, the last button (Cancel) next to it, and any
    // others ("Don't Save") on the left, as on macOS.
    auto row = panel.reduced (padX, 0).withTrimmedBottom (padBottom).removeFromBottom (buttonHeight);

    auto widthFor = [] (IconButton& b)
    {
        const auto text = b.getTitle();
        return juce::jmax (minButtonWidth, (int) std::ceil (juce::GlyphArrangement::getStringWidth (fonts::uiSemiBold (textSize::lg), text)) + 32);
    };

    for (int i = 0; i < buttons.size(); ++i)
    {
        auto& b = *buttons[i];
        const bool right = i == 0 || i == buttons.size() - 1;

        if (right)
        {
            b.setBounds (row.removeFromRight (widthFor (b)));
            row.removeFromRight (buttonSpacing);
        }
    }

    for (int i = 1; i < buttons.size() - 1; ++i)
    {
        auto& b = *buttons[i];
        b.setBounds (row.removeFromLeft (widthFor (b)));
        row.removeFromLeft (buttonSpacing);
    }
}

void AlertDialog::paint (juce::Graphics& g)
{
    // Dim the app behind the dialog.
    g.fillAll (theme().scrim);

    theme().dialogShadow.drawForRectangle (g, panel);
    g.setColour (theme().dialogBg);
    g.fillRoundedRectangle (panel.toFloat(), size::radiusPill);
    g.setColour (theme().border);
    g.drawRoundedRectangle (panel.toFloat().reduced (0.5f), size::radiusPill, 1.0f);

    g.setColour (theme().text);
    g.setFont (fonts::uiBold (textSize::title));
    g.drawText (title, titleArea, juce::Justification::centredLeft, true);

    message.draw (g, messageArea.toFloat());
}

} // namespace ikaros
