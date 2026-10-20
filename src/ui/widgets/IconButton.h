#pragma once

#include "ui/widgets/Icons.h"
#include <functional>
#include <optional>

namespace ikaros
{

//==============================================================================
/**
    A custom-drawn button with an optional icon and/or text.

    Used for nearly every button in the app (toolbar, transport, M/S, tabs,
    effect pills). Icon-only buttons must be given an accessible name: it's
    used as the screen-reader title and the hover tooltip.
*/
class IconButton  : public juce::Button
{
public:
    struct Colours
    {
        juce::Colour background, foreground;
        juce::Colour backgroundOn, foregroundOn;   // used when the toggle state is on
        juce::Colour border = juce::Colours::transparentBlack;
        std::optional<juce::Colour> hoverBackground;   // e.g. red for a window's close button
        std::optional<juce::Colour> hoverForeground;
        std::optional<juce::Colour> borderOn;          // the border when on (unset: the same as off)
    };

    /** Where a button gets its colours: a function the button calls every
        time it paints, so the colours always come from the current theme.
        Pass a preset by name (no brackets), e.g.
            button.setColours (IconButton::ghostColours);
        or a lambda for your own:
            button.setColours ([] { auto c = IconButton::raisedColours(); c.border = theme().border; return c; });
        A lambda can capture `this` to pick colours from the component's state. */
    using ColourScheme = std::function<Colours()>;

    IconButton (const juce::String& accessibleName, std::optional<Icon> icon = std::nullopt,
                const juce::String& text = {});

    void setIcon (std::optional<Icon>);
    void setText (const juce::String&);
    void setColours (ColourScheme);
    void setCornerRadius (float r)             { cornerRadius = r; repaint(); }
    void setIconSize (float s)                 { iconSize = s; repaint(); }
    void setFont (juce::Font f)                { font = f; repaint(); }
    /** Overrides the icon colour (e.g. the red record dot). */
    void setIconColour (std::optional<juce::Colour> c) { if (c != iconColour) { iconColour = c; repaint(); } }

    /** Preset colour schemes from the design. */
    static Colours raisedColours();     // grey raised button, amber when toggled on (active toggles only)
    static Colours accentColours();     // amber button with dark icon
    static Colours ghostColours();      // transparent until hovered
    static Colours neutralToggleColours();   // grey raised button, neutralActive when toggled on (e.g. library shown)
    static Colours segmentColours();    // a segment/tab: transparent, neutralActive when chosen
    static Colours primaryColours();    // a dialog's main action (Done, Export, Create)
    static Colours borderedColours();   // raisedColours with a border (secondary dialog actions: Cancel, Retry)
    static Colours powerColours();      // an on/off switch for an effect or monitoring: grey, green when on

    /** Draws the soft shadow under a chosen segment (light mode only). Call
        it from the parent's paint(), before the buttons paint: the shadow
        falls outside the button's own bounds. Does nothing if `segment`
        isn't the chosen one or is hidden. */
    static void drawSegmentShadow (juce::Graphics&, const IconButton& segment);

protected:
    void paintButton (juce::Graphics&, bool isMouseOver, bool isButtonDown) override;

private:
    std::optional<Icon> icon;
    std::optional<juce::Colour> iconColour;
    juce::String text;
    ColourScheme colourScheme = raisedColours;
    float cornerRadius = 8.0f;
    float iconSize = 18.0f;
    juce::Font font;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (IconButton)
};

} // namespace ikaros
