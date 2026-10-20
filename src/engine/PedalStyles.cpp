#include "engine/PedalStyles.h"
#include "engine/BuiltInEffects.h"
#include "engine/PortedPedals.h"

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
        // Drive and boost, from the gentlest.
        { GoldenBoostPlugin::xmlTypeName, GoldenBoostPlugin::getPluginName(), "Overdrive", "Drive & Boost",
          juce::Colour (0xffc9a24e), true,
          { { "gain", "Gain" }, { "treble", "Treble" }, { "level", "Level" } } },
        { YellowDrivePlugin::xmlTypeName, YellowDrivePlugin::getPluginName(), "Overdrive", "Drive & Boost",
          juce::Colour (0xffd8b93a), true,
          { { "drive", "Drive" }, { "tone", "Tone" }, { "level", "Level" } } },
        { StackDrivePlugin::xmlTypeName, StackDrivePlugin::getPluginName(), "Amp-in-a-box", "Drive & Boost",
          juce::Colour (0xff24262b), false,
          { { "gain", "Gain" }, { "treble", "Tone" }, { "level", "Level" } } },
        { HotPlatePlugin::xmlTypeName, HotPlatePlugin::getPluginName(), "Tube drive", "Drive & Boost",
          juce::Colour (0xffa8432f), false,
          { { "gain", "Gain" }, { "treble", "Treble" }, { "volume", "Level" } } },
        { TubeDrivePlugin::xmlTypeName, TubeDrivePlugin::getPluginName(), "Tube drive", "Drive & Boost",
          juce::Colour (0xff5c6e4a), false,
          { { "gain", "Gain" }, { "treble", "Tone" }, { "volume", "Level" } } },
        { MellowDrivePlugin::xmlTypeName, MellowDrivePlugin::getPluginName(), "Low gain", "Drive & Boost",
          juce::Colour (0xff3f6f8f), false,
          { { "drive", "Drive" }, { "glass", "Glass" }, { "level", "Level" } } },

        // Fuzz and distortion.
        { SunFuzzPlugin::xmlTypeName, SunFuzzPlugin::getPluginName(), "Fuzz", "Fuzz & Distortion",
          juce::Colour (0xffc4562f), false,
          { { "drive", "Fuzz" }, { "volume", "Level" } } },
        { VintageFuzzPlugin::xmlTypeName, VintageFuzzPlugin::getPluginName(), "Fuzz", "Fuzz & Distortion",
          juce::Colour (0xff6d7438), false,
          { { "fuzz", "Fuzz" }, { "tone", "Tone" }, { "volume", "Level" } } },
        { SuperFuzzPlugin::xmlTypeName, SuperFuzzPlugin::getPluginName(), "Octave fuzz", "Fuzz & Distortion",
          juce::Colour (0xff5a3f8c), false,
          { { "expander", "Fuzz" }, { "balance", "Level" } } },
        { DistortionPlugin::xmlTypeName, "Valve Distortion", "Distortion", "Fuzz & Distortion",
          juce::Colour (0xff2b2e35), false,
          { { "drive", "Drive" }, { "tone", "Tone" }, { "output", "Level" } } },

        // Filter and dynamics. Squeeze's Sustain is the compressor's threshold,
        // reversed: more sustain = a lower threshold = more compression. Slow
        // Swell's Sens likewise: a lower threshold hears quieter notes.
        { EnvelopeWahPlugin::xmlTypeName, EnvelopeWahPlugin::getPluginName(), "Auto-wah", "Filter & Dynamics",
          juce::Colour (0xff3f78c9), false,
          { { "depth", "Sens" }, { "peak", "Peak" }, { "gain", "Level" } } },
        { SlowSwellPlugin::xmlTypeName, SlowSwellPlugin::getPluginName(), "Volume swell", "Filter & Dynamics",
          juce::Colour (0xff8b96a2), true,
          { { "threshold", "Sens", true }, { "uptime", "Swell" } } },
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
        { JetFlangerPlugin::xmlTypeName, JetFlangerPlugin::getPluginName(), "Flanger", "Modulation",
          juce::Colour (0xff8d6fd9), false,
          { { "freq", "Rate" }, { "depth", "Depth" }, { "feedback", "Regen" } } },
        { TremoloPlugin::xmlTypeName, "Pulse Tremolo", "Tremolo", "Modulation",
          juce::Colour (0xff2f9e62), false,
          { { "rate", "Rate" }, { "depth", "Depth" } } },

        // Delay & reverb
        { te::DelayPlugin::xmlTypeName, "Tape Echo", "Delay", "Delay & Reverb",
          juce::Colour (0xffd9ccab), true,
          { { "length", "Time" }, { "feedback", "Repeats" }, { "mix proportion", "Mix" } } },
        { SpringTankPlugin::xmlTypeName, SpringTankPlugin::getPluginName(), "Spring reverb", "Delay & Reverb",
          juce::Colour (0xff2c6b6f), false,
          { { "dry_wet", "Mix" }, { "roomsize", "Dwell" }, { "damp", "Tone", true } } },
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
