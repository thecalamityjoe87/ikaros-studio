#include "engine/PortedPedals.h"
#include "engine/KlonCentaurDsp.h"
#include <GxDsp.h>

namespace te = tracktion;

namespace ikaros
{

//==============================================================================
void GxPedalPlugin::DspDeleter::operator() (PluginLV2* d) const
{
    // As the original LV2 wrappers do: free its buffers (if it has any),
    // then the DSP itself.
    if (d->activate_plugin != nullptr)
        d->activate_plugin (false, d);

    d->delete_instance (d);
}

GxPedalPlugin::GxPedalPlugin (te::PluginCreationInfo info, juce::String pedalName, std::vector<Stage> stageList)
    : BuiltInEffectPlugin (info), name (std::move (pedalName))
{
    for (auto& stage : stageList)
    {
        StageState s;
        s.stage = stage;
        s.dsp[0].reset (stage.create());
        s.stereo = s.dsp[0]->mono_audio == nullptr && s.dsp[0]->stereo_audio != nullptr;

        if (! s.stereo)
            s.dsp[1].reset (stage.create());   // one per channel

        if (stage.oversampling > 1)
        {
            // Linear phase, like the zita-resampler filters the originals
            // used. Integer latency, so Tracktion can line the track up exactly.
            const auto stages = (size_t) (stage.oversampling == 4 ? 2 : 1);
            s.oversampler = std::make_unique<juce::dsp::Oversampling<float>> (2, stages,
                                juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple, false, true);
        }

        stages.push_back (std::move (s));
    }

    // Until initialise() gives the real rate, so a DSP is never left unset
    // (its members aren't initialised until this).
    forEachDsp ([] (PluginLV2& d) { d.set_samplerate (44100, &d); });
}

GxPedalPlugin::~GxPedalPlugin() = default;

void GxPedalPlugin::forEachDsp (const std::function<void (PluginLV2&)>& f)
{
    for (auto& s : stages)
        for (auto& d : s.dsp)
            if (d != nullptr)
                f (*d);
}

te::AutomatableParameter::Ptr GxPedalPlugin::addControl (std::uint32_t port, const juce::Identifier& id, const juce::String& settingName,
                                                         juce::NormalisableRange<float> range, float defaultValue,
                                                         std::function<juce::String (float)> toText)
{
    auto parameter = addSetting (id, settingName, range, defaultValue, std::move (toText));
    auto value = std::make_unique<float> (parameter->getCurrentValue());

    // Each DSP reads the control from this float from now on (the ones
    // that don't use this port ignore it).
    auto* address = value.get();
    forEachDsp ([port, address] (PluginLV2& d) { d.connect_ports (port, address, &d); });

    controls.push_back ({ parameter, std::move (value) });
    return parameter;
}

te::AutomatableParameter::Ptr GxPedalPlugin::addChoice (std::uint32_t port, const juce::Identifier& id, const juce::String& settingName,
                                                        juce::StringArray names, int defaultIndex)
{
    // An interval of 1: the setting only takes whole numbers.
    return addControl (port, id, settingName, { 0.0f, (float) (names.size() - 1), 1.0f }, (float) defaultIndex,
                       [names] (float v) { return names[juce::jlimit (0, names.size() - 1, juce::roundToInt (v))]; });
}

juce::NormalisableRange<float> GxPedalPlugin::logRange (float start, float end)
{
    // A NormalisableRange can map the knob's 0..1 travel with functions of
    // our own: here, so that each octave takes the same travel.
    return juce::NormalisableRange<float> (start, end,
                                           [] (float s, float e, float proportion) { return s * std::pow (e / s, proportion); },
                                           [] (float s, float e, float value)      { return std::log (value / s) / std::log (e / s); });
}

double GxPedalPlugin::getLatencySeconds()
{
    double samples = 0.0;

    for (auto& s : stages)
        if (s.oversampler != nullptr)
            samples += s.oversampler->getLatencyInSamples();

    return samples / rate;
}

void GxPedalPlugin::initialise (const te::PluginInitialisationInfo& info)
{
    silence.prepare (info.sampleRate, 4.0);   // tail: long enough for Spring Tank's reverb
    rate = info.sampleRate;
    maxBlockSize = juce::jmax (1, info.blockSizeSamples);

    for (auto& s : stages)
    {
        // The LV2 order: the rate first, then activate (which allocates any
        // buffers the DSP needs, sized for the rate). If it was already
        // active at another rate, its buffers are freed first.
        const auto dspRate = (std::uint32_t) juce::roundToInt (rate * s.stage.rateFactor);

        for (auto& d : s.dsp)
        {
            if (d == nullptr)
                continue;

            if (d->activate_plugin != nullptr)
                d->activate_plugin (false, d.get());

            d->set_samplerate (dspRate, d.get());

            if (d->activate_plugin != nullptr)
                d->activate_plugin (true, d.get());
        }

        // Allocates, so it's done here, on the message thread.
        if (s.oversampler != nullptr)
            s.oversampler->initProcessing ((size_t) maxBlockSize);
    }
}

void GxPedalPlugin::reset()
{
    for (auto& s : stages)
    {
        if (s.oversampler != nullptr)
            s.oversampler->reset();

        for (auto& d : s.dsp)
            if (d != nullptr && d->clear_state != nullptr)
                d->clear_state (d.get());
    }
}

void GxPedalPlugin::process (StageState& s, juce::dsp::AudioBlock<float>& block)
{
    const int n = (int) block.getNumSamples();

    if (s.stereo)
    {
        // In place, both channels at once (with one channel, it's both).
        auto* left = block.getChannelPointer (0);
        auto* right = block.getNumChannels() > 1 ? block.getChannelPointer (1) : left;
        s.dsp[0]->stereo_audio (n, left, right, left, right, s.dsp[0].get());
        return;
    }

    // In place, as the original wrappers run it: each channel through its own copy.
    for (size_t ch = 0; ch < juce::jmin ((size_t) 2, block.getNumChannels()); ++ch)
    {
        auto* samples = block.getChannelPointer (ch);
        s.dsp[ch]->mono_audio (n, samples, samples, s.dsp[ch].get());
    }
}

void GxPedalPlugin::applyToBuffer (const te::PluginRenderContext& fc)
{
    if (fc.destBuffer == nullptr || fc.bufferNumSamples <= 0)
        return;

    // Nothing to do while the input is silent and the tail is over (see SilenceSkip).
    if (! silence.shouldProcess (*fc.destBuffer, fc.bufferStartSample, fc.bufferNumSamples))
        return;

    SCOPED_REALTIME_CHECK

    // Tiny "denormal" numbers in the filters' feedback (as a note dies
    // away) are very slow to work with; this treats them as zero while
    // we're in this function.
    juce::ScopedNoDenormals noDenormals;

    // The DSPs read their controls at the start of each call.
    for (auto& c : controls)
        *c.value = c.parameter->getCurrentValue();

    auto& buffer = *fc.destBuffer;
    const auto channels = (size_t) juce::jmin (2, buffer.getNumChannels());

    if (channels == 0)
        return;

    juce::dsp::AudioBlock<float> whole (buffer.getArrayOfWritePointers(), channels,
                                        (size_t) fc.bufferStartSample, (size_t) fc.bufferNumSamples);

    // The oversamplers were set up for at most maxBlockSize samples at a time.
    for (size_t done = 0; done < whole.getNumSamples(); done += (size_t) maxBlockSize)
    {
        auto block = whole.getSubBlock (done, juce::jmin ((size_t) maxBlockSize, whole.getNumSamples() - done));

        for (auto& s : stages)
        {
            if (s.oversampler == nullptr)
            {
                process (s, block);
                continue;
            }

            // Up to the higher rate, run the stage there, and back down.
            auto up = s.oversampler->processSamplesUp (block);
            process (s, up);
            s.oversampler->processSamplesDown (block);
        }
    }
}

//==============================================================================
// The pedals
//==============================================================================
namespace
{
    // Knob value text.
    juce::String signedDecibels (float db)     { return (db >= 0.05f ? "+" : "") + BuiltInEffectPlugin::decibels (db); }
    juce::String wholePercent (float v)        { return juce::String (juce::roundToInt (v)) + "%"; }   // for 0..100 settings
    juce::String plainNumber (float v)         { return juce::String (v, 1); }
}

// Yellow Drive (GxSD1)
const char* YellowDrivePlugin::xmlTypeName = "ikarosYellowDrive";

YellowDrivePlugin::YellowDrivePlugin (te::PluginCreationInfo info)
    : GxPedalPlugin (info, getPluginName(), { { gx::sd1::create } })
{
    // The DSP itself turns Drive into its exponential curve.
    addControl (gx::sd1::drive, "drive", "Drive", { 0.0f, 1.0f }, 0.5f, percent);
    addControl (gx::sd1::tone,  "tone",  "Tone",  { 100.0f, 1000.0f }, 400.0f, hertz);
    addControl (gx::sd1::level, "level", "Level", { -20.0f, 4.0f }, -16.0f, signedDecibels);
}

YellowDrivePlugin::~YellowDrivePlugin()    { notifyListenersOfDeletion(); }   // Tracktion needs this in the most-derived class

// Stack Drive (GxGuvnor)
const char* StackDrivePlugin::xmlTypeName = "ikarosStackDrive";

StackDrivePlugin::StackDrivePlugin (te::PluginCreationInfo info)
    : GxPedalPlugin (info, getPluginName(), { { gx::guvnor::create } })
{
    addControl (gx::guvnor::gain,   "gain",   "Gain",   { 0.0f, 1.0f }, 0.5f, percent);
    addControl (gx::guvnor::bass,   "bass",   "Bass",   { 0.0f, 1.0f }, 0.5f, percent);
    addControl (gx::guvnor::mid,    "mid",    "Middle", { 0.0f, 1.0f }, 0.5f, percent);
    addControl (gx::guvnor::treble, "treble", "Treble", { 0.0f, 1.0f }, 0.5f, percent);
    addControl (gx::guvnor::level,  "level",  "Level",  { 0.0f, 1.0f }, 0.5f, percent);
}

StackDrivePlugin::~StackDrivePlugin()    { notifyListenersOfDeletion(); }

// Hot Plate (GxHotBox)
const char* HotPlatePlugin::xmlTypeName = "ikarosHotPlate";

HotPlatePlugin::HotPlatePlugin (te::PluginCreationInfo info)
    : GxPedalPlugin (info, getPluginName(), { { gx::hotbox::create } })
{
    addControl (gx::hotbox::gain,   "gain",   "Gain",   { 0.0f, 1.0f }, 0.5f, percent);
    addControl (gx::hotbox::bass,   "bass",   "Bass",   { 0.0f, 1.0f }, 0.5f, percent);
    addControl (gx::hotbox::treble, "treble", "Treble", { 0.0f, 1.0f }, 0.5f, percent);
    addControl (gx::hotbox::volume, "volume", "Volume", { 0.0f, 1.0f }, 0.25f, percent);
}

HotPlatePlugin::~HotPlatePlugin()    { notifyListenersOfDeletion(); }

// Tube Drive (GxBottleRocket)
const char* TubeDrivePlugin::xmlTypeName = "ikarosTubeDrive";

TubeDrivePlugin::TubeDrivePlugin (te::PluginCreationInfo info)
    : GxPedalPlugin (info, getPluginName(), { { gx::bottlerocket::create } })
{
    addControl (gx::bottlerocket::gain,   "gain",   "Gain",   { 0.0f, 1.0f }, 0.5f, percent);
    addControl (gx::bottlerocket::bass,   "bass",   "Bass",   { 0.0f, 1.0f }, 0.5f, percent);
    addControl (gx::bottlerocket::treble, "treble", "Treble", { 0.0f, 1.0f }, 0.5f, percent);
    addControl (gx::bottlerocket::volume, "volume", "Volume", { 0.0f, 1.0f }, 0.5f, percent);
}

TubeDrivePlugin::~TubeDrivePlugin()    { notifyListenersOfDeletion(); }

// Mellow Drive (GxEternity). The DSP sets its filters up for twice the rate
// it's told (rateFactor 1), and we run it at that rate (oversampling 2).
const char* MellowDrivePlugin::xmlTypeName = "ikarosMellowDrive";

MellowDrivePlugin::MellowDrivePlugin (te::PluginCreationInfo info)
    : GxPedalPlugin (info, getPluginName(), { { gx::eternity::create, 2, 1 } })
{
    addControl (gx::eternity::drive, "drive", "Drive", { 0.0f, 1.0f }, 0.5f, percent);
    addControl (gx::eternity::glass, "glass", "Glass", { 0.0f, 1.0f }, 0.5f, percent);
    addControl (gx::eternity::level, "level", "Level", { 0.0f, 1.0f }, 0.5f, percent);
}

MellowDrivePlugin::~MellowDrivePlugin()    { notifyListenersOfDeletion(); }

// Sun Fuzz (GxSunFace)
const char* SunFuzzPlugin::xmlTypeName = "ikarosSunFuzz";

SunFuzzPlugin::SunFuzzPlugin (te::PluginCreationInfo info)
    : GxPedalPlugin (info, getPluginName(), { { gx::sunface::create } })
{
    addControl (gx::sunface::drive,  "drive",  "Fuzz",   { 0.0f, 1.0f }, 0.5f, percent);
    addControl (gx::sunface::input,  "input",  "Input",  { 0.0f, 1.0f }, 0.5f, percent);
    addControl (gx::sunface::volume, "volume", "Volume", { 0.0f, 1.0f }, 0.5f, percent);
}

SunFuzzPlugin::~SunFuzzPlugin()    { notifyListenersOfDeletion(); }

// Vintage Fuzz (GxSuppaToneBender): input stage, clipping at 4x, output stage.
const char* VintageFuzzPlugin::xmlTypeName = "ikarosVintageFuzz";

VintageFuzzPlugin::VintageFuzzPlugin (te::PluginCreationInfo info)
    : GxPedalPlugin (info, getPluginName(), { { gx::tonebender::createInput },
                                              { gx::tonebender::createClip, 4, 4 },
                                              { gx::tonebender::createOutput } })
{
    addControl (gx::tonebender::fuzz,   "fuzz",   "Fuzz",   { 0.0f, 1.0f }, 0.5f, percent);
    addControl (gx::tonebender::tone,   "tone",   "Tone",   { 0.0f, 1.0f }, 0.5f, percent);
    addControl (gx::tonebender::volume, "volume", "Volume", { 0.0f, 1.0f }, 0.5f, percent);
}

VintageFuzzPlugin::~VintageFuzzPlugin()    { notifyListenersOfDeletion(); }

// Super Fuzz (GxSuperFuzz): input stage, clipping at 4x, output stage.
const char* SuperFuzzPlugin::xmlTypeName = "ikarosSuperFuzz";

SuperFuzzPlugin::SuperFuzzPlugin (te::PluginCreationInfo info)
    : GxPedalPlugin (info, getPluginName(), { { gx::superfuzz::createInput },
                                              { gx::superfuzz::createClip, 4, 4 },
                                              { gx::superfuzz::createOutput } })
{
    addControl (gx::superfuzz::expander, "expander", "Fuzz",   { 0.0f, 1.0f }, 0.5f, percent);
    addControl (gx::superfuzz::balance,  "balance",  "Volume", { 0.0f, 1.0f }, 0.5f, percent);
    addControl (gx::superfuzz::trim,     "trim",     "Trim",   { 0.0f, 0.49f }, 0.25f, plainNumber);
    addChoice  (gx::superfuzz::tone,     "tone",     "Tone",   { "On", "Off" }, 0);   // the original's order: 0 = on
}

SuperFuzzPlugin::~SuperFuzzPlugin()    { notifyListenersOfDeletion(); }

// Envelope Wah (GxQuack)
const char* EnvelopeWahPlugin::xmlTypeName = "ikarosEnvelopeWah";

EnvelopeWahPlugin::EnvelopeWahPlugin (te::PluginCreationInfo info)
    : GxPedalPlugin (info, getPluginName(), { { gx::quack::create } })
{
    addControl (gx::quack::depth, "depth", "Depth", { 0.0f, 1.0f }, 1.0f, percent);
    addControl (gx::quack::peak,  "peak",  "Peak",  { -6.0f, 20.0f }, 5.0f, plainNumber);
    addControl (gx::quack::gain,  "gain",  "Level", { -12.0f, 6.0f }, 0.0f, signedDecibels);
    addChoice  (gx::quack::mode,  "mode",  "Mode",  { "Low", "High", "Original" }, 2);
    addChoice  (gx::quack::range, "range", "Range", { "Low", "High" }, 1);
    addChoice  (gx::quack::tone,  "tone",  "Tone",  { "Off", "Low", "High" }, 0);
    addChoice  (gx::quack::drive, "drive", "Drive", { "Off", "On" }, 0);
}

EnvelopeWahPlugin::~EnvelopeWahPlugin()    { notifyListenersOfDeletion(); }

// Slow Swell (GxSlowGear)
const char* SlowSwellPlugin::xmlTypeName = "ikarosSlowSwell";

SlowSwellPlugin::SlowSwellPlugin (te::PluginCreationInfo info)
    : GxPedalPlugin (info, getPluginName(), { { gx::slowgear::create } })
{
    addControl (gx::slowgear::threshold, "threshold", "Threshold", { 0.0f, 10.0f }, 1.0f, plainNumber);
    addControl (gx::slowgear::uptime,    "uptime",    "Swell",     { 0.0f, 1000.0f }, 100.0f, milliseconds);
    addControl (gx::slowgear::downtime,  "downtime",  "Fade",      { 0.0f, 1000.0f }, 5.0f, milliseconds);
}

SlowSwellPlugin::~SlowSwellPlugin()    { notifyListenersOfDeletion(); }

// Jet Flanger (Guitarix's gx_flanger)
const char* JetFlangerPlugin::xmlTypeName = "ikarosJetFlanger";

JetFlangerPlugin::JetFlangerPlugin (te::PluginCreationInfo info)
    : GxPedalPlugin (info, getPluginName(), { { gx::flanger::create } })
{
    addControl (gx::flanger::freq,     "freq",     "Speed",    logRange (0.05f, 10.0f), 0.2f, hertz);
    addControl (gx::flanger::depth,    "depth",    "Depth",    { 0.0f, 5.0f }, 0.5f, [] (float ms) { return juce::String (ms, 2) + " ms"; });
    addControl (gx::flanger::width,    "width",    "Width",    { 0.0f, 10.0f }, 5.0f, [] (float ms) { return juce::String (ms, 1) + " ms"; });
    addControl (gx::flanger::feedback, "feedback", "Feedback", { -0.99f, 0.99f }, -0.707f, percent);
    addControl (gx::flanger::wet,      "wet",      "Wet",      { 0.0f, 100.0f }, 100.0f, wholePercent);
    addControl (gx::flanger::mix,      "mix",      "Mix",      { -1.0f, 1.0f }, 0.0f, plainNumber);
}

JetFlangerPlugin::~JetFlangerPlugin()    { notifyListenersOfDeletion(); }

// Spring Tank (Guitarix's stereo gx_reverb)
const char* SpringTankPlugin::xmlTypeName = "ikarosSpringTank";

SpringTankPlugin::SpringTankPlugin (te::PluginCreationInfo info)
    : GxPedalPlugin (info, getPluginName(), { { gx::reverb::create } })
{
    addControl (gx::reverb::wetDry,   "dry_wet",  "Mix",     { 0.0f, 100.0f }, 50.0f, wholePercent);
    addControl (gx::reverb::roomSize, "roomsize", "Size",    { 0.0f, 1.0f }, 0.5f, percent);
    addControl (gx::reverb::damp,     "damp",     "Damping", { 0.0f, 1.0f }, 0.2f, percent);
    addControl (gx::reverb::lfo,      "lfo",      "LFO",     { 0.2f, 5.0f }, 0.2f, hertz);
    addChoice  (gx::reverb::invert,   "mode",     "Mode",    { "Linear", "Ping-pong" }, 0);
}

SpringTankPlugin::~SpringTankPlugin()    { notifyListenersOfDeletion(); }

//==============================================================================
// Golden Boost (KlonCentaur)
const char* GoldenBoostPlugin::xmlTypeName = "ikarosGoldenBoost";

GoldenBoostPlugin::GoldenBoostPlugin (te::PluginCreationInfo info)
    : BuiltInEffectPlugin (info), dsp (std::make_unique<KlonCentaurDsp>())
{
    gain   = addSetting ("gain",   "Gain",   { 0.0f, 1.0f }, 0.5f, percent);
    treble = addSetting ("treble", "Treble", { 0.0f, 1.0f }, 0.5f, percent);
    level  = addSetting ("level",  "Level",  { 0.0f, 1.0f }, 0.5f, percent);

    // Until initialise() gives the real rate, so it's never left unset.
    dsp->prepare (44100.0, 512);
}

GoldenBoostPlugin::~GoldenBoostPlugin()
{
    notifyListenersOfDeletion();
}

double GoldenBoostPlugin::getLatencySeconds()
{
    return dsp->getLatencyInSamples() / rate;
}

void GoldenBoostPlugin::initialise (const te::PluginInitialisationInfo& info)
{
    silence.prepare (info.sampleRate, 1.0);   // tail: its filters
    rate = info.sampleRate;
    dsp->prepare (rate, juce::jmax (1, info.blockSizeSamples));   // allocates: message thread
}

void GoldenBoostPlugin::reset()
{
    dsp->reset();
}

void GoldenBoostPlugin::applyToBuffer (const te::PluginRenderContext& fc)
{
    if (fc.destBuffer == nullptr || fc.bufferNumSamples <= 0)
        return;

    // Nothing to do while the input is silent and the tail is over (see SilenceSkip).
    if (! silence.shouldProcess (*fc.destBuffer, fc.bufferStartSample, fc.bufferNumSamples))
        return;

    SCOPED_REALTIME_CHECK
    juce::ScopedNoDenormals noDenormals;   // as ChowCentaur does

    auto& buffer = *fc.destBuffer;
    const int channels = juce::jmin (2, buffer.getNumChannels());
    float* pointers[2] = { nullptr, nullptr };

    for (int ch = 0; ch < channels; ++ch)
        pointers[ch] = buffer.getWritePointer (ch, fc.bufferStartSample);

    if (channels > 0)
        dsp->process (pointers, channels, fc.bufferNumSamples,
                      gain->getCurrentValue(), treble->getCurrentValue(), level->getCurrentValue());
}

} // namespace ikaros
