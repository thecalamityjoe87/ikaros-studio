#include "ui/widgets/IconButton.h"
#include "ui/theme/Fonts.h"
#include "ui/theme/IkarosLookAndFeel.h"
#include "ui/theme/Tokens.h"

namespace ikaros
{

using namespace tokens;

IconButton::IconButton (const juce::String& accessibleName, std::optional<Icon> i, const juce::String& t)
    : juce::Button (accessibleName), icon (i), text (t), font (fonts::uiSemiBold (textSize::lg))
{
    // Accessibility: setTitle is what screen readers announce. For icon-only
    // buttons we also show it as a tooltip so sighted users can discover it.
    setTitle (accessibleName);

    if (text.isEmpty())
        setTooltip (accessibleName);

    // Reachable with Tab, but clicking doesn't move keyboard focus here.
    // Otherwise a click on e.g. Mute would leave focus on it, and the next
    // Space press would toggle Mute instead of starting playback.
    setWantsKeyboardFocus (true);
    setMouseClickGrabsKeyboardFocus (false);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void IconButton::setIcon (std::optional<Icon> i)    { icon = i; repaint(); }
void IconButton::setText (const juce::String& t)    { text = t; repaint(); }
void IconButton::setColours (ColourScheme c)        { colourScheme = std::move (c); repaint(); }

IconButton::Colours IconButton::raisedColours()
{
    return { theme().raised, theme().text, theme().accent, theme().textOnAccent, theme().raisedBorder,
             std::nullopt, std::nullopt, theme().accentBorder };
}

IconButton::Colours IconButton::accentColours()
{
    return { theme().accent, theme().textOnAccent, theme().accentLight, theme().textOnAccent, theme().accentBorder };
}

IconButton::Colours IconButton::ghostColours()
{
    return { juce::Colours::transparentBlack, theme().textMenu, theme().raised, theme().text };
}

IconButton::Colours IconButton::neutralToggleColours()
{
    return { theme().raised, theme().text, theme().neutralActive, theme().text, theme().border,
             std::nullopt, std::nullopt, theme().neutralActiveBorder };
}

IconButton::Colours IconButton::segmentColours()
{
    return { juce::Colours::transparentBlack, theme().textSecondary, theme().segmentSelected, theme().text };
}

IconButton::Colours IconButton::primaryColours()
{
    // Neutral, like everything that isn't "on"; the light border and bold
    // text are what mark it as the main action.
    return { theme().neutralActive, theme().text, theme().neutralActive, theme().text, theme().selectedBorder };
}

IconButton::Colours IconButton::borderedColours()
{
    auto c = raisedColours();
    c.border = theme().border;
    c.borderOn.reset();   // the same border when on
    return c;
}

void IconButton::drawSegmentShadow (juce::Graphics& g, const IconButton& segment)
{
    if (! segment.isVisible() || ! segment.getToggleState())
        return;

    // A static member function can read the button's private cornerRadius.
    juce::Path pill;
    pill.addRoundedRectangle (segment.getBounds().toFloat(), segment.cornerRadius);
    juce::DropShadow (theme().segmentShadow, 2, { 0, 1 }).drawForPath (g, pill);
}

IconButton::Colours IconButton::powerColours()
{
    return { theme().controlFill, theme().textSecondary, theme().effectOn, theme().textOnAccent };
}

void IconButton::paintButton (juce::Graphics& g, bool over, bool down)
{
    auto r = getLocalBounds().toFloat();
    const bool on = getToggleState();
    const auto colours = colourScheme();   // asked afresh every paint, so they follow the theme

    auto bg = on ? colours.backgroundOn : colours.background;
    auto fg = on ? colours.foregroundOn : colours.foreground;

    auto border = on ? colours.borderOn.value_or (colours.border) : colours.border;

    if (! isEnabled())
    {
        // Clearly "not now": grey icon/text, and a flatter, fainter button.
        fg = theme().textDisabled;
        bg = bg.withMultipliedAlpha (0.5f);
        border = border.withMultipliedAlpha (0.5f);
    }
    else if ((over || down) && colours.hoverBackground.has_value())
    {
        bg = down ? colours.hoverBackground->darker (0.15f) : *colours.hoverBackground;
        fg = colours.hoverForeground.value_or (fg);
    }
    else if (down)
        bg = (bg.isTransparent() ? theme().raised : bg).brighter (0.12f);
    else if (over)
        bg = (bg.isTransparent() ? theme().raised : bg).brighter (0.06f);

    g.setColour (bg);
    g.fillRoundedRectangle (r, cornerRadius);

    if (! border.isTransparent())
    {
        g.setColour (border);
        g.drawRoundedRectangle (r.reduced (0.5f), cornerRadius, 1.0f);
    }

    auto content = r.reduced (8.0f, 0.0f);

    if (icon.has_value() && text.isNotEmpty())
    {
        // Icon + label, centred together as one group.
        g.setFont (font);
        auto textWidth = juce::GlyphArrangement::getStringWidth (font, text);
        auto groupWidth = iconSize + 6.0f + textWidth;
        auto group = content.withSizeKeepingCentre (juce::jmin (groupWidth, content.getWidth()), content.getHeight());

        icons::draw (g, *icon, group.removeFromLeft (iconSize), iconColour.value_or (fg), iconSize, 2.0f);
        group.removeFromLeft (6.0f);
        g.setColour (fg);
        g.drawText (text, group, juce::Justification::centredLeft, true);
    }
    else if (icon.has_value())
    {
        icons::draw (g, *icon, r, iconColour.value_or (fg), iconSize);
    }
    else
    {
        g.setColour (fg);
        g.setFont (font);
        g.drawFittedText (text, r.toNearestInt().reduced (4, 0), juce::Justification::centred, 1);
    }

    if (hasKeyboardFocus (false))
        IkarosLookAndFeel::drawFocusRing (g, r, cornerRadius);
}

} // namespace ikaros
