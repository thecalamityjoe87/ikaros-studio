#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace ikaros
{

//==============================================================================
/**
    Draws a juce::DropShadow, but blurs it only once. DropShadow::drawForPath
    draws the shape into an image and blurs it on every paint, which adds up
    when many shadows repaint at once (the pedalboard under a scrolling
    dialog, a pedal being dragged). This keeps the blurred image and only
    makes a new one when the shape's size or the shadow changes; moving the
    shape just draws the same image somewhere else.

    One cache per shape size: a parent drawing many same-sized shadows (like
    the pedalboard's pedals) needs only one.
*/
class CachedShadow
{
public:
    void drawForRoundedRectangle (juce::Graphics&, const juce::DropShadow&, juce::Rectangle<float> area, float cornerRadius);
    void drawForEllipse (juce::Graphics&, const juce::DropShadow&, juce::Rectangle<float> area);

private:
    // What the cached image was made for. The position's fraction of a
    // pixel counts: the shape is drawn at it, so the edges line up.
    struct Key
    {
        juce::DropShadow shadow;
        juce::Rectangle<float> shape;   // its size, at its position's fraction of a pixel
        float cornerRadius = 0.0f;      // -1 for an ellipse

        bool operator== (const Key& o) const
        {
            return shadow.colour == o.shadow.colour && shadow.radius == o.shadow.radius
                    && shape == o.shape && cornerRadius == o.cornerRadius;
        }
    };

    Key key;
    juce::Image image;   // the blurred shape (one channel), with room for the blur around it

    void draw (juce::Graphics&, const juce::DropShadow&, juce::Rectangle<float> area, float cornerRadius);
};

} // namespace ikaros
