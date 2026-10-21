#pragma once

#include "engine/BuiltInEffects.h"
#include <array>

struct PluginLV2;   // a GxPlugins pedal's DSP (see external/gxplugins/ikaros/GxDsp.h)

namespace ikaros
{

class KlonCentaurDsp;

//==============================================================================
/**
    The base of the pedals whose sound is ported from Hermann Meyer's
    GxPlugins and Guitarix (GPL, see CREDITS). Each one is a
    BuiltInEffectPlugin whose settings mirror the original's controls, with
    the same ranges, defaults and curves, so it sounds the same at the same
    settings.

    A pedal is one or more DSPs ("stages") in a row. Most original DSPs are
    mono, so there's one copy of each per channel (two on a stereo track); a
    stereo one (Spring Tank's) gets both channels at once. A stage can run
    oversampled: the original pedals did that with zita-resampler, which
    needs threads MSVC doesn't have, so here juce::dsp::Oversampling does it
    and the latency it adds is reported to Tracktion.

    A subclass lists its stages and links each of its settings to the DSPs'
    control (an LV2 port number) with addControl().

    Threading: the DSPs and oversamplers are made in the constructor
    (message thread), and set up for the sample rate in initialise() (also
    the message thread). applyToBuffer() runs on the audio thread and only
    reads the settings and runs the DSPs, which never allocate or lock.
*/
class GxPedalPlugin  : public BuiltInEffectPlugin
{
public:
    ~GxPedalPlugin() override;

    juce::String getName() const override                   { return name; }
    juce::String getShortName (int) override                { return name; }
    double getLatencySeconds() override;

    void initialise (const tracktion::PluginInitialisationInfo&) override;
    void reset() override;
    void applyToBuffer (const tracktion::PluginRenderContext&) override;

protected:
    struct Stage
    {
        PluginLV2* (*create)();         // makes one copy of the DSP, e.g. gx::sd1::create
        int oversampling = 1;           // 1, 2 or 4: how many times the track's rate it runs at
        std::uint32_t rateFactor = 1;   // the DSP is told the rate times this (see the subclasses)
    };

    GxPedalPlugin (tracktion::PluginCreationInfo, juce::String name, std::vector<Stage>);

    /** Adds a setting (see BuiltInEffectPlugin::addSetting) whose value goes
        to the DSPs' control on LV2 port `port`. */
    tracktion::AutomatableParameter::Ptr addControl (std::uint32_t port, const juce::Identifier& id, const juce::String& name,
                                                     juce::NormalisableRange<float> range, float defaultValue,
                                                     std::function<juce::String (float)> toText);

    /** A switch-like control: whole numbers 0..names.size()-1, shown by name. */
    tracktion::AutomatableParameter::Ptr addChoice (std::uint32_t port, const juce::Identifier& id, const juce::String& name,
                                                    juce::StringArray names, int defaultIndex);

    /** A logarithmic range (equal knob travel per octave), like Faust's [log]. */
    static juce::NormalisableRange<float> logRange (float start, float end);

private:
    // Frees a DSP copy through its own delete function. With this as its
    // deleter, a std::unique_ptr frees the copy when it's destroyed.
    struct DspDeleter  { void operator() (PluginLV2*) const; };
    using DspPtr = std::unique_ptr<PluginLV2, DspDeleter>;

    struct StageState
    {
        Stage stage;
        std::array<DspPtr, 2> dsp;      // one per channel; only [0] for a stereo DSP
        bool stereo = false;
        std::unique_ptr<juce::dsp::Oversampling<float>> oversampler;   // if oversampling > 1
    };

    struct Control
    {
        tracktion::AutomatableParameter::Ptr parameter;
        // Where the DSPs read this control from. A unique_ptr, so the
        // address they were given stays put when `controls` grows.
        std::unique_ptr<float> value;
    };

    void forEachDsp (const std::function<void (PluginLV2&)>&);
    void process (StageState&, juce::dsp::AudioBlock<float>&);

    const juce::String name;
    std::vector<StageState> stages;
    std::vector<Control> controls;
    int maxBlockSize = 512;
    double rate = 44100.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GxPedalPlugin)
};

//==============================================================================
// The ported pedals. They differ only in their constructors (their stages
// and settings, in PortedPedals.cpp), so this macro writes the rest of each
// class. IKAROS_DECLARE_PORTED_PEDAL (YellowDrivePlugin, "Yellow Drive")
// declares a GxPedalPlugin subclass called YellowDrivePlugin, with a
// constructor, a destructor, getPluginName() returning "Yellow Drive", and
// xmlTypeName, its type name in sessions (set in PortedPedals.cpp; never
// change one, or older sessions lose that pedal).
#define IKAROS_DECLARE_PORTED_PEDAL(ClassName, displayName) \
    class ClassName  : public GxPedalPlugin \
    { \
    public: \
        explicit ClassName (tracktion::PluginCreationInfo); \
        ~ClassName() override; \
        static const char* xmlTypeName; \
        static const char* getPluginName()          { return displayName; } \
        juce::String getPluginType() override       { return xmlTypeName; } \
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ClassName) \
    };

