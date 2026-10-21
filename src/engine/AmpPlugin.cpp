#include "engine/AmpPlugin.h"
#include <GuitarixAmp.h>

namespace te = tracktion;

namespace ikaros
{

namespace
{
    namespace amp = gx::amp;

    //==============================================================================
    // What each voice sounds like, in getAmpVoices()' order.
    //
    // The tube model for each was picked by measuring all eighteen of
    // Guitarix's on a DI guitar and test tones (how much they distort and
    // compress, and at what settings), with the voice's character in mind:
    //   Clean      gxamp16 (pre 12AT7 / push-pull 6V6): the most headroom of
    //              the push-pull models, about 2.6% distortion at Gain 0.30
    //   Crunch     gxamp7 (pre 12ax7 / push-pull 6V6): a tweed's tubes; breaks
    //              up from about Gain 0.5 (19% at 0.62)
    //   Chime      gxamp (12ax7): clean at low Gain, on the edge of breakup at
    //              0.50 (8%), so it jangles
    //   British    gxamp5 (pre 12ax7 / master 6V6): the hottest, most
    //              compressed model, with less pre-gain so it's still nearly
    //              clean at Gain 0 (2%) and a rock crunch at 0.70 (31%)
    //   High Gain  gxamp9 (12ax7 feedback): saturates fully and evenly, even
    //              when picked softly
    //   Bass       gxamp8 (pre 12AU7 / push-pull 6V6): round and mostly clean
    //              on a low E (4.7% at 0.40), hairier when turned up
    //
    // Gain turns up the model's Drive and the level into it (pre-gain)
    // together. The tubes get much louder as Gain goes up (20-30 dB over
    // its travel), so the output is turned down to match: `trimDb` sets the
    // level at the voice's default Gain (calibrated so every voice, at its
    // starting settings, plays the DI test guitar at about -20 dBFS RMS,
    // close to the guitar's own level), and below and above that Gain the
    // level changes by `levelBelow`/`levelAbove` dB per unit of Gain. Each
    // voice still gets about 8-14 dB louder from Gain 0 to 1, like an amp.
    struct VoiceSound
    {
        const char* dspNames;              // for getVoiceDspNames
        amp::Model model;
        amp::ToneStack toneStack;
        float driveLow, driveHigh;         // the model's Drive (0.01..1) at Gain 0 and 1
        float preGainLow, preGainHigh;     // dB into the first tube at Gain 0 and 1
        float trimDb;                      // the output level at the voice's default Gain
        float levelBelow, levelAbove;      // dB per unit of Gain made up below/above it
    };

    const std::array<VoiceSound, 6> voiceSounds {{
        { "gxamp16 + tonestack_fender_default",  amp::Model::gxamp16,  amp::ToneStack::fenderDefault, 0.05f, 0.75f,  -6.0f,  6.0f,   3.1f, 10.7f, 18.0f },   // Clean
        { "gxamp7 + tonestack_bassman",          amp::Model::gxamp7,   amp::ToneStack::bassman,       0.10f, 0.90f,  -6.0f,  8.0f, -14.7f, 19.0f,  0.0f },   // Crunch
        { "gxamp + tonestack_ac30",              amp::Model::gxamp,    amp::ToneStack::ac30,          0.10f, 0.90f,  -6.0f,  8.0f,  -7.0f, 24.0f,  1.0f },   // Chime
        { "gxamp5 + tonestack_jcm800",           amp::Model::gxamp5,   amp::ToneStack::jcm800,        0.10f, 0.90f, -12.0f,  4.0f, -21.4f, 17.0f,  0.0f },   // British
        { "gxamp9 + tonestack_engl",             amp::Model::gxamp9,   amp::ToneStack::engl,          0.30f, 1.00f,   0.0f, 14.0f,  -9.9f,  6.0f,  0.0f },   // High Gain
        { "gxamp8 + tonestack_ampeg",            amp::Model::gxamp8,   amp::ToneStack::ampeg,         0.05f, 0.75f,  -6.0f,  6.0f,  -8.4f, 19.0f, 11.0f },   // Bass
    }};

