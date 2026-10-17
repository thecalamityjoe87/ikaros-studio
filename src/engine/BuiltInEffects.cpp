#include "engine/BuiltInEffects.h"

namespace te = tracktion;

namespace ikaros
{

//==============================================================================
BuiltInEffectPlugin::BuiltInEffectPlugin (te::PluginCreationInfo info) : te::Plugin (info)
{
}

BuiltInEffectPlugin::~BuiltInEffectPlugin()
{
    // The parameters point at our CachedValues; unhook them before the
    // values are destroyed (members go after this body runs).
    for (auto& s : settings)
        s.parameter->detachFromCurrentValue();
}

te::AutomatableParameter::Ptr BuiltInEffectPlugin::addSetting (const juce::Identifier& id, const juce::String& name,
                                                               juce::NormalisableRange<float> range, float defaultValue,
                                                               std::function<juce::String (float)> toText)
{
    auto value = std::make_unique<juce::CachedValue<float>>();

    // referTo links the CachedValue to a property on our state, with the
    // value to use while the property isn't there (a newly added effect).
    // Changes made through it go into the session's undo history.
    value->referTo (state, id, getUndoManager(), defaultValue);

    // Typing a value isn't something our knobs offer, but Tracktion wants a
    // way back from text: the number at the start of it.
    te::AutomatableParameter::Ptr parameter = addParam (id.toString(), name, range, std::move (toText),
                                                        [] (const juce::String& s) { return s.getFloatValue(); });
    parameter->attachToCurrentValue (*value);

    settings.push_back ({ std::move (value), parameter });
    return parameter;
}

void BuiltInEffectPlugin::restorePluginStateFromValueTree (const juce::ValueTree& v)
{
    // Used when a preset or a copied effect is pasted over this one: take
    // whichever of our settings it has.
    for (auto& s : settings)
        if (v.hasProperty (s.value->getPropertyID()))
            *s.value = (float) v.getProperty (s.value->getPropertyID());

    for (auto& s : settings)
        s.parameter->updateFromAttachedValue();
}

juce::String BuiltInEffectPlugin::decibels (float db)
{
    // A real minus sign, like the rest of the app's dB readouts.
    const auto text = juce::String (std::abs (db), 1) + " dB";
    return db <= -0.05f ? juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x92")) + text : text;
}

juce::String BuiltInEffectPlugin::milliseconds (float ms)
{
    return ms < 1000.0f ? juce::String (juce::roundToInt (ms)) + " ms"
                        : juce::String (ms / 1000.0f, 2) + " s";
}

juce::String BuiltInEffectPlugin::hertz (float hz)
{
    return hz < 1000.0f ? juce::String (hz, hz < 10.0f ? 1 : 0) + " Hz"
                        : juce::String (hz / 1000.0f, 1) + " kHz";
}

juce::String BuiltInEffectPlugin::percent (float v)
{
    return juce::String (juce::roundToInt (v * 100.0f)) + "%";
}

//==============================================================================
// Limiter
const char* LimiterPlugin::xmlTypeName = "ikarosLimiter";

LimiterPlugin::LimiterPlugin (te::PluginCreationInfo info) : BuiltInEffectPlugin (info)
{
    gain    = addSetting ("gain", "Gain", { 0.0f, 12.0f }, 0.0f,
                          [] (float db) { return (db >= 0.05f ? "+" : "") + decibels (db); });
    ceiling = addSetting ("ceiling", "Ceiling", { -12.0f, 0.0f }, defaultCeilingDb, decibels);

    // The skew gives the short times (where small changes matter) more of
    // the knob's travel: the middle of the knob is the value given.
    juce::NormalisableRange<float> releaseRange { 10.0f, 1000.0f };
    releaseRange.setSkewForCentre (120.0f);
    release = addSetting ("release", "Release", releaseRange, 100.0f, milliseconds);
}

LimiterPlugin::~LimiterPlugin()
{
    // Tracktion needs this in the most-derived class's destructor.
    notifyListenersOfDeletion();
}

void LimiterPlugin::initialise (const te::PluginInitialisationInfo& info)
{
    rate = info.sampleRate;
    window = juce::jmax (1, juce::roundToInt (lookaheadSeconds * rate)) + 1;

    // Allocating is fine here: this runs on the message thread, not while playing.
    for (auto& d : delayed)
        d.assign ((size_t) window, 0.0f);

    neededGain.assign ((size_t) window, 1.0f);
    smoothedGain.assign ((size_t) window, 1.0f);
    reset();
}

void LimiterPlugin::reset()
{
    for (auto& d : delayed)
        std::fill (d.begin(), d.end(), 0.0f);

    std::fill (neededGain.begin(), neededGain.end(), 1.0f);
    std::fill (smoothedGain.begin(), smoothedGain.end(), 1.0f);
    smoothedSum = window;
    envelope = 1.0f;
    position = 0;
}

void LimiterPlugin::applyToBuffer (const te::PluginRenderContext& fc)
{
    if (fc.destBuffer == nullptr || neededGain.empty())
        return;

    SCOPED_REALTIME_CHECK

    auto& buffer = *fc.destBuffer;
    const int start = fc.bufferStartSample, num = fc.bufferNumSamples;
    const int channels = juce::jmin (2, buffer.getNumChannels());

    if (channels == 0)
        return;

    const float inputGain = juce::Decibels::decibelsToGain (gain->getCurrentValue());
    const float ceilingGain = juce::Decibels::decibelsToGain (ceiling->getCurrentValue());

    // The release is a one-pole curve: each sample, the gain moves this
    // fraction of the way back towards where it wants to be.
    const auto releaseSamples = juce::jmax (1.0, release->getCurrentValue() * 0.001 * rate);
    const auto releaseStep = (float) (1.0 - std::exp (-1.0 / releaseSamples));

    float* data[2] = { buffer.getWritePointer (0, start), channels > 1 ? buffer.getWritePointer (1, start) : nullptr };

    for (int i = 0; i < num; ++i)
    {
        // 1. How far this sample has to come down to stay under the ceiling
        //    (both channels together, so the stereo image doesn't shift).
        float peak = 0.0f;

        for (int ch = 0; ch < channels; ++ch)
            peak = juce::jmax (peak, std::abs (data[ch][i]) * inputGain);

        neededGain[(size_t) position] = peak > ceilingGain ? ceilingGain / peak : 1.0f;

        // 2. The most any sample in the window needs. The window covers the
        //    samples still waiting in the delay, so the gain starts going
        //    down before a peak is heard.
        float lowest = 1.0f;

        for (auto g : neededGain)
            lowest = juce::jmin (lowest, g);

        // 3. Down straight away, back up along the release curve.
        envelope = lowest < envelope ? lowest : envelope + (lowest - envelope) * releaseStep;

        // 4. Average the gain over the window, so a sudden drop becomes a
        //    smooth fade (no click). Every value averaged is at or below
        //    what the oldest sample needs, so the average is too: the
        //    output never goes over the ceiling.
        smoothedSum += envelope - smoothedGain[(size_t) position];
        smoothedGain[(size_t) position] = envelope;
        const auto smooth = (float) (smoothedSum / window);

        // 5. Delay the sound by window - 1 samples (the oldest slot in the
        //    ring is the one after the one just written), and apply the gain.
        //    The final limit only catches rounding errors.
        const int oldest = (position + 1) % window;

        for (int ch = 0; ch < channels; ++ch)
        {
            auto& ring = delayed[ch];
            ring[(size_t) position] = data[ch][i] * inputGain;
            data[ch][i] = juce::jlimit (-ceilingGain, ceilingGain, ring[(size_t) oldest] * smooth);
        }

        if (++position == window)
        {
            position = 0;

            // Adding and subtracting slowly gathers rounding errors; start
            // the sum afresh once per trip round the ring.
            smoothedSum = 0.0;

            for (auto g : smoothedGain)
                smoothedSum += g;
        }
    }
}

//==============================================================================
// Noise Gate
const char* NoiseGatePlugin::xmlTypeName = "ikarosNoiseGate";

NoiseGatePlugin::NoiseGatePlugin (te::PluginCreationInfo info) : BuiltInEffectPlugin (info)
{
    threshold = addSetting ("threshold", "Threshold", { minThresholdDb, maxThresholdDb }, defaultThresholdDb, decibels);

    juce::NormalisableRange<float> releaseRange { 20.0f, 1000.0f };
    releaseRange.setSkewForCentre (150.0f);
    release = addSetting ("release", "Release", releaseRange, 150.0f, milliseconds);
}

NoiseGatePlugin::~NoiseGatePlugin()
{
    notifyListenersOfDeletion();
}

void NoiseGatePlugin::initialise (const te::PluginInitialisationInfo& info)
{
    // prepare() allocates, so it's done here, on the message thread.
    gate.prepare ({ info.sampleRate, (juce::uint32) juce::jmax (1, info.blockSizeSamples), 2 });

    // A steep ratio makes it a gate rather than a gentle expander, and a
    // quick attack keeps the first moment of a note.
    gate.setRatio (10.0f);
    gate.setAttack (1.0f);
    gateThreshold = 1.0f;
    gateRelease = -1.0f;
}

void NoiseGatePlugin::reset()
{
    gate.reset();
}

void NoiseGatePlugin::applyToBuffer (const te::PluginRenderContext& fc)
{
    if (fc.destBuffer == nullptr)
        return;

    SCOPED_REALTIME_CHECK

    const auto channels = (size_t) juce::jmin (2, fc.destBuffer->getNumChannels());

    if (channels == 0)
        return;

    // The setters only do a little arithmetic (no allocation), but there's
    // no need to repeat it every block.
    if (const auto t = threshold->getCurrentValue(); t != gateThreshold)
        gate.setThreshold (gateThreshold = t);

    if (const auto r = release->getCurrentValue(); r != gateRelease)
        gate.setRelease (gateRelease = r);

    // An AudioBlock is a view onto the buffer's samples (it owns nothing).
    // getSubBlock picks out the part of the buffer this call is for.
    juce::dsp::AudioBlock<float> block (fc.destBuffer->getArrayOfWritePointers(), channels,
                                        (size_t) fc.bufferStartSample, (size_t) fc.bufferNumSamples);
    gate.process (juce::dsp::ProcessContextReplacing<float> (block));
}

//==============================================================================
// Distortion
const char* DistortionPlugin::xmlTypeName = "ikarosDistortion";

namespace
{
    // Drive 0..1 is 0..+36 dB into the saturation.
    constexpr float maxDriveDb = 36.0f;