/** Yellow Drive: a warm, mid-heavy overdrive. Inspired by the Boss SD-1
    Super OverDrive; GxSD1's DSP. */
IKAROS_DECLARE_PORTED_PEDAL (YellowDrivePlugin, "Yellow Drive")

/** Stack Drive: an amp stack's crunch in a box. Inspired by the Marshall
    Guv'nor; GxGuvnor's DSP. */
IKAROS_DECLARE_PORTED_PEDAL (StackDrivePlugin, "Stack Drive")

/** Hot Plate: a hot tube preamp. Inspired by the Matchless Hotbox;
    GxHotBox's DSP. */
IKAROS_DECLARE_PORTED_PEDAL (HotPlatePlugin, "Hot Plate")

/** Tube Drive: a valve overdrive. Inspired by the Electro-Harmonix Bottle
    Rocket; GxBottleRocket's DSP. */
IKAROS_DECLARE_PORTED_PEDAL (TubeDrivePlugin, "Tube Drive")

/** Mellow Drive: a low-gain, open overdrive. Inspired by the Lovepedal
    Eternity; GxEternity's DSP, oversampled 2x. */
IKAROS_DECLARE_PORTED_PEDAL (MellowDrivePlugin, "Mellow Drive")

/** Sun Fuzz: a germanium fuzz. Inspired by the Analogman Sun Face (a Fuzz
    Face); GxSunFace's DSP. */
IKAROS_DECLARE_PORTED_PEDAL (SunFuzzPlugin, "Sun Fuzz")

/** Vintage Fuzz: a three-transistor fuzz. Inspired by the Sola Sound Tone
    Bender; GxSuppaToneBender's DSP, its clipping oversampled 4x. */
IKAROS_DECLARE_PORTED_PEDAL (VintageFuzzPlugin, "Vintage Fuzz")

/** Super Fuzz: an octave fuzz. Inspired by the Univox Super-Fuzz;
    GxSuperFuzz's DSP, its clipping oversampled 4x. */
IKAROS_DECLARE_PORTED_PEDAL (SuperFuzzPlugin, "Super Fuzz")

/** Envelope Wah: a filter that opens as you play harder. Inspired by the
    Musitronics Mu-Tron III; GxQuack's DSP. */
IKAROS_DECLARE_PORTED_PEDAL (EnvelopeWahPlugin, "Envelope Wah")

/** Slow Swell: fades each note in, like turning a volume knob up. Inspired
    by the Boss SG-1 Slow Gear; GxSlowGear's DSP. */
IKAROS_DECLARE_PORTED_PEDAL (SlowSwellPlugin, "Slow Swell")

/** Jet Flanger: a flanger. Guitarix's gx_flanger DSP. */
IKAROS_DECLARE_PORTED_PEDAL (JetFlangerPlugin, "Jet Flanger")

/** Spring Tank: a reverb. Guitarix's stereo gx_reverb DSP (a Freeverb
    variant: neither Guitarix nor Airwindows has a spring model). */
IKAROS_DECLARE_PORTED_PEDAL (SpringTankPlugin, "Spring Tank")

//==============================================================================
/**
    Golden Boost: a transparent overdrive. Inspired by the Klon Centaur; the
    DSP is Jatin Chowdhury's KlonCentaur circuit model (BSD 3-clause, see
    CREDITS), its "traditional" (not neural) gain stage.

      Gain    0..1, default 0.5
      Treble  0..1, default 0.5
      Level   0..1, default 0.5
*/
class GoldenBoostPlugin  : public BuiltInEffectPlugin
{
public:
    explicit GoldenBoostPlugin (tracktion::PluginCreationInfo);
    ~GoldenBoostPlugin() override;

    static const char* xmlTypeName;      // "ikarosGoldenBoost"
    static const char* getPluginName()   { return "Golden Boost"; }

    juce::String getName() const override                   { return getPluginName(); }
    juce::String getShortName (int) override                { return getPluginName(); }
    juce::String getPluginType() override                   { return xmlTypeName; }
    double getLatencySeconds() override;

    void initialise (const tracktion::PluginInitialisationInfo&) override;
    void reset() override;
    void applyToBuffer (const tracktion::PluginRenderContext&) override;

private:
    tracktion::AutomatableParameter::Ptr gain, treble, level;
    std::unique_ptr<KlonCentaurDsp> dsp;
    double rate = 44100.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GoldenBoostPlugin)
};

} // namespace ikaros