    // Guitarix's "Distortion": how much of the signal takes the tubes'
    // clipping path. All of it, for every voice (Gain does the rest).
    constexpr float wetDry = 100.0f;

    //==============================================================================
    // Which Guitarix IR each cab plays (getAmpCabs()' order), and a level
    // that makes the cabs equally loud. Guitarix's IRs differ by almost
    // 30 dB on the same amp (its 1x8 is very loud, its Princeton very
    // quiet); these were measured on a crunchy amp tone, so switching cab
    // changes the tone, not the volume.
    struct CabSound
    {
        int guitarixIndex;     // see gx::amp::getCabinet
        float levelDb;
    };

    const std::array<CabSound, 18> cabSounds {{
        { 17, -9.5f },    // 1x8 Small Combo       <- 1x8
        { 10, 19.7f },    // 1x10 Small Combo      <- Princeton
        {  2,  7.9f },    // 1x12 Open Back        <- 1x12
        { 11, -4.4f },    // 1x12 Studio           <- A2
        { 16,  1.2f },    // 1x12 Warm             <- charisma
        { 15, -2.6f },    // 1x12 Crisp            <- vitalize
        { 12, -3.8f },    // 1x15                  <- 1x15
        {  4,  6.3f },    // 2x10 Open Back        <- 2x10
        {  1,  1.0f },    // 2x12 Closed Back      <- 2x12
        {  6,  3.3f },    // 2x12 Open Back        <- Twin
        {  9,  0.8f },    // 2x12 Blue             <- AC30
        { 14,  5.1f },    // 2x12 Bright           <- briliant
        {  3,  3.8f },    // 4x10 Closed Back      <- 4x10
        {  7,  1.1f },    // 4x10 Open Back        <- Bassman
        {  0,  3.0f },    // 4x12 Closed Back      <- 4x12
        {  8,  1.2f },    // 4x12 Vintage          <- Marshall
        {  5,  4.0f },    // 4x12 Heavy            <- HighGain
        { 13,  2.8f },    // 4x12 Modern           <- mesa
    }};

    // Bright: +4 dB above about 2 kHz, before the preamp (like a bright cap
    // across the volume pot). Presence: a shelf around 3.5 kHz after the
    // tone stack, -6..+6 dB (flat at 0.5).
    constexpr double brightHz = 2000.0, brightDb = 4.0;
    constexpr double presenceHz = 3500.0, presenceRangeDb = 12.0;

    // Master: 0 dB at 0.55, 40 dB over its travel, fading to silence at the
    // bottom.
    float masterGain (float m)
    {
        return juce::Decibels::decibelsToGain (40.0f * (m - 0.55f)) * juce::jmin (1.0f, m / 0.05f);
    }

    const auto& defaultVoice()    { return getAmpVoices().front(); }
}

//==============================================================================
// A biquad high shelf (the Audio EQ Cookbook's), working in doubles. Plain
// arithmetic, so its gain can be changed on the audio thread (JUCE's IIR
// coefficients are allocated objects, which can't).
struct AmpPlugin::Biquad
{
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
    double z1 = 0.0, z2 = 0.0;

    void setHighShelf (double sampleRate, double hz, double gainDb)
    {
        const double A = std::pow (10.0, gainDb / 40.0);
        const double w = juce::MathConstants<double>::twoPi * juce::jmin (hz, sampleRate * 0.45) / sampleRate;
        const double cosW = std::cos (w), alpha = std::sin (w) / (2.0 * 0.6);   // Q 0.6: a gentle slope
        const double sqrtTerm = 2.0 * std::sqrt (A) * alpha;
        const double a0 = (A + 1.0) - (A - 1.0) * cosW + sqrtTerm;

        b0 = A * ((A + 1.0) + (A - 1.0) * cosW + sqrtTerm) / a0;
        b1 = -2.0 * A * ((A - 1.0) + (A + 1.0) * cosW) / a0;
        b2 = A * ((A + 1.0) + (A - 1.0) * cosW - sqrtTerm) / a0;
        a1 = 2.0 * ((A - 1.0) - (A + 1.0) * cosW) / a0;
        a2 = ((A + 1.0) - (A - 1.0) * cosW - sqrtTerm) / a0;
    }

