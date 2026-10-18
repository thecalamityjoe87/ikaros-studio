#include "ui/titlebar/WindowControls.h"
#include "ui/theme/Tokens.h"

namespace ikaros
{

using namespace tokens;

WindowControls::WindowControls()
{
    auto normal = IconButton::Colours { juce::Colours::transparentBlack, colour::textMenu,
                                        juce::Colours::transparentBlack, colour::textMenu };
    normal.hoverBackground = colour::raised;
    normal.hoverForeground = colour::text;

    auto close = normal;
    close.hoverBackground = colour::record.darker (0.15f);
    close.hoverForeground = colour::textOnColour;

    for (auto* b : { &minimiseButton, &maximiseButton, &closeButton })
    {
        b->setColours (b == &closeButton ? close : normal);
        b->setCornerRadius (0.0f);
        b->setIconSize (16.0f);
        addAndMakeVisible (b);
    }

    // The DocumentWindow already knows how to do each of these; we just call
    // the same methods its built-in title-bar buttons would.
    minimiseButton.onClick = [this] { if (auto* w = getWindow()) w->minimiseButtonPressed(); };
    maximiseButton.onClick = [this] { if (auto* w = getMaximisableWindow()) w->toggleMaximised(); };
    closeButton.onClick    = [this] { if (auto* w = getWindow()) w->closeButtonPressed(); };
}

juce::DocumentWindow* WindowControls::getWindow() const
{
    // findParentComponentOfClass walks up the parent chain and returns the
    // first ancestor of that type (or nullptr).
    return findParentComponentOfClass<juce::DocumentWindow>();
}

MaximisableWindow* WindowControls::getMaximisableWindow() const
{
    // dynamic_cast across to the interface: the top-level window implements it.
    return dynamic_cast<MaximisableWindow*> (getTopLevelComponent());
}

void WindowControls::update()
{
    auto* window = getMaximisableWindow();
    const bool maximised = window != nullptr && window->isMaximised();

    if (maximised != showingRestore)
    {
        showingRestore = maximised;
        maximiseButton.setIcon (maximised ? Icon::windowRestore : Icon::windowMaximise);
        maximiseButton.setTitle (maximised ? "Restore" : "Maximize");
        maximiseButton.setTooltip (maximised ? "Restore" : "Maximize");
    }
}

juce::Component::WindowControlKind WindowControls::getControlAt (juce::Point<float> p) const
{
    // Plain rectangle tests only (see the warning on Component::findControlAtPoint).
    if (minimiseButton.getBounds().toFloat().contains (p))  return WindowControlKind::minimise;
    if (maximiseButton.getBounds().toFloat().contains (p))  return WindowControlKind::maximise;
    if (closeButton.getBounds().toFloat().contains (p))     return WindowControlKind::close;
    return WindowControlKind::caption;
}

void WindowControls::resized()
{
    auto r = getLocalBounds();

   #if JUCE_MAC
    // macOS convention: close on the left.
    closeButton.setBounds (r.removeFromLeft (buttonWidth));
    minimiseButton.setBounds (r.removeFromLeft (buttonWidth));
    maximiseButton.setBounds (r.removeFromLeft (buttonWidth));
   #else
    minimiseButton.setBounds (r.removeFromLeft (buttonWidth));
    maximiseButton.setBounds (r.removeFromLeft (buttonWidth));
    closeButton.setBounds (r.removeFromLeft (buttonWidth));
   #endif
}

} // namespace ikaros
