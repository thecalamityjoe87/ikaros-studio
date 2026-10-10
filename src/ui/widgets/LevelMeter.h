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

private:
    Orientation orientation;
    float left = 0.0f, right = 0.0f;    // displayed values, 0..1

    static float dbToProportion (float db);
    void paintBar (juce::Graphics&, juce::Rectangle<float>, float value);
};

} // namespace ikaros
