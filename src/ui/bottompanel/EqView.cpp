#include "ui/bottompanel/EqView.h"
#include "engine/TrackController.h"
#include "ui/theme/Fonts.h"
#include "ui/theme/Tokens.h"

namespace ikaros
{

using namespace tokens;

namespace
{
    constexpr float displayRangeDb = 24.0f;   // the graph shows -24..+24 dB (bands go to ±20)
    constexpr float pointRadius = 7.0f;
    constexpr int readoutHeight = 40;

    // Each band has a colour of its own, so its point and its readout match.
    const juce::Colour bandColours[EqSettings::numBands] { colour::trackBlue, colour::trackGreen,
                                                           colour::trackYellow, colour::trackPurple };
}

//==============================================================================
EqView::EqView (AudioEngine& e) : engine (e)
{
    setTitle ("EQ");

    auto colours = IconButton::raisedColours();
    colours.border = colour::border;
    powerButton.setColours (colours);
    powerButton.setFont (fonts::uiSemiBold (textSize::md));
    powerButton.setIconSize (14.0f);
    powerButton.setTooltip ("Turn the EQ on or off, to hear the difference");
    powerButton.onClick = [this] { engine.getTracks().eq().setEnabled (target, ! settings.enabled); };
    addAndMakeVisible (powerButton);

    resetButton.setColours (colours);
    resetButton.setFont (fonts::uiSemiBold (textSize::md));
    resetButton.setTooltip ("Put every band back to 0 dB");
    resetButton.onClick = [this] { engine.getTracks().eq().reset (target); };
    addAndMakeVisible (resetButton);
}

void EqView::setTarget (TrackId id, juce::Colour c)
{
    if (id != target)
        dragBand = hoverBand = -1;

    target = id;
    colour = c;
    refresh();
}

void EqView::refresh()
{
    settings = target != 0 ? engine.getTracks().eq().getEq (target) : EqController::getDefaults();
    powerButton.setToggleState (settings.enabled, juce::dontSendNotification);
    powerButton.setText (settings.enabled ? "On" : "Off");
    powerButton.setEnabled (target != 0);
    resetButton.setEnabled (target != 0);
    updateCurve();
    repaint();
}

void EqView::updateCurve()
{
    // One point per column of the graph.
    std::vector<float> frequencies;

    for (int x = 0; x < graphArea.getWidth(); ++x)
        frequencies.push_back (xToFrequency ((float) (graphArea.getX() + x)));

    curve = target != 0 ? engine.getTracks().eq().getResponse (target, frequencies)
                        : std::vector<float> (frequencies.size(), 0.0f);
}

//==============================================================================
// Coordinates
float EqView::frequencyToX (float hz) const
{
    const auto proportion = std::log (hz / EqController::minFrequency) / std::log (EqController::maxFrequency / EqController::minFrequency);
    return (float) graphArea.getX() + proportion * (float) graphArea.getWidth();
}

float EqView::xToFrequency (float x) const
{
    const auto proportion = juce::jlimit (0.0f, 1.0f, (x - (float) graphArea.getX()) / (float) juce::jmax (1, graphArea.getWidth()));
    return EqController::minFrequency * std::pow (EqController::maxFrequency / EqController::minFrequency, proportion);
}

float EqView::gainToY (float db) const
{
    return juce::jmap (db, displayRangeDb, -displayRangeDb, (float) graphArea.getY(), (float) graphArea.getBottom());
}

float EqView::yToGain (float y) const
{
    return juce::jmap (y, (float) graphArea.getY(), (float) graphArea.getBottom(), displayRangeDb, -displayRangeDb);
}

juce::Point<float> EqView::bandPoint (int band) const
{
    const auto& b = settings.bands[(size_t) band];
    return { frequencyToX (b.frequencyHz), gainToY (b.gainDb) };
}

int EqView::bandAt (juce::Point<float> p) const
{
    // The nearest point within reach (a little more than its size).
    int best = -1;
    float bestDistance = pointRadius * 2.0f;

    for (int b = 0; b < EqSettings::numBands; ++b)
    {
        const auto d = bandPoint (b).getDistanceFrom (p);

        if (d < bestDistance)
        {
            best = b;
            bestDistance = d;
        }
    }

    return best;
}

void EqView::setBand (int band, float frequencyHz, float gainDb, float q)
{
    engine.getTracks().eq().setBand (target, band, frequencyHz, gainDb, q);
    refresh();   // straight away, so the point keeps up with the mouse
}

juce::String EqView::describeFrequency (float hz)
{
    return hz >= 1000.0f ? juce::String (hz / 1000.0f, hz >= 10000.0f ? 0 : 1) + " kHz"
                         : juce::String (juce::roundToInt (hz)) + " Hz";
}

//==============================================================================
// Mouse
void EqView::mouseMove (const juce::MouseEvent& e)
{
    const auto band = target != 0 ? bandAt (e.position) : -1;

    if (band != hoverBand)
    {
        hoverBand = band;
        setMouseCursor (band >= 0 ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor);
        repaint();
    }
}

void EqView::mouseExit (const juce::MouseEvent&)
{
    if (hoverBand >= 0 && dragBand < 0)
    {
        hoverBand = -1;
        repaint();
    }
}

void EqView::mouseDown (const juce::MouseEvent& e)
{
    dragBand = target != 0 ? bandAt (e.position) : -1;

    if (dragBand >= 0)
        engine.beginUndoStep ("EQ");   // the whole drag undoes in one go
}

void EqView::mouseDrag (const juce::MouseEvent& e)
{
    if (dragBand < 0)
        return;

    const auto pos = graphArea.toFloat().getConstrainedPoint (e.position);
    setBand (dragBand, xToFrequency (pos.x),
             juce::jlimit (EqController::minGain, EqController::maxGain, yToGain (pos.y)),
             settings.bands[(size_t) dragBand].q);
}

void EqView::mouseUp (const juce::MouseEvent&)
{
    dragBand = -1;
}

void EqView::mouseDoubleClick (const juce::MouseEvent& e)
{
    // Back to 0 dB, keeping the frequency and width.
    if (const auto band = target != 0 ? bandAt (e.position) : -1; band >= 0)
    {
        const auto& b = settings.bands[(size_t) band];
        engine.beginUndoStep ("EQ");
        setBand (band, b.frequencyHz, 0.0f, b.q);
    }
}

void EqView::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    // Over a point: wider (wheel down) or narrower (wheel up).
    const auto band = target != 0 ? bandAt (e.position) : -1;

