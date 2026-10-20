#include "ui/widgets/CachedShadow.h"

namespace ikaros
{

void CachedShadow::drawForRoundedRectangle (juce::Graphics& g, const juce::DropShadow& shadow,
                                            juce::Rectangle<float> area, float cornerRadius)
{
    draw (g, shadow, area, juce::jmax (0.0f, cornerRadius));
}

void CachedShadow::drawForEllipse (juce::Graphics& g, const juce::DropShadow& shadow, juce::Rectangle<float> area)
{
    draw (g, shadow, area, -1.0f);
}

void CachedShadow::draw (juce::Graphics& g, const juce::DropShadow& shadow, juce::Rectangle<float> area, float cornerRadius)
{
    if (area.isEmpty() || shadow.radius <= 0)
        return;

    // Split the position into whole pixels (where the image goes) and the
    // fraction left over (where the shape sits inside the image).
    const juce::Point<float> whole (std::floor (area.getX()), std::floor (area.getY()));
    const auto shape = area - whole;
    const int margin = shadow.radius + 1;   // as much room as DropShadow leaves for the blur

    const Key wanted { shadow, shape, cornerRadius };

    if (image.isNull() || ! (key == wanted))
    {
        key = wanted;
        image = juce::Image (juce::Image::SingleChannel,
                             (int) std::ceil (shape.getRight()) + margin * 2,
                             (int) std::ceil (shape.getBottom()) + margin * 2, true);

        {
            juce::Graphics ig (image);
            ig.setColour (juce::Colours::white);
            const auto inImage = shape.translated ((float) margin, (float) margin);

            if (cornerRadius < 0.0f)
                ig.fillEllipse (inImage);
            else
                ig.fillRoundedRectangle (inImage, cornerRadius);
        }

        image.getPixelData()->applySingleChannelBoxBlurEffect (shadow.radius);
    }

    // `true`: a one-channel image is drawn as a mask, in the current colour.
    g.setColour (shadow.colour);
    g.drawImageAt (image, (int) whole.x + shadow.offset.x - margin, (int) whole.y + shadow.offset.y - margin, true);
}

} // namespace ikaros
