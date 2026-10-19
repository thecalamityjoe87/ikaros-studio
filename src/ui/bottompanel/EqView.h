#pragma once

#include "engine/AudioEngine.h"
#include "ui/widgets/IconButton.h"
#include "ui/theme/Family.h"

namespace ikaros
{

//==============================================================================
/**
    The "EQ" tab: a 4-band EQ for the selected track or for the master.

        [⏻ On]                                              [Reset]
        ┌────────────────────────────────────────────────────────┐
        │ +12 ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ │  the curve: how
        │   0 ───①──────╱‾‾②‾╲────────────③──────────④────────│  loud each
        │ -12           100 Hz        1 kHz          10 kHz      │  frequency gets
        └────────────────────────────────────────────────────────┘
          Low 100 Hz +0 dB   Low Mid 400 Hz +6 dB   ...

    Drag a band's point: sideways for its frequency, up and down to boost or
    cut. The mouse wheel over a point makes the band wider or narrower, and a
    double-click puts it back to 0 dB. Every drag is one undo step.
*/
class EqView  : public juce::Component
{
public:
    explicit EqView (AudioEngine&);

    /** Which EQ to show: a track's, or the master's (masterTrackId). 0 = none. */
    void setTarget (TrackId, tokens::Family);

    /** Re-reads the settings from the engine (model changed). */
    void refresh();

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    AudioEngine& engine;
    TrackId target = 0;
    tokens::Family family = tokens::Family::master;   // the curve's colour, looked up when painting
    EqSettings settings;
    std::vector<float> curve;      // dB at each column of the graph

    IconButton powerButton { "EQ on/off", Icon::power, "On" };
    IconButton resetButton { "Reset EQ", std::nullopt, "Reset" };

    juce::Rectangle<int> graphArea, readoutArea;
    int dragBand = -1, hoverBand = -1;

    // The graph: frequency across (on a log scale, like your ears hear it),
    // dB up and down.
    float frequencyToX (float hz) const;
    float xToFrequency (float x) const;
    float gainToY (float db) const;
    float yToGain (float y) const;
    juce::Point<float> bandPoint (int band) const;
    int bandAt (juce::Point<float>) const;

    void setBand (int band, float frequencyHz, float gainDb, float q);
    void updateCurve();
    static juce::String describeFrequency (float hz);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EqView)
};

} // namespace ikaros