    void reset()    { z1 = z2 = 0.0; }

    float process (float x)
    {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return (float) y;
    }
};

// One voice's two Guitarix DSPs.
struct AmpPlugin::VoiceDsp
{
    DspPtr model, toneStack;
};

void AmpPlugin::DspDeleter::operator() (PluginLV2* d) const
{
    d->delete_instance (d);   // (none of these has buffers to free first)
}

//==============================================================================
const char* AmpPlugin::xmlTypeName = ampPluginType;   // never change it, or sessions lose their Amps

AmpPlugin::AmpPlugin (te::PluginCreationInfo info)
    : BuiltInEffectPlugin (info),
      brightFilter (std::make_unique<Biquad>()),
      presenceFilter (std::make_unique<Biquad>())
{
    jassert (getAmpVoices().size() == voiceSounds.size() && getAmpCabs().size() == cabSounds.size());

    // A new Amp is the first voice (Clean) at its starting settings.
    auto& start = defaultVoice();

    juce::StringArray voiceNames, cabNames;

    for (auto& v : getAmpVoices())
        voiceNames.add (v.name);

    for (int i = 0; i <= getOffCab(); ++i)
        cabNames.add (getAmpCabName (i));

    // Choices: whole numbers only (an interval of 1), shown by name.
    auto choice = [this] (const juce::Identifier& id, const juce::String& name, juce::StringArray names, int defaultIndex)
    {
        return addSetting (id, name, { 0.0f, (float) (names.size() - 1), 1.0f }, (float) defaultIndex,
                           [names] (float v) { return names[juce::jlimit (0, names.size() - 1, juce::roundToInt (v))]; });
    };

    voiceParam = choice ("voice", "Voice", voiceNames, 0);

    for (int i = 0; i < ampKnobCount; ++i)
        knobs[(size_t) i] = addSetting (ampKnobIds[(size_t) i], ampKnobLabels[(size_t) i], { 0.0f, 1.0f },
                                        start.knobDefaults[(size_t) i], percent);

    brightParam = choice ("bright", "Bright", { "Off", "On" }, start.bright ? 1 : 0);
    cabParam = choice ("cab", "Cab", cabNames, start.cab);

    // Every voice's DSPs, all reading their controls from `controls`.
    float* controlAddress[] = { &controls.gain1, &controls.pregain, &controls.wetDry, &controls.drive,
                                &controls.middle, &controls.bass, &controls.treble };
    const std::uint32_t ports[] = { amp::gain1, amp::pregain, amp::wetDry, amp::drive, amp::middle, amp::bass, amp::treble };

    for (auto& sound : voiceSounds)
    {
        VoiceDsp v;
        v.model.reset (amp::createModel (sound.model));
        v.toneStack.reset (amp::createToneStack (sound.toneStack));

        for (auto* d : { v.model.get(), v.toneStack.get() })
        {
            // Until initialise() gives the real rate, so a DSP is never left
            // unset (its members aren't initialised until this).
            d->set_samplerate (44100, d);

            for (size_t p = 0; p < std::size (ports); ++p)
                d->connect_ports (ports[p], controlAddress[p], d);

            d->connect_ports (amp::highGain, &controls.highGain, d);
        }

        voices.push_back (std::move (v));
    }

    // A convolver per cab, sharing our message queue. Latency {0}: JUCE's
    // zero-latency convolution (the IRs are short, so it's cheap).
    for (size_t i = 0; i < cabSounds.size(); ++i)
        cabinets.push_back (std::make_unique<juce::dsp::Convolution> (juce::dsp::Convolution::Latency { 0 }, convolutionQueue));
}

AmpPlugin::~AmpPlugin()
{
    // Tracktion needs this in the most-derived class's destructor.
    notifyListenersOfDeletion();
}

juce::ValueTree AmpPlugin::createState (int voice)
{
    auto& voices = getAmpVoices();
    voice = juce::jlimit (0, (int) voices.size() - 1, voice);
    auto& v = voices[(size_t) voice];

    // The properties are the settings' IDs (see BuiltInEffectPlugin::addSetting).
    auto state = te::createValueTree (te::IDs::PLUGIN, te::IDs::type, xmlTypeName);
    state.setProperty ("voice", (float) voice, nullptr);

    for (int i = 0; i < ampKnobCount; ++i)
        state.setProperty (ampKnobIds[(size_t) i], v.knobDefaults[(size_t) i], nullptr);

    state.setProperty ("bright", v.bright ? 1.0f : 0.0f, nullptr);
    state.setProperty ("cab", (float) v.cab, nullptr);
    return state;
}

juce::String AmpPlugin::getVoiceDspNames (int voice)
{
    return voiceSounds[(size_t) juce::jlimit (0, (int) voiceSounds.size() - 1, voice)].dspNames;
}

juce::AudioBuffer<float> AmpPlugin::getCabinetIR (int cab, double& sampleRate)
{
    if (! juce::isPositiveAndBelow (cab, (int) cabSounds.size()))
    {
        sampleRate = 0.0;
        return {};
    }

    const auto ir = amp::getCabinet (cabSounds[(size_t) cab].guitarixIndex);
    juce::AudioBuffer<float> samples (1, ir.length);
    samples.copyFrom (0, 0, ir.samples, ir.length, juce::Decibels::decibelsToGain (cabSounds[(size_t) cab].levelDb));
    sampleRate = (double) ir.sampleRate;
    return samples;
}

//==============================================================================
int AmpPlugin::getVoice() const
{
    return juce::jlimit (0, (int) voiceSounds.size() - 1, juce::roundToInt (voiceParam->getCurrentValue()));
}

int AmpPlugin::getCab() const
{
    return juce::jlimit (0, getOffCab(), juce::roundToInt (cabParam->getCurrentValue()));
}

bool AmpPlugin::isBright() const
{
    return brightParam->getCurrentValue() >= 0.5f;
}

void AmpPlugin::setSetting (te::AutomatableParameter& param, float value)
{
    // As EffectsController's setEffectKnob: setting it to its current value
    // first makes sure the state property exists, so the change below is
    // recorded in the undo history as a change of that property.
    param.setParameter (param.getCurrentValue(), juce::sendNotification);
    param.setParameter (value, juce::sendNotification);
}

void AmpPlugin::setVoice (int voice)
{
    auto& voicesList = getAmpVoices();
    voice = juce::jlimit (0, (int) voicesList.size() - 1, voice);
    auto& v = voicesList[(size_t) voice];

    setSetting (*voiceParam, (float) voice);

    for (int i = 0; i < ampKnobCount; ++i)
        setSetting (*knobs[(size_t) i], v.knobDefaults[(size_t) i]);

    setSetting (*brightParam, v.bright ? 1.0f : 0.0f);
    setSetting (*cabParam, (float) v.cab);
}

void AmpPlugin::setCab (int cab)
{
    setSetting (*cabParam, (float) juce::jlimit (0, getOffCab(), cab));
}

void AmpPlugin::setBright (bool on)
{
    setSetting (*brightParam, on ? 1.0f : 0.0f);
}

//==============================================================================
double AmpPlugin::getLatencySeconds()
{
    // Zero-latency convolution, and no oversampling: normally 0. Asked by
    // Tracktion after initialise(), so the convolvers are ready.
    int samples = 0;

    for (auto& c : cabinets)
        samples = juce::jmax (samples, c->getLatency());

    return samples / rate;
}

void AmpPlugin::initialise (const te::PluginInitialisationInfo& info)
{
    rate = info.sampleRate;
    maxBlockSize = juce::jmax (1, info.blockSizeSamples);

    // The DSPs set up their filters for the rate (and clear themselves).
    for (auto& v : voices)
        for (auto* d : { v.model.get(), v.toneStack.get() })
            d->set_samplerate ((std::uint32_t) juce::roundToInt (rate), d);

    // Each cab's IR, at the rate Guitarix stored it at (JUCE resamples it to
    // ours), with its level built in. prepare() runs the load right here and
    // installs it: no background loading, no fade-in.
    const juce::dsp::ProcessSpec spec { rate, (juce::uint32) maxBlockSize, 1 };

    for (size_t i = 0; i < cabinets.size(); ++i)
    {
        double irRate = 0.0;
        auto samples = getCabinetIR ((int) i, irRate);

        cabinets[i]->loadImpulseResponse (std::move (samples), irRate,
                                          juce::dsp::Convolution::Stereo::no, juce::dsp::Convolution::Trim::no,
                                          juce::dsp::Convolution::Normalise::no);
        cabinets[i]->prepare (spec);
    }

    // Allocating is fine here: this runs on the message thread, not while playing.
    mono.setSize (1, maxBlockSize);
    fade.setSize (1, maxBlockSize);

    // Knob changes glide over 30 ms instead of jumping (which would click).
    brightMix.reset (rate, 0.03);
    outputGain.reset (rate, 0.03);
    reset();
}

void AmpPlugin::reset()
{
    for (auto& v : voices)
        for (auto* d : { v.model.get(), v.toneStack.get() })
            d->clear_state (d);

    for (auto& c : cabinets)
        c->reset();

    brightFilter->reset();
    presenceFilter->reset();
    brightMix.setCurrentAndTargetValue (isBright() ? 1.0f : 0.0f);
    activeVoice = getVoice();
    activeCab = getCab();
    outputGain.setCurrentAndTargetValue (outputGainFor (activeVoice));
    presenceNow = -1.0f;
}

float AmpPlugin::outputGainFor (int voice) const
{
    auto& sound = voiceSounds[(size_t) voice];
    const auto defaultGain = getAmpVoices()[(size_t) voice].knobDefaults[0];
    const auto gain = knobs[0]->getCurrentValue();
    const auto makeUp = gain < defaultGain ? sound.levelBelow * (defaultGain - gain)
                                           : -sound.levelAbove * (gain - defaultGain);

    return juce::Decibels::decibelsToGain (sound.trimDb + makeUp) * masterGain (knobs[5]->getCurrentValue());
}

void AmpPlugin::runVoice (VoiceDsp& v, float* samples, int n)
{
    v.model->mono_audio (n, samples, samples, v.model.get());
    v.toneStack->mono_audio (n, samples, samples, v.toneStack.get());
}

void AmpPlugin::runCab (int cab, float* samples, int n)
{
    if (cab >= (int) cabinets.size())
        return;   // Off: no cabinet

    // An AudioBlock is a view onto samples it doesn't own; processing a
    // "replacing" context works in place.
    float* channels[] = { samples };
    juce::dsp::AudioBlock<float> block (channels, 1, (size_t) n);
    cabinets[(size_t) cab]->process (juce::dsp::ProcessContextReplacing<float> (block));
}

void AmpPlugin::processMono (float* samples, int n)
{
    // The controls for this block. The DSPs read them at the start of each call.
    const int voice = getVoice(), cab = getCab();
    auto& sound = voiceSounds[(size_t) voice];
    const auto gain = knobs[0]->getCurrentValue();
    controls.drive = juce::jmap (gain, sound.driveLow, sound.driveHigh);
    controls.pregain = juce::jmap (gain, sound.preGainLow, sound.preGainHigh);
    controls.wetDry = wetDry;
    controls.bass = knobs[1]->getCurrentValue();
    controls.middle = knobs[2]->getCurrentValue();
    controls.treble = knobs[3]->getCurrentValue();

    if (const auto presence = knobs[4]->getCurrentValue(); presence != presenceNow)
        presenceFilter->setHighShelf (rate, presenceHz, ((presenceNow = presence) - 0.5) * presenceRangeDb);

    // 1. Bright: always filtered, and mixed in (so switching it glides).
    brightMix.setTargetValue (isBright() ? 1.0f : 0.0f);

    for (int i = 0; i < n; ++i)
    {
        const auto shelved = brightFilter->process (samples[i]);
        samples[i] += (shelved - samples[i]) * brightMix.getNextValue();
    }

    // 2-3. The tube model and tone stack. A new voice starts from silence
    // (cleared) and fades in over this block while the old one fades out.
    float* faded = fade.getWritePointer (0);
    const auto rampDown = [n] (int i)    { return 1.0f - (float) (i + 1) / (float) n; };

    if (voice != activeVoice)
    {
        juce::FloatVectorOperations::copy (faded, samples, n);
        runVoice (voices[(size_t) activeVoice], faded, n);

        auto& next = voices[(size_t) voice];
        next.model->clear_state (next.model.get());
        next.toneStack->clear_state (next.toneStack.get());
        runVoice (next, samples, n);

        for (int i = 0; i < n; ++i)
            samples[i] += (faded[i] - samples[i]) * rampDown (i);

        activeVoice = voice;
    }
    else
    {
        runVoice (voices[(size_t) voice], samples, n);
    }

    // 4-5. Presence, then Master (with the voice's level make-up).
    outputGain.setTargetValue (outputGainFor (voice));

    for (int i = 0; i < n; ++i)
        samples[i] = presenceFilter->process (samples[i]) * outputGain.getNextValue();

    // 6. The cabinet, crossfading to a new one the same way.
    if (cab != activeCab)
    {
        juce::FloatVectorOperations::copy (faded, samples, n);
        runCab (activeCab, faded, n);

        if (cab < (int) cabinets.size())
            cabinets[(size_t) cab]->reset();

        runCab (cab, samples, n);

        for (int i = 0; i < n; ++i)
            samples[i] += (faded[i] - samples[i]) * rampDown (i);

        activeCab = cab;
    }
    else
    {
        runCab (cab, samples, n);
    }
}

void AmpPlugin::applyToBuffer (const te::PluginRenderContext& fc)
{
    if (fc.destBuffer == nullptr || fc.bufferNumSamples <= 0 || mono.getNumSamples() == 0)
        return;

    SCOPED_REALTIME_CHECK

    // Tiny "denormal" numbers in the filters' feedback (as a note dies
    // away) are very slow to work with; this treats them as zero while
    // we're in this function.
    juce::ScopedNoDenormals noDenormals;

    auto& buffer = *fc.destBuffer;
    const int channels = juce::jmin (2, buffer.getNumChannels());

    if (channels == 0)
        return;

    float* left = buffer.getWritePointer (0, fc.bufferStartSample);
    float* right = channels > 1 ? buffer.getWritePointer (1, fc.bufferStartSample) : nullptr;
    float* samples = mono.getWritePointer (0);

    // In pieces of at most maxBlockSize (what the convolvers and our
    // buffers were set up for).
    for (int done = 0; done < fc.bufferNumSamples; done += maxBlockSize)
    {
        const int n = juce::jmin (maxBlockSize, fc.bufferNumSamples - done);

        // Mono in: the first channel, or both averaged (a mono source
        // arrives on both, so that's the source unchanged).
        if (right != nullptr)
        {
            for (int i = 0; i < n; ++i)
                samples[i] = 0.5f * (left[done + i] + right[done + i]);
        }
        else
        {
            juce::FloatVectorOperations::copy (samples, left + done, n);
        }

        processMono (samples, n);

        // The same sound on both channels.
        juce::FloatVectorOperations::copy (left + done, samples, n);

        if (right != nullptr)
            juce::FloatVectorOperations::copy (right + done, samples, n);
    }
}

} // namespace ikaros
