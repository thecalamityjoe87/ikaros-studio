#pragma once

#include "engine/SilenceSkip.h"
#include <tracktion_engine/tracktion_engine.h>
#include <memory>
#include <vector>

namespace ikaros
{

//==============================================================================
/**
    The base of our own built-in effects (Limiter, Noise Gate, Distortion,
    Tremolo). They sit in the "+ Add effect" menu beside Tracktion's
    Compressor, Echo and Reverb, and the effect settings view shows a knob
    for each setting.

    A setting is an AutomatableParameter backed by a property on the
    plugin's state (through a CachedValue), the same way Tracktion's own
    effects do it. So settings are saved with the session, and undo
    restores them.

    Threading: the constructor, the settings and restorePluginState run on
    the message thread. initialise() also runs on the message thread, before
    playback starts, so that's where buffers get allocated. applyToBuffer()
    runs on the audio thread: it must never allocate, lock or wait. It reads
    the settings with AutomatableParameter::getCurrentValue(), which is safe
    to call from there.
*/
class BuiltInEffectPlugin  : public tracktion::Plugin
{
public:
    ~BuiltInEffectPlugin() override;

    //==============================================================================
    // tracktion::Plugin: the parts every one of our effects shares.
    juce::String getSelectableDescription() override        { return getName(); }
    int getNumOutputChannelsGivenInputs (int) override      { return 2; }
    BusLayout getBusses() const override                    { return BusLayout::singleStereoInOut(); }
    void deinitialise() override                            {}
    void restorePluginStateFromValueTree (const juce::ValueTree&) override;

    // Text for the knobs' value labels.
    static juce::String decibels (float db);
    static juce::String milliseconds (float ms);
    static juce::String hertz (float hz);
    static juce::String percent (float zeroToOne);

protected:
    explicit BuiltInEffectPlugin (tracktion::PluginCreationInfo);

    /** Adds a setting. Call it from the subclass's constructor, in the order
        the knobs should appear. The id is also the state property's name;
        never rename one, or older sessions lose that setting. */
    tracktion::AutomatableParameter::Ptr addSetting (const juce::Identifier& id, const juce::String& name,
                                                     juce::NormalisableRange<float> range, float defaultValue,
                                                     std::function<juce::String (float)> toText);

    /** Each effect prepares this in initialise() with its tail length, and
        skips its DSP while it says so (see SilenceSkip). */
    SilenceSkip silence;

private:
    struct Setting
    {
        // juce::CachedValue can't be copied or moved, so each one lives on
        // the heap and the vector only moves the pointer around.
        std::unique_ptr<juce::CachedValue<float>> value;
        tracktion::AutomatableParameter::Ptr parameter;
    };

    std::vector<Setting> settings;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BuiltInEffectPlugin)
};

//==============================================================================
/**
    Keeps the level under a ceiling, so a mix never clips. New sessions have
    one on the master channel, at -1 dB.

      Gain     0..+12 dB into the limiter (makes things louder, up to the ceiling)
      Ceiling  -12..0 dB, the loudest the output gets
      Release  how quickly the level comes back after a peak

    It looks 2 ms ahead (and reports that as latency, which Tracktion makes
    up for), so it can turn down smoothly before a peak arrives instead of
    clipping it.

    Why not juce::dsp::Limiter? That one is a "maximiser": it always
    compresses everything above -10 dBFS at 4:1, which would squash a
    finished mix even when nothing is near the ceiling. A master limiter
    that's on by default must leave the sound alone until a peak comes.
*/
class LimiterPlugin  : public BuiltInEffectPlugin
{
public:
    explicit LimiterPlugin (tracktion::PluginCreationInfo);
    ~LimiterPlugin() override;

    static const char* xmlTypeName;      // "ikarosLimiter"
    static const char* getPluginName()   { return "Limiter"; }
    static constexpr float defaultCeilingDb = -1.0f;

    juce::String getName() const override                   { return getPluginName(); }
    juce::String getPluginType() override                   { return xmlTypeName; }
    juce::String getShortName (int) override                { return "Limiter"; }
    // Tracktion asks for the latency after initialise(), so it's exact for
    // the sample rate in use.
    double getLatencySeconds() override                     { return (window - 1) / rate; }
    bool noTail() override                                  { return false; }
    double getTailLength() const override                   { return lookaheadSeconds; }

    void initialise (const tracktion::PluginInitialisationInfo&) override;
    void reset() override;
    void applyToBuffer (const tracktion::PluginRenderContext&) override;

private:
    static constexpr double lookaheadSeconds = 0.002;

    tracktion::AutomatableParameter::Ptr gain, ceiling, release;