    if (band < 0 || wheel.deltaY == 0.0f)
        return;

    const auto& b = settings.bands[(size_t) band];
    engine.beginUndoStep ("EQ");
    setBand (band, b.frequencyHz, b.gainDb,
             juce::jlimit (EqController::minQ, EqController::maxQ, b.q * std::pow (2.0f, wheel.deltaY * 2.0f)));
}

//==============================================================================
void EqView::paint (juce::Graphics& g)
{
    // The graph's background and grid.
    g.setColour (colour::wellBg);
    g.fillRoundedRectangle (graphArea.toFloat(), size::radius);

    g.setFont (fonts::mono (textSize::tiny));

    for (float hz : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
    {
        const auto x = frequencyToX (hz);
        const bool labelled = hz == 100.0f || hz == 1000.0f || hz == 10000.0f;
        g.setColour (colour::tintLight.withAlpha (labelled ? 0.08f : 0.04f));
        g.fillRect (x, (float) graphArea.getY(), 1.0f, (float) graphArea.getHeight());

        if (labelled)
        {
            g.setColour (colour::textSecondary);
            g.drawText (describeFrequency (hz), juce::Rectangle<float> (x + 4.0f, (float) graphArea.getBottom() - 16.0f, 60.0f, 14.0f),
                        juce::Justification::centredLeft);
        }
    }

    for (float db : { -12.0f, 0.0f, 12.0f })
    {
        const auto y = gainToY (db);
        g.setColour (colour::tintLight.withAlpha (db == 0.0f ? 0.12f : 0.05f));
        g.fillRect ((float) graphArea.getX(), y, (float) graphArea.getWidth(), 1.0f);
        g.setColour (colour::textSecondary);
        g.drawText ((db > 0.0f ? "+" : "") + juce::String ((int) db) + " dB",
                    juce::Rectangle<float> ((float) graphArea.getX() + 6.0f, y - 14.0f, 60.0f, 12.0f),
                    juce::Justification::centredLeft);
    }

    if (target == 0)
    {
        g.setColour (colour::textSecondary);
        g.setFont (fonts::ui (textSize::lg));
        g.drawText ("Select a track to EQ it", graphArea, juce::Justification::centred);
        return;
    }

    // The curve, filled down (or up) to the 0 dB line.
    const auto curveColour = settings.enabled ? colour : colour::textSecondary;
    juce::Path line;

    for (size_t i = 0; i < curve.size(); ++i)
    {
        const auto x = (float) graphArea.getX() + (float) i;
        const auto y = gainToY (juce::jlimit (-displayRangeDb, displayRangeDb, curve[i]));

        if (i == 0)
            line.startNewSubPath (x, y);
        else
            line.lineTo (x, y);
    }

    if (! curve.empty())
    {
        auto fill = line;
        fill.lineTo ((float) graphArea.getRight(), gainToY (0.0f));
        fill.lineTo ((float) graphArea.getX(), gainToY (0.0f));
        fill.closeSubPath();
        g.setColour (curveColour.withAlpha (0.15f));
        g.fillPath (fill);
        g.setColour (curveColour);
        g.strokePath (line, juce::PathStrokeType (2.0f));
    }

    // The band points, numbered.
    for (int b = 0; b < EqSettings::numBands; ++b)
    {
        const auto p = bandPoint (b);
        const bool active = b == hoverBand || b == dragBand;
        const auto c = settings.enabled ? bandColours[b] : colour::textSecondary;
        auto circle = juce::Rectangle<float> (pointRadius * 2.0f, pointRadius * 2.0f).withCentre (p);

        g.setColour (c.withAlpha (active ? 1.0f : 0.85f));
        g.fillEllipse (circle);

        if (active)
        {
            g.setColour (colour::text);
            g.drawEllipse (circle.expanded (2.0f), 1.5f);
        }

        g.setColour (colour::textOnAccent);
        g.setFont (fonts::uiBold (textSize::tiny));
        g.drawText (juce::String (b + 1), circle, juce::Justification::centred);
    }

    // Each band's settings, underneath.
    const int columnWidth = readoutArea.getWidth() / EqSettings::numBands;

    for (int b = 0; b < EqSettings::numBands; ++b)
    {
        const auto& band = settings.bands[(size_t) b];
        auto r = readoutArea.withX (readoutArea.getX() + b * columnWidth).withWidth (columnWidth).reduced (4, 0);

        g.setColour (settings.enabled ? bandColours[b] : colour::textSecondary);
        g.fillEllipse (r.removeFromLeft (8).withSizeKeepingCentre (8, 8).toFloat());
        r.removeFromLeft (6);

        g.setColour (colour::text);
        g.setFont (fonts::uiBold (textSize::sm));
        g.drawText (band.name, r.removeFromTop (readoutHeight / 2), juce::Justification::bottomLeft, true);

        g.setColour (colour::textSecondary);
        g.setFont (fonts::ui (textSize::sm));
        const auto gain = (band.gainDb > 0.05f ? "+" : "") + juce::String (band.gainDb, 1) + " dB";
        g.drawText (describeFrequency (band.frequencyHz) + "  " + gain + "  Q " + juce::String (band.q, 1),
                    r, juce::Justification::topLeft, true);
    }
}

void EqView::resized()
{
    auto r = getLocalBounds().reduced (18, 14);

    auto top = r.removeFromTop (28);
    powerButton.setBounds (top.removeFromLeft (72));
    resetButton.setBounds (top.removeFromRight (72));
    r.removeFromTop (10);

    readoutArea = r.removeFromBottom (readoutHeight);
    r.removeFromBottom (6);
    graphArea = r;
    updateCurve();
}

} // namespace ikaros
