#include "ui/widgets/MeterSlider.h"
#include "ui/widgets/PaintProbe.h"
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
    IKAROS_PERF_PAINT ("MeterSlider");
    // juce::Slider normally asks the LookAndFeel to draw it. This draws it
    // itself: the meter bars across the handle's travel, then the handle.
    // getPositionOfValue gives the handle's x for a value, so the bars line
    // up with where the handle can go.
    const auto start = (float) getPositionOfValue (getMinimum());
    const auto end = (float) getPositionOfValue (getMaximum());
    const auto centreY = (float) getHeight() * 0.5f;

    const float barHeight = 4.0f, barGap = 2.0f;
    auto bars = juce::Rectangle<float> (start, centreY - barHeight - barGap * 0.5f, end - start, barHeight * 2.0f + barGap);
    const auto unlit = style == Style::trackHeader ? theme().volumeSliderTrack : theme().sliderTrack;
    LevelMeter::paintBar (g, bars.removeFromTop (barHeight), left, LevelMeter::Orientation::horizontal, unlit);
    bars.removeFromTop (barGap);
    LevelMeter::paintBar (g, bars.removeFromTop (barHeight), right, LevelMeter::Orientation::horizontal, unlit);

    // The handle, with a ring so it stands out against the lit bars, and a
    // soft shadow in light mode.
    const auto x = (float) getPositionOfValue (getValue());
    auto thumb = juce::Rectangle<float> (12.0f, 12.0f).withCentre ({ x, centreY });
    juce::Path disc;
    disc.addEllipse (thumb.expanded (1.5f));
    juce::DropShadow (theme().thumbShadow, 3, { 0, 1 }).drawForPath (g, disc);
    g.setColour (theme().meterThumbRing);
    g.fillEllipse (thumb.expanded (1.5f));
    // In the "automated" colour while automation moves it (see setAutomatedLook).
    const bool automated = getProperties()["automated"];
    g.setColour (! isEnabled() ? theme().textSecondary : automated ? theme().automated : theme().thumb);
    g.fillEllipse (thumb);

    if (hasKeyboardFocus (false))
        IkarosLookAndFeel::drawFocusRing (g, getLocalBounds().toFloat().expanded (2.0f), size::radiusXs);
}

} // namespace ikaros