    // Audio-thread state. Four ring buffers of `window` samples, all written
    // at the same position: the delayed input (one per channel), the gain
    // each input sample needed, and the smoothed gain.
    int window = 1, position = 0;
    std::vector<float> delayed[2], neededGain, smoothedGain;
    double smoothedSum = 1.0;            // sum of smoothedGain, for its average
    float envelope = 1.0f;               // the gain after the release curve
    double rate = 44100.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LimiterPlugin)
};

//==============================================================================
/**
    Silences the track while it's quieter than a threshold, so a microphone's
    hiss and room noise don't come through between phrases. The track
    inspector's "Noise Gate" section is a shortcut to the first one in a
    track's chain.

      Threshold  below this the sound is turned down
      Release    how quickly it closes once the sound drops below

    The work is done by juce::dsp::NoiseGate.
*/
class NoiseGatePlugin  : public BuiltInEffectPlugin
{
public:
    explicit NoiseGatePlugin (tracktion::PluginCreationInfo);
    ~NoiseGatePlugin() override;

    static const char* xmlTypeName;      // "ikarosNoiseGate"
    static const char* getPluginName()   { return "Noise Gate"; }
    static constexpr float minThresholdDb = -80.0f, maxThresholdDb = -10.0f, defaultThresholdDb = -50.0f;

    tracktion::AutomatableParameter* getThresholdParameter() const    { return threshold.get(); }

    juce::String getName() const override                   { return getPluginName(); }
    juce::String getPluginType() override                   { return xmlTypeName; }
    juce::String getShortName (int) override                { return "Gate"; }

    void initialise (const tracktion::PluginInitialisationInfo&) override;
    void reset() override;
    void applyToBuffer (const tracktion::PluginRenderContext&) override;

private:
    tracktion::AutomatableParameter::Ptr threshold, release;

    juce::dsp::NoiseGate<float> gate;
    float gateThreshold = 1.0f, gateRelease = -1.0f;   // what the gate was last set to

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NoiseGatePlugin)
};

//==============================================================================
/**
    Overdrive through to heavy distortion.

      Drive   how hard the sound is pushed into the saturation
      Tone    darker to brighter (a low-pass after the saturation)
      Output  the level afterwards

    The level is evened out as Drive goes up, so turning it doesn't also
    make the track much louder. The saturation runs at twice the sample rate
    (juce::dsp::Oversampling) to keep it from sounding harsh and grainy.
*/
class DistortionPlugin  : public BuiltInEffectPlugin
{
public:
    explicit DistortionPlugin (tracktion::PluginCreationInfo);
    ~DistortionPlugin() override;

    static const char* xmlTypeName;      // "ikarosDistortion"
    static const char* getPluginName()   { return "Distortion"; }

    juce::String getName() const override                   { return getPluginName(); }
    juce::String getPluginType() override                   { return xmlTypeName; }
    juce::String getShortName (int) override                { return "Dist"; }
    double getLatencySeconds() override;

    void initialise (const tracktion::PluginInitialisationInfo&) override;
    void reset() override;
    void applyToBuffer (const tracktion::PluginRenderContext&) override;

private:
    tracktion::AutomatableParameter::Ptr drive, tone, output;

    // 2 channels, 2x (one stage), the cheaper IIR filters.
    juce::dsp::Oversampling<float> oversampling { 2, 1, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR };
    juce::dsp::StateVariableTPTFilter<float> toneFilter;
    juce::SmoothedValue<float> driveGain { 1.0f }, outputGain { 1.0f };
    int maxBlockSize = 512;
    double rate = 44100.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DistortionPlugin)
};

//==============================================================================
/**
    The volume pulses up and down, like an amp's tremolo.

      Rate   pulses per second
      Depth  how far down the volume dips
      Shape  smooth (a sine) to choppy (nearly on/off)
*/
class TremoloPlugin  : public BuiltInEffectPlugin
{
public:
    explicit TremoloPlugin (tracktion::PluginCreationInfo);
    ~TremoloPlugin() override;

    static const char* xmlTypeName;      // "ikarosTremolo"
    static const char* getPluginName()   { return "Tremolo"; }

    juce::String getName() const override                   { return getPluginName(); }
    juce::String getPluginType() override                   { return xmlTypeName; }
    juce::String getShortName (int) override                { return "Trem"; }

    void initialise (const tracktion::PluginInitialisationInfo&) override;
    void reset() override;
    void applyToBuffer (const tracktion::PluginRenderContext&) override;

private:
    tracktion::AutomatableParameter::Ptr rateParam, depth, shape;

    double phase = 0.0;                  // 0..1 through one pulse
    juce::SmoothedValue<float> smoothedDepth { 0.0f }, smoothedShape { 0.0f };
    double rate = 44100.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TremoloPlugin)
};

} // namespace ikaros
