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
void IconButton::setColours (Colours c)             { colours = c; repaint(); }

IconButton::Colours IconButton::raisedColours()
{
    return { colour::raised, colour::text, colour::accent, colour::textOnAccent };
}

IconButton::Colours IconButton::accentColours()
{
    return { colour::accent, colour::textOnAccent, colour::accentLight, colour::textOnAccent };
}

IconButton::Colours IconButton::ghostColours()
{
    return { juce::Colours::transparentBlack, colour::textMenu, colour::raised, colour::text };
}

IconButton::Colours IconButton::neutralToggleColours()
{
    return { colour::raised, colour::text, colour::neutralActive, colour::text, colour::border };
}

IconButton::Colours IconButton::segmentColours()
{
    return { juce::Colours::transparentBlack, colour::textSecondary, colour::neutralActive, colour::text };
}

IconButton::Colours IconButton::primaryColours()
{
    // Neutral, like everything that isn't "on"; the light border and bold
    // text are what mark it as the main action.
    return { colour::neutralActive, colour::text, colour::neutralActive, colour::text, colour::selectedBorder };
}

void IconButton::paintButton (juce::Graphics& g, bool over, bool down)
{
    auto r = getLocalBounds().toFloat();
    const bool on = getToggleState();

    auto bg = on ? colours.backgroundOn : colours.background;
    auto fg = on ? colours.foregroundOn : colours.foreground;

    auto border = colours.border;

    if (! isEnabled())
    {
        // Clearly "not now": grey icon/text, and a flatter, fainter button.
        fg = colour::textDisabled;
        bg = bg.withMultipliedAlpha (0.5f);
        border = border.withMultipliedAlpha (0.5f);
    }
    else if ((over || down) && colours.hoverBackground.has_value())
    {
        bg = down ? colours.hoverBackground->darker (0.15f) : *colours.hoverBackground;
        fg = colours.hoverForeground.value_or (fg);
    }
    else if (down)
        bg = (bg.isTransparent() ? colour::raised : bg).brighter (0.12f);
    else if (over)
        bg = (bg.isTransparent() ? colour::raised : bg).brighter (0.06f);

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
