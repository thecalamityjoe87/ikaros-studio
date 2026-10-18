#include "ui/titlebar/MenuBar.h"
#include "BinaryData.h"
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

    // ImageCache decodes the embedded PNG once and keeps it, so asking for it
    // again (e.g. after the title bar is recreated) costs nothing.
    logo = juce::ImageCache::getFromMemory (BinaryData::io_github_thecalamityjoe87_IkarosStudio_png,
                                            BinaryData::io_github_thecalamityjoe87_IkarosStudio_pngSize);
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

    // Centred branding: [logo] Ikaros Studio · Session name [dot]
    const auto appFont = fonts::uiBold (textSize::sm);
    const auto nameFont = fonts::ui (textSize::sm);
    const juce::String appText ("Ikaros Studio");
    const auto separator = juce::String::fromUTF8 ("  \xc2\xb7  ");   // " · "

    const float logoSize = (float) size::logoSize;
    const float logoGap = 8.0f;
    const float dotSpace = unsavedChanges ? 12.0f : 0.0f;
    const float appWidth = juce::GlyphArrangement::getStringWidth (appFont, appText);
    const float separatorWidth = juce::GlyphArrangement::getStringWidth (nameFont, separator);
    const float nameWidth = juce::GlyphArrangement::getStringWidth (nameFont, sessionName);
    const float total = logoSize + logoGap + appWidth + separatorWidth + nameWidth + dotSpace;

    // Centre on the whole strip, but stay clear of the menus and buttons.
    auto bar = getLocalBounds().withTrimmedBottom (1).toFloat();
    auto space = freeArea.toFloat().reduced (12.0f, 0.0f);
    auto x = juce::jlimit (space.getX(), juce::jmax (space.getX(), space.getRight() - total), bar.getCentreX() - total * 0.5f);
    auto title = juce::Rectangle<float> (x, bar.getY(), juce::jmin (total, space.getWidth()), bar.getHeight());

    // Takes up to `width` from the left of what's left of the title.
    auto take = [&title] (float width) { return title.removeFromLeft (juce::jmin (width, title.getWidth())); };

    if (logo.isValid())
    {
        // The PNG has a transparent margin around its rounded tile (16px of
        // 256, as in the SVG). Draw just the tile at logoSize, and outline it:
        // the tile is about the title bar's own colour, so it would vanish.
        const auto tile = take (logoSize).withSizeKeepingCentre (logoSize, logoSize);
        const auto source = logo.getBounds().reduced (logo.getWidth() / 16);
        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        g.drawImage (logo, tile.getX(), tile.getY(), tile.getWidth(), tile.getHeight(),
                     source.getX(), source.getY(), source.getWidth(), source.getHeight());

        const float tileRadius = logoSize * 52.0f / 224.0f;   // the SVG tile's rx, scaled
        g.setColour (colour::logoOutline);
        g.drawRoundedRectangle (tile.reduced (0.5f), tileRadius, 1.0f);
        take (logoGap);
    }
    else
    {
        take (logoSize + logoGap);
    }

    g.setColour (colour::text);
    g.setFont (appFont);
    g.drawText (appText, take (appWidth), juce::Justification::centredLeft, true);

    g.setColour (colour::textSecondary);
    g.setFont (nameFont);
    g.drawText (separator, take (separatorWidth), juce::Justification::centredLeft, true);
    g.drawText (sessionName, take (nameWidth), juce::Justification::centredLeft, true);

    if (unsavedChanges)
        g.fillEllipse (take (dotSpace).withSizeKeepingCentre (6.0f, 6.0f).translated (3.0f, 0.0f));
}

void MenuBar::resized()
{
    auto r = getLocalBounds().withTrimmedBottom (1);

   #if JUCE_MAC
    windowControls.setBounds (r.removeFromLeft (WindowControls::preferredWidth));
   #else
    windowControls.setBounds (r.removeFromRight (WindowControls::preferredWidth));
   #endif

    // The menus start at the very left: each item has half the bar's height
    // of padding built in, which puts "File" on the panels' 14px edge.

    // Item widths depend on the menu bar's height (JUCE adds padding based on
    // it), so give the menus their height before measuring them.
    menus.setBounds (r.withWidth (0));
    menus.setBounds (r.removeFromLeft (juce::jmin (getMenusWidth(), r.getWidth())));

    freeArea = r;
}

} // namespace ikaros
