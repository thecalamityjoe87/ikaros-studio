#include "ui/library/PreviewButton.h"
#include "ui/theme/Tokens.h"

namespace ikaros
{

using namespace tokens;

namespace
{
    constexpr int rightInset = 6;   // from the row's right edge
}

PreviewButton::PreviewButton (const juce::String& itemName)
    : IconButton ("Preview " + itemName, Icon::play)
{
    setCornerRadius ((float) size::roundButton / 2.0f);
    setIconSize (12.0f);
    setMouseClickGrabsKeyboardFocus (false);
    setState (false);
}

void PreviewButton::setState (bool isPlaying, bool available)
{
    playing = isPlaying;

    setIcon (playing ? Icon::stop : Icon::play);
    setTooltip (! available ? juce::String ("Preview isn't available for this sound yet")
                            : playing ? juce::String ("Stop") : juce::String ("Preview"));

    // Light, so it reads as "click to hear" without using the accent (the
    // accent only means "on"). Greyed when there's nothing to play.
    Colours c { colour::previewFill, colour::previewIcon, colour::previewFill, colour::previewIcon };
    c.hoverBackground = colour::tintLight;

    if (! available)
        c = { colour::buttonFill, colour::textDisabled, colour::buttonFill, colour::textDisabled, colour::buttonBorder };

    setColours (c);
}

void PreviewButton::updateVisibility (juce::Component& row)
{
    // isMouseOver (true) counts children too, so the button stays while the
    // mouse moves from the row onto it. (JUCE updates the component under
    // the mouse before it sends mouseExit, so this is already true inside
    // the row's mouseExit.)
    setVisible (playing || row.isMouseOver (true));
}

juce::Rectangle<int> PreviewButton::getBoundsIn (juce::Rectangle<int> row)
{
    return row.withTrimmedRight (rightInset).removeFromRight (size::roundButton)
              .withSizeKeepingCentre (size::roundButton, size::roundButton);
}

} // namespace ikaros
