#include "ui/titlebar/MenuBar.h"
#include "ui/theme/Fonts.h"
#include "ui/theme/Tokens.h"

namespace ikaros
{

using namespace tokens;

MenuBar::MenuBar (juce::MenuBarModel* m)
    : model (m),
     #if JUCE_MAC
      menus (nullptr)        // the model is shown in the native macOS menu bar instead
     #else
      menus (m)
     #endif
{
    addAndMakeVisible (menus);
    addAndMakeVisible (windowControls);
}

void MenuBar::setSessionStatus (const juce::String& name, bool /*isUntitled*/, bool unsaved)
{
    if (name != sessionName || unsaved != unsavedChanges)
    {
        sessionName = name;
        unsavedChanges = unsaved;
        setTooltip (unsaved ? "Unsaved changes" : juce::String());
        repaint();
    }
}

void MenuBar::updateWindowState()
{
    windowControls.update();

    // Keep the OS window title (taskbar, Alt+Tab) in step with ours.
    if (auto* window = findParentComponentOfClass<juce::DocumentWindow>())
    {
        auto title = sessionName + (unsavedChanges ? " *" : "") + " - Ikaros Studio";

        if (window->getName() != title)
            window->setName (title);
    }
}

int MenuBar::getMenusWidth()
{
   #if JUCE_MAC
    return 0;
   #else
    // Size the menu bar to its items, so the empty space after "Help" belongs
    // to the strip itself and can be used to drag the window.
    int width = 0;
    auto names = model->getMenuBarNames();

    for (int i = 0; i < names.size(); ++i)
        width += getLookAndFeel().getMenuBarItemWidth (menus, i, names[i]);

    return width;
   #endif
}

juce::Component::WindowControlKind MenuBar::getWindowControlAt (juce::Point<float> p) const
{
    if (windowControls.getBounds().toFloat().contains (p))
        return windowControls.getControlAt (p - windowControls.getPosition().toFloat());

    if (menus.isVisible() && menus.getBounds().toFloat().contains (p))
        return WindowControlKind::client;   // the menus must stay clickable

    return getLocalBounds().toFloat().contains (p) ? WindowControlKind::caption
                                                   : WindowControlKind::client;
}

//==============================================================================
void MenuBar::mouseDown (const juce::MouseEvent& e)
{
    auto* window = getWindow();
    auto* maximisable = dynamic_cast<MaximisableWindow*> (window);
    draggingWindow = window != nullptr && maximisable != nullptr;

    if (! draggingWindow)
        return;

    restoreOnDrag = maximisable->isMaximised();
    dragOffset = e.getScreenPosition() - window->getScreenPosition();
}

void MenuBar::mouseDrag (const juce::MouseEvent& e)
{
    auto* window = getWindow();
    auto* maximisable = dynamic_cast<MaximisableWindow*> (window);

    if (! draggingWindow || maximisable == nullptr)
        return;

    // Wait for a deliberate drag, so a plain click or double-click isn't one.
    if (e.getDistanceFromDragStart() < 4)
        return;

    // Where supported (Linux), let the window manager run the drag: it
    // un-maximises and moves the window like any native one, with snapping.
    if (maximisable->startWindowManagerDrag())
    {
        draggingWindow = false;
        return;
    }

    if (restoreOnDrag)
    {
        restoreOnDrag = false;

        // Remember where along the title bar the mouse was, as a proportion,
        // then restore and keep the mouse at the same proportion of the
        // (narrower) restored window.
        const auto proportion = (float) dragOffset.x / (float) juce::jmax (1, window->getWidth());
        maximisable->setMaximised (false);
        dragOffset.x = juce::roundToInt (proportion * (float) window->getWidth());
    }

    window->setTopLeftPosition (e.getScreenPosition() - dragOffset);
}

void MenuBar::mouseDoubleClick (const juce::MouseEvent&)
{
    if (auto* window = dynamic_cast<MaximisableWindow*> (getWindow()))
        window->toggleMaximised();
}

//==============================================================================
void MenuBar::paint (juce::Graphics& g)
{
    g.fillAll (colour::menuBarBg);

    g.setColour (colour::divider);
    g.fillRect (getLocalBounds().removeFromBottom (1));

    // Centred title: [dot] Session name · Ikaros Studio
    auto nameFont = fonts::uiSemiBold (textSize::sm);
    auto appFont = fonts::uiBold (textSize::sm);
    const juce::String appText = juce::String::fromUTF8 ("  \xc2\xb7  Ikaros Studio");   // " · Ikaros Studio"

    const float dotSpace = unsavedChanges ? 12.0f : 0.0f;
    const float nameWidth = juce::GlyphArrangement::getStringWidth (nameFont, sessionName);
    const float appWidth = juce::GlyphArrangement::getStringWidth (appFont, appText);
    const float total = dotSpace + nameWidth + appWidth;

    // Centre on the whole strip, but stay clear of the menus and buttons.
    auto bar = getLocalBounds().withTrimmedBottom (1).toFloat();
    auto space = freeArea.toFloat().reduced (12.0f, 0.0f);
    auto x = juce::jlimit (space.getX(), juce::jmax (space.getX(), space.getRight() - total), bar.getCentreX() - total * 0.5f);
    auto title = juce::Rectangle<float> (x, bar.getY(), juce::jmin (total, space.getWidth()), bar.getHeight());

    if (unsavedChanges)
    {
        g.setColour (colour::accent);
        g.fillEllipse (title.removeFromLeft (dotSpace).withSizeKeepingCentre (6.0f, 6.0f).translated (-3.0f, 0.0f));
    }

    g.setColour (colour::text);
    g.setFont (nameFont);
    g.drawText (sessionName, title.removeFromLeft (juce::jmin (nameWidth, title.getWidth())), juce::Justification::centredLeft, true);

    // The "·" separator stays grey; the app name keeps its amber brand colour.
    const juce::String separator = juce::String::fromUTF8 ("  \xc2\xb7  ");
    const float separatorWidth = juce::GlyphArrangement::getStringWidth (appFont, separator);
    g.setFont (appFont);
    g.setColour (colour::textSecondary);
    g.drawText (separator, title.removeFromLeft (juce::jmin (separatorWidth, title.getWidth())), juce::Justification::centredLeft, true);

    g.setColour (colour::accent);
    g.setFont (fonts::uiBold (textSize::sm));
    g.drawText ("Ikaros Studio", title, juce::Justification::centredLeft, true);
}

void MenuBar::resized()
{
    auto r = getLocalBounds().withTrimmedBottom (1);

   #if JUCE_MAC
    windowControls.setBounds (r.removeFromLeft (WindowControls::preferredWidth));
   #else
    windowControls.setBounds (r.removeFromRight (WindowControls::preferredWidth));
   #endif

    r.removeFromLeft (6);

    // Item widths depend on the menu bar's height (JUCE adds padding based on
    // it), so give the menus their height before measuring them.
    menus.setBounds (r.withWidth (0));
    menus.setBounds (r.removeFromLeft (juce::jmin (getMenusWidth(), r.getWidth())));

    freeArea = r;
}

} // namespace ikaros
