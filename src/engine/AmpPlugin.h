#pragma once

#include "engine/BuiltInEffects.h"
#include "engine/AmpStyles.h"
#include <array>

struct PluginLV2;   // a Guitarix DSP (see external/guitarix/ikaros/GuitarixAmp.h)

namespace ikaros
{

//==============================================================================
/**
    The Amp: a combo amp after a guitar or bass track's pedals, built from
    Guitarix's amp models, tone stacks and cabinet IRs (GPL, see CREDITS).
    It runs, in order:

      1. Bright     a gentle high-shelf boost before the preamp (on/off)
      2. Preamp     the voice's Guitarix tube model (preamp and power amp)
      3. Tone       the voice's Guitarix tone stack (Bass, Mid, Treble)
      4. Presence   a high shelf after the tone stack
      5. Master     the output level
      6. Cabinet    the cab's impulse response (juce::dsp::Convolution), or Off

    Settings (all automatable, saved with the session):
      voice     which voice (AmpStyles.h's getAmpVoices() index); picks the
                tube model and the tone stack
      gain, bass, mid, treble, presence, master    0..1
      bright    0 = off, 1 = on
      cab       getAmpCabs() index, or getAmpCabs().size() for Off

    The DSP is mono: it plays the input's first channel, or the two
    channels' average for stereo input (Tracktion copies a mono source to
    both, so that's the source unchanged), and puts the result on both
    channels.

    Picking a voice in the UI (setVoice) also sets the cab, Bright and the
    knobs to the voice's starting settings; changing the voice setting alone
    (automation, the Plug-ins inspector) only swaps the model and tone stack.

    Threading:
      - The constructor makes every voice's DSP and a convolver per cab
        (message thread).
      - initialise() sets them all up for the sample rate, and loads each
        cab's IR into its convolver there and then: Convolution::prepare()
        installs a loaded IR straight away (resampled from the IR's own rate
        to the session's by JUCE). That's on the message thread, before
        playback.
      - applyToBuffer() runs on the audio thread. Switching voice or cab
        (even by automation) just picks other, already-prepared DSPs and
        crossfades to them over one block, so nothing is loaded, allocated
        or locked there.
*/
class AmpPlugin  : public BuiltInEffectPlugin
{
public:
    explicit AmpPlugin (tracktion::PluginCreationInfo);
    ~AmpPlugin() override;

    static const char* xmlTypeName;      // "ikarosAmp"
    static const char* getPluginName()   { return "Amp"; }

    juce::String getName() const override                   { return getPluginName(); }
    juce::String getShortName (int) override                { return getPluginName(); }
    juce::String getPluginType() override                   { return xmlTypeName; }
    double getLatencySeconds() override;
    bool noTail() override                                  { return false; }
    double getTailLength() const override                   { return 0.05; }   // the longest cab IR is ~21 ms

    void initialise (const tracktion::PluginInitialisationInfo&) override;
    void reset() override;
    void applyToBuffer (const tracktion::PluginRenderContext&) override;

    //==============================================================================
    // The settings, for EffectsController (message thread). The setters go
    // into the current undo step.
    int getVoice() const;
    int getCab() const;
    bool isBright() const;
    tracktion::AutomatableParameter& getKnob (int index) const    { return *knobs[(size_t) index]; }

    /** Picks a voice and its starting settings: cab, Bright and knobs. */
    void setVoice (int voice);
    void setCab (int cab);
    void setBright (bool);

    /** The cab setting that means no cabinet. */
    static int getOffCab()          { return (int) getAmpCabs().size(); }

    /** A new Amp's state with a voice's starting settings, so it's created
        with them (no separate undo steps). */
    static juce::ValueTree createState (int voice);

    /** The Guitarix DSPs a voice plays, e.g. "gxamp7 + tonestack_bassman"
        (for tests and diagnostics; never shown to users). */
    static juce::String getVoiceDspNames (int voice);

    /** A cab's impulse response as the Amp loads it (level-matched), at its
        own sample rate (set in `sampleRate`). Empty for Off. */
    static juce::AudioBuffer<float> getCabinetIR (int cab, double& sampleRate);

private:
    struct VoiceDsp;
    struct Biquad;
    struct DspDeleter  { void operator() (PluginLV2*) const; };
    using DspPtr = std::unique_ptr<PluginLV2, DspDeleter>;

    tracktion::AutomatableParameter::Ptr voiceParam, brightParam, cabParam;
    std::array<tracktion::AutomatableParameter::Ptr, ampKnobCount> knobs;

    // The DSPs: a tube model and a tone stack per voice, and a convolver
    // per cab. They all read their controls from `controls` (a member, so
    // its address never changes).
    struct Controls
    {
        float gain1 = 0.0f, pregain = 0.0f, wetDry = 100.0f, drive = 0.25f;
        float middle = 0.5f, bass = 0.5f, treble = 0.5f, highGain = 0.0f;
    } controls;

    std::vector<VoiceDsp> voices;
    // One background thread for all of this Amp's convolvers (each would
    // otherwise start one of its own). We only ever load IRs right before
    // prepare(), which loads them on the calling thread.
    juce::dsp::ConvolutionMessageQueue convolutionQueue;
    std::vector<std::unique_ptr<juce::dsp::Convolution>> cabinets;

    std::unique_ptr<Biquad> brightFilter, presenceFilter;

    // Audio-thread state.
    juce::AudioBuffer<float> mono, fade;     // one block each, allocated in initialise()
    int activeVoice = -1, activeCab = -1;    // what's playing (-1: nothing yet)
    float presenceNow = -1.0f;               // what presenceFilter is set to
    juce::SmoothedValue<float> brightMix { 0.0f }, outputGain { 1.0f };
    int maxBlockSize = 512;
    double rate = 44100.0;

    void setSetting (tracktion::AutomatableParameter&, float value);
    float outputGainFor (int voice) const;
    void runVoice (VoiceDsp&, float* samples, int n);
    void runCab (int cab, float* samples, int n);
    void processMono (float* samples, int n);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AmpPlugin)
};

} // namespace ikaros
