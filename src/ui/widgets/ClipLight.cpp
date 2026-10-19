#include "ui/widgets/ClipLight.h"
#include "ui/theme/Tokens.h"

namespace ikaros
{

using namespace tokens;

ClipLight::ClipLight()
{
    setTitle ("Clip light");
    setTooltip ("Lights up red when the input is too loud");
}

void ClipLight::setClipped (bool shouldBeLit)
{
    if (clipped == shouldBeLit)
        return;

    clipped = shouldBeLit;
    setTooltip (clipped ? "The input was too loud and distorted. Turn down the gain on your "
                          "interface, or move back a little. Click to clear."
                        : "Lights up red when the input is too loud");
    setMouseCursor (clipped ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
    repaint();
}

void ClipLight::paint (juce::Graphics& g)
{
    // A small rounded square, centred: dark when off, red when lit.
    const auto side = (float) juce::jmin (getWidth(), getHeight());
    const auto r = getLocalBounds().toFloat().withSizeKeepingCentre (side, side);

    g.setColour (clipped ? theme().record : theme().controlFill);
    g.fillRoundedRectangle (r, side * 0.3f);
}

void ClipLight::mouseDown (const juce::MouseEvent&)
{
    if (clipped && onClear)
        onClear();
}

} // namespace ikaros
