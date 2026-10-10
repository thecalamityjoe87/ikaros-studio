#include "ui/widgets/LevelMeter.h"
#include "ui/theme/Tokens.h"

namespace ikaros
{

using namespace tokens;

LevelMeter::LevelMeter (Orientation o) : orientation (o)
{
    setInterceptsMouseClicks (false, false);
    setAccessible (false);   // decorative; the numbers aren't useful to a screen reader at 30 Hz
}

float LevelMeter::dbToProportion (float db)
{
    // Map -60 dB .. +6 dB onto 0..1, with a mild curve so normal levels sit
    // in the green part of the meter.
    const float minDb = -60.0f, maxDb = 6.0f;
    auto p = (juce::jlimit (minDb, maxDb, db) - minDb) / (maxDb - minDb);
    return std::pow (p, 1.6f);
}

void LevelMeter::setLevel (StereoLevel level)
{
    // Instant attack, gradual release.
    auto fall = [] (float current, float target) { return target > current ? target : current * 0.86f + target * 0.14f; };

    auto newLeft = fall (left, dbToProportion (level.leftDb));
    auto newRight = fall (right, dbToProportion (level.rightDb));

    if (std::abs (newLeft - left) > 0.002f || std::abs (newRight - right) > 0.002f)
    {
        left = newLeft;
        right = newRight;
        repaint();
    }
}

void LevelMeter::reset()
{
    left = right = 0.0f;
    repaint();
}

void LevelMeter::paintBar (juce::Graphics& g, juce::Rectangle<float> r, float value)
{
    const float radius = juce::jmin (r.getWidth(), r.getHeight()) * 0.5f;
    g.setColour (colour::controlFill);
    g.fillRoundedRectangle (r, radius);

    if (value <= 0.001f)
        return;

    // Green → yellow → red, laid along the bar's length.
    const bool horiz = orientation == Orientation::horizontal;
    juce::ColourGradient grad (colour::meterGreen,
                               horiz ? r.getX() : r.getCentreX(), horiz ? r.getCentreY() : r.getBottom(),
                               colour::meterRed,
                               horiz ? r.getRight() : r.getCentreX(), horiz ? r.getCentreY() : r.getY(),
                               false);
    grad.addColour (0.7, colour::meterGreen);
    grad.addColour (0.88, colour::meterYellow);

    auto filled = horiz ? r.withWidth (r.getWidth() * value)
                        : r.withTop (r.getBottom() - r.getHeight() * value);

    g.setGradientFill (grad);
    g.fillRoundedRectangle (filled, radius);
}

void LevelMeter::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();

    if (orientation == Orientation::horizontal)
    {
        auto h = juce::jmin (5.0f, (r.getHeight() - 3.0f) / 2.0f);
        paintBar (g, r.removeFromTop (h), left);
        r.removeFromTop (3.0f);
        paintBar (g, r.removeFromTop (h), right);
    }
    else
    {
        auto w = juce::jmin (8.0f, (r.getWidth() - 5.0f) / 2.0f);
        auto bars = r.withSizeKeepingCentre (w * 2.0f + 5.0f, r.getHeight());
        paintBar (g, bars.removeFromLeft (w), left);
        bars.removeFromLeft (5.0f);
        paintBar (g, bars.removeFromLeft (w), right);
    }
}

} // namespace ikaros
