#pragma once

#include "engine/EngineTypes.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace ikaros
{

//==============================================================================
/**
    A stereo level meter (two bars), horizontal or vertical.

    It doesn't read the engine itself: the owner calls setLevel() from its
    ~30 Hz timer. The meter smooths the fall so it doesn't flicker.
*/
class LevelMeter  : public juce::Component
{
public:
    enum class Orientation { horizontal, vertical };

    explicit LevelMeter (Orientation = Orientation::horizontal);

    void setLevel (StereoLevel);
    void reset();

    void paint (juce::Graphics&) override;

    // Shared with MeterSlider, so every meter looks and moves the same.

    /** A level in dB as a proportion of the bar (0..1). */
    static float dbToProportion (float db);

    /** One step of the bar's movement: straight up to a louder level, a
        gradual fall to a quieter one (so it doesn't flicker). */
    static float fallTowards (float current, float target);

    /** Draws one bar showing `value` (0..1), green to yellow to red along it. */
    /** One bar: `unlit` behind it, then the level in green → yellow → red. */
    static void paintBar (juce::Graphics&, juce::Rectangle<float>, float value, Orientation, juce::Colour unlit);

private:
    Orientation orientation;
    float left = 0.0f, right = 0.0f;    // displayed values, 0..1
};

} // namespace ikaros
