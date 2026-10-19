#include "engine/PedalStyles.h"
#include "engine/BuiltInEffects.h"

namespace te = tracktion;

namespace ikaros
{

const std::vector<PedalStyle>& getPedalStyles()
{
    // A function-local static is built the first time it's needed, so the
    // xmlTypeName strings (defined in other files) are set up by then.
    // Knob ids are ParameterInfo::id: a parameter's ID, or for the settings
    // Tracktion keeps as plain properties (Chorus, Phaser, the Echo's time),
    // the property's name (see storedSettings in EffectsController.cpp).
    static const std::vector<PedalStyle> styles {
        // Distortion
        { DistortionPlugin::xmlTypeName, "Valve Distortion", "Distortion", "Fuzz & Distortion",
          juce::Colour (0xff2b2e35), false,
          { { "drive", "Drive" }, { "tone", "Tone" }, { "output", "Level" } } },

        // Dynamics. Sustain is the compressor's threshold, reversed: more
        // sustain = a lower threshold = more compression.
        { te::CompressorPlugin::xmlTypeName, "Squeeze", "Compressor", "Filter & Dynamics",
          juce::Colour (0xff2f5f9e), false,
          { { "threshold", "Sustain", true }, { "attack", "Attack" }, { "output gain", "Level" } } },
        { NoiseGatePlugin::xmlTypeName, "Quiet Gate", "Noise gate", "Filter & Dynamics",
          juce::Colour (0xff4a5260), false,
          { { "threshold", "Thresh" }, { "release", "Release" } } },

        // Modulation
        { te::ChorusPlugin::xmlTypeName, "Shimmer Chorus", "Chorus", "Modulation",
          juce::Colour (0xff4fb3cc), true,
          { { "speedHz", "Rate" }, { "depthMs", "Depth" }, { "mixProportion", "Mix" } } },
        { te::PhaserPlugin::xmlTypeName, "Orbit Phaser", "Phaser", "Modulation",
          juce::Colour (0xffe07a3a), true,
          { { "rate", "Rate" }, { "depth", "Depth" } } },
        { TremoloPlugin::xmlTypeName, "Pulse Tremolo", "Tremolo", "Modulation",
          juce::Colour (0xff2f9e62), false,
          { { "rate", "Rate" }, { "depth", "Depth" } } },

        // Delay & reverb
        { te::DelayPlugin::xmlTypeName, "Tape Echo", "Delay", "Delay & Reverb",
          juce::Colour (0xffd9ccab), true,
          { { "length", "Time" }, { "feedback", "Repeats" }, { "mix proportion", "Mix" } } },
    };

    return styles;
}

const PedalStyle* findPedalStyle (const juce::String& pluginType)
{
    for (auto& style : getPedalStyles())
        if (style.pluginType == pluginType)
            return &style;

    return nullptr;
}

} // namespace ikaros
