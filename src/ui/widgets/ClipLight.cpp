#include "ui/widgets/ClipLight.h"
#include "ui/widgets/PaintProbe.h"
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
    IKAROS_PERF_PAINT ("ClipLight");
    // A small warning sign, centred: a rounded triangle with "!" in it,
    // outlined in grey when off, filled red when lit.
    const auto side = (float) juce::jmin (getWidth(), getHeight());
    const auto r = getLocalBounds().toFloat().withSizeKeepingCentre (side, side);
    const auto inset = side * 0.06f;

    juce::Path triangle;
    triangle.addTriangle (r.getCentreX(), r.getY() + inset, r.getRight() - inset, r.getBottom() - inset,
                          r.getX() + inset, r.getBottom() - inset);
    triangle = triangle.createPathWithRoundedCorners (side * 0.12f);

    if (clipped)
    {
        g.setColour (theme().record);
        g.fillPath (triangle);
    }
    else
    {
        g.setColour (theme().textDisabled);
        g.strokePath (triangle, juce::PathStrokeType (juce::jmax (1.0f, side * 0.1f)));
    }

    // The "!": a bar and a dot.
    const auto stroke = juce::jmax (1.2f, side * 0.13f);
    g.setColour (clipped ? theme().textOnColour : theme().textDisabled);
    g.fillRoundedRectangle (juce::Rectangle<float> (stroke, side * 0.28f).withCentre ({ r.getCentreX(), r.getY() + side * 0.47f }),
                            stroke * 0.5f);
    g.fillEllipse (juce::Rectangle<float> (stroke, stroke).withCentre ({ r.getCentreX(), r.getY() + side * 0.77f }));
}

void ClipLight::mouseDown (const juce::MouseEvent&)
{
    if (clipped && onClear)
        onClear();
}

} // namespace ikaros
