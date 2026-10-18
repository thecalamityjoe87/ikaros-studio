#include "ui/widgets/MeterSlider.h"
#include "ui/theme/IkarosLookAndFeel.h"
#include "ui/theme/Tokens.h"

namespace ikaros
{

using namespace tokens;

MeterSlider::MeterSlider()
    : juce::Slider (juce::Slider::LinearHorizontal, juce::Slider::NoTextBox)
{
    setRange (0.0, 1.0);
}

void MeterSlider::setLevel (StereoLevel level)
{
    auto newLeft = LevelMeter::fallTowards (left, LevelMeter::dbToProportion (level.leftDb));
    auto newRight = LevelMeter::fallTowards (right, LevelMeter::dbToProportion (level.rightDb));

    if (std::abs (newLeft - left) > 0.002f || std::abs (newRight - right) > 0.002f)
    {
        left = newLeft;
        right = newRight;
        repaint();
    }
}

void MeterSlider::paint (juce::Graphics& g)
{
    // juce::Slider normally asks the LookAndFeel to draw it. This draws it
    // itself: the meter bars across the handle's travel, then the handle.
    // getPositionOfValue gives the handle's x for a value, so the bars line
    // up with where the handle can go.
    const auto start = (float) getPositionOfValue (getMinimum());
    const auto end = (float) getPositionOfValue (getMaximum());
    const auto centreY = (float) getHeight() * 0.5f;

    const float barHeight = 4.0f, barGap = 2.0f;
    auto bars = juce::Rectangle<float> (start, centreY - barHeight - barGap * 0.5f, end - start, barHeight * 2.0f + barGap);
    LevelMeter::paintBar (g, bars.removeFromTop (barHeight), left, LevelMeter::Orientation::horizontal);
    bars.removeFromTop (barGap);
    LevelMeter::paintBar (g, bars.removeFromTop (barHeight), right, LevelMeter::Orientation::horizontal);

    // The handle, with a dark ring so it stands out against the lit bars.
    const auto x = (float) getPositionOfValue (getValue());
    auto thumb = juce::Rectangle<float> (12.0f, 12.0f).withCentre ({ x, centreY });
    g.setColour (colour::toolbarBg);
    g.fillEllipse (thumb.expanded (1.5f));
    // In the "automated" colour while automation moves it (see setAutomatedLook).
    const bool automated = getProperties()["automated"];
    g.setColour (! isEnabled() ? colour::textSecondary : automated ? colour::automated : colour::thumb);
    g.fillEllipse (thumb);

    if (hasKeyboardFocus (false))
        IkarosLookAndFeel::drawFocusRing (g, getLocalBounds().toFloat().expanded (2.0f), size::radiusXs);
}

} // namespace ikaros
