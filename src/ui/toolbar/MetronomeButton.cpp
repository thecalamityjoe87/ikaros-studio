#include "ui/toolbar/MetronomeButton.h"

namespace ikaros
{

namespace
{
    // The arm in the icon's 24×24 space: from the pivot to (17, 7) at rest.
    const juce::Point<float> pivot { 12.0f, 15.0f };
    const float armLength = pivot.getDistanceFrom ({ 17.0f, 7.0f });
    const float restAngle = std::atan2 (5.0f, 8.0f);
}

MetronomeButton::MetronomeButton() : IconButton ("Metronome", Icon::metronome), angle (restAngle)
{
}

void MetronomeButton::setSwing (std::optional<double> beats)
{
    // One end on even beats, the other on odd ones, slowing at each end like
    // a pendulum. The swing is as wide as the resting lean.
    const float target = beats.has_value() ? restAngle * (float) std::cos (juce::MathConstants<double>::pi * *beats)
                                           : restAngle;

    if (std::abs (target - angle) > 0.005f)
    {
        angle = target;
        repaint();
    }
}

void MetronomeButton::paintIcon (juce::Graphics& g, juce::Rectangle<float> area, juce::Colour colour)
{
    const float size = getIconSize();
    icons::draw (g, Icon::metronomeBody, area, colour, size);

    // Drawn like the icon's other strokes (icons::draw's default width).
    auto box = area.withSizeKeepingCentre (size, size);
    const float scale = size / 24.0f;
    const auto tip = pivot + juce::Point<float> (std::sin (angle), -std::cos (angle)) * armLength;

    juce::Path arm;
    arm.startNewSubPath (pivot);
    arm.lineTo (tip);
    arm.applyTransform (juce::AffineTransform::scale (scale).translated (box.getX(), box.getY()));
    g.strokePath (arm, juce::PathStrokeType (1.8f * scale, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

} // namespace ikaros