    // Tone 0..1 sweeps the low-pass from 800 Hz to 16 kHz, evenly in octaves.
    float toneToCutoff (float tone)    { return 800.0f * std::pow (20.0f, tone); }
}

DistortionPlugin::DistortionPlugin (te::PluginCreationInfo info) : BuiltInEffectPlugin (info)
{
    drive  = addSetting ("drive", "Drive", { 0.0f, 1.0f }, 0.4f, percent);
    tone   = addSetting ("tone", "Tone", { 0.0f, 1.0f }, 0.6f, [] (float t) { return hertz (toneToCutoff (t)); });
    output = addSetting ("output", "Output", { -24.0f, 12.0f }, 0.0f,
                         [] (float db) { return (db >= 0.05f ? "+" : "") + decibels (db); });

    toneFilter.setType (juce::dsp::StateVariableTPTFilterType::lowpass);
}

DistortionPlugin::~DistortionPlugin()
{
    notifyListenersOfDeletion();
}

double DistortionPlugin::getLatencySeconds()
{
    // The oversampling filters delay the sound by a sample or two.
    return oversampling.getLatencyInSamples() / rate;
}

void DistortionPlugin::initialise (const te::PluginInitialisationInfo& info)
{
    rate = info.sampleRate;
    maxBlockSize = juce::jmax (1, info.blockSizeSamples);

    // Both allocate, so they're done here, on the message thread.
    oversampling.initProcessing ((size_t) maxBlockSize);
    toneFilter.prepare ({ rate, (juce::uint32) maxBlockSize, 2 });

    // Knob changes glide over 50 ms instead of jumping (which would click).
    // The drive is applied at twice the rate, so its glide counts twice the samples.
    driveGain.reset (rate * 2.0, 0.05);
    outputGain.reset (rate, 0.05);
    reset();
}

void DistortionPlugin::reset()
{
    oversampling.reset();
    toneFilter.reset();
}

void DistortionPlugin::applyToBuffer (const te::PluginRenderContext& fc)
{
    if (fc.destBuffer == nullptr)
        return;

    SCOPED_REALTIME_CHECK

    auto& buffer = *fc.destBuffer;
    const auto channels = (size_t) juce::jmin (2, buffer.getNumChannels());

    if (channels == 0)
        return;

    const float pushed = juce::Decibels::decibelsToGain (drive->getCurrentValue() * maxDriveDb);
    driveGain.setTargetValue (pushed);

    // The saturation keeps the peaks under 1 however hard it's pushed, but
    // the quiet parts come up, so it sounds louder. Coming down by half the
    // drive (in dB) roughly evens that out.
    outputGain.setTargetValue (juce::Decibels::decibelsToGain (output->getCurrentValue()) / std::sqrt (pushed));
    toneFilter.setCutoffFrequency (juce::jmin (toneToCutoff (tone->getCurrentValue()), (float) rate * 0.45f));

    juce::dsp::AudioBlock<float> whole (buffer.getArrayOfWritePointers(), channels,
                                        (size_t) fc.bufferStartSample, (size_t) fc.bufferNumSamples);

    // The oversampler was set up for at most maxBlockSize samples at a time.
    for (size_t done = 0; done < whole.getNumSamples(); done += (size_t) maxBlockSize)
    {
        auto block = whole.getSubBlock (done, juce::jmin ((size_t) maxBlockSize, whole.getNumSamples() - done));

        // Up to twice the rate, saturate there, and back down. (The
        // filters in between remove what the saturation adds above the
        // original rate's limit, which would otherwise fold back down as
        // harsh, unmusical tones.)
        auto up = oversampling.processSamplesUp (block);

        for (size_t i = 0; i < up.getNumSamples(); ++i)
        {
            const float g = driveGain.getNextValue();

            for (size_t ch = 0; ch < channels; ++ch)
                up.setSample ((int) ch, (int) i, std::tanh (up.getSample ((int) ch, (int) i) * g));
        }

        oversampling.processSamplesDown (block);

        for (size_t i = 0; i < block.getNumSamples(); ++i)
        {
            const float g = outputGain.getNextValue();

            for (size_t ch = 0; ch < channels; ++ch)
                block.setSample ((int) ch, (int) i, toneFilter.processSample ((int) ch, block.getSample ((int) ch, (int) i)) * g);
        }
    }

    toneFilter.snapToZero();   // flush tiny values that slow the CPU down
}

//==============================================================================
// Tremolo
const char* TremoloPlugin::xmlTypeName = "ikarosTremolo";

TremoloPlugin::TremoloPlugin (te::PluginCreationInfo info) : BuiltInEffectPlugin (info)
{
    juce::NormalisableRange<float> rateRange { 0.5f, 15.0f };
    rateRange.setSkewForCentre (4.0f);
    rateParam = addSetting ("rate", "Rate", rateRange, 5.0f, hertz);
    depth     = addSetting ("depth", "Depth", { 0.0f, 1.0f }, 0.5f, percent);
    shape     = addSetting ("shape", "Shape", { 0.0f, 1.0f }, 0.0f, [] (float s)
                {
                    return s < 0.05f ? juce::String ("Smooth") : s > 0.95f ? juce::String ("Choppy") : percent (s);
                });
}

TremoloPlugin::~TremoloPlugin()
{
    notifyListenersOfDeletion();
}

void TremoloPlugin::initialise (const te::PluginInitialisationInfo& info)
{
    rate = info.sampleRate;
    smoothedDepth.reset (rate, 0.05);
    smoothedShape.reset (rate, 0.05);
    smoothedDepth.setCurrentAndTargetValue (depth->getCurrentValue());
    smoothedShape.setCurrentAndTargetValue (shape->getCurrentValue());
    reset();
}

void TremoloPlugin::reset()
{
    phase = 0.0;
}

void TremoloPlugin::applyToBuffer (const te::PluginRenderContext& fc)
{
    if (fc.destBuffer == nullptr)
        return;

    SCOPED_REALTIME_CHECK

    auto& buffer = *fc.destBuffer;
    const int start = fc.bufferStartSample, num = fc.bufferNumSamples;
    const int channels = juce::jmin (2, buffer.getNumChannels());

    smoothedDepth.setTargetValue (depth->getCurrentValue());
    smoothedShape.setTargetValue (shape->getCurrentValue());
    const double step = rateParam->getCurrentValue() / rate;

    for (int i = 0; i < num; ++i)
    {
        // The pulse: a sine, squeezed towards a square as Shape goes up
        // (tanh flattens its top and bottom; dividing by tanh (k) keeps
        // the peaks at +/-1).
        const float k = 1.0f + smoothedShape.getNextValue() * 9.0f;
        const auto wave = std::tanh (k * std::sin (juce::MathConstants<float>::twoPi * (float) phase)) / std::tanh (k);

        // Full volume at the top of the pulse, down by Depth at the bottom.
        const float gainNow = 1.0f - smoothedDepth.getNextValue() * (0.5f - 0.5f * wave);

        for (int ch = 0; ch < channels; ++ch)
            buffer.getWritePointer (ch, start)[i] *= gainNow;

        phase += step;

        if (phase >= 1.0)
            phase -= 1.0;
    }
}

} // namespace ikaros
