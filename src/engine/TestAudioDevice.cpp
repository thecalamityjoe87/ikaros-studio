#include "engine/TestAudioDevice.h"
#include "engine/AudioEngine.h"
#include "engine/TrackHelpers.h"

namespace te = tracktion;

namespace ikaros
{

static te::HostedAudioDeviceInterface::Parameters toParameters (const TestAudioDevice::Settings& s)
{
    te::HostedAudioDeviceInterface::Parameters p;
    p.sampleRate = s.sampleRate;
    p.blockSize = s.blockSize;
    p.useMidiDevices = false;               // no real MIDI devices either
    p.inputChannels = s.numInputs;
    p.outputChannels = s.numOutputs;
    p.inputLatencyNumSamples = s.inputLatency;
    p.outputLatencyNumSamples = s.outputLatency;
    return p;
}

TestAudioDevice::TestAudioDevice (AudioEngine& e, Settings s)
    : engine (e), settings (s)
{
    auto& dm = engine.getTeEngine().getDeviceManager();

    // Blocks come as fast as the test sends them, so the CPU meter reads
    // nonsense; without this, Tracktion would mute "overloaded" blocks.
    dm.setCpuLimitBeforeMuting (1000.0);
    reopen();
}

TestAudioDevice::~TestAudioDevice()
{
    auto& dm = engine.getTeEngine().getDeviceManager();
    dm.deviceManager.closeAudioDevice();
    dm.removeHostedAudioDeviceInterface();
}

void TestAudioDevice::reopen()
{
    auto& dm = engine.getTeEngine().getDeviceManager();
    auto& hosted = dm.getHostedAudioDeviceInterface();
    hosted.initialise (toParameters (settings));

    // Being made for plug-ins, the hosted device turns every input mono.
    // Lay them out again the app's way (see IkarosEngineBehaviour).
    dm.applyWaveDeviceLayout (juce::XmlElement ("AUDIODEVICE_LAYOUT"));

    hosted.prepareToPlay (settings.sampleRate, settings.blockSize);
    dm.dispatchPendingUpdates();
    engine.reattachAudioCallbacks();
}

void TestAudioDevice::close()
{
    engine.getTeEngine().getDeviceManager().deviceManager.closeAudioDevice();
}

void TestAudioDevice::setSampleRate (double rate)
{
    settings.sampleRate = rate;
    engine.getTeEngine().getDeviceManager().getHostedAudioDeviceInterface().prepareToPlay (rate, settings.blockSize);
}

juce::AudioBuffer<float> TestAudioDevice::process (const juce::AudioBuffer<float>& input)
{
    auto& hosted = engine.getTeEngine().getDeviceManager().getHostedAudioDeviceInterface();
    const int numSamples = input.getNumSamples();
    const int numChannels = juce::jmax (settings.numInputs, settings.numOutputs);

    juce::AudioBuffer<float> output (settings.numOutputs, numSamples);
    juce::AudioBuffer<float> block (numChannels, settings.blockSize);
    juce::MidiBuffer midi;

    // The hosted device works in place: input channels in, output channels out.
    for (int start = 0; start < numSamples; start += settings.blockSize)
    {
        const int num = juce::jmin (settings.blockSize, numSamples - start);
        block.setSize (numChannels, num, false, false, true);
        block.clear();

        for (int ch = 0; ch < juce::jmin (settings.numInputs, input.getNumChannels()); ++ch)
            block.copyFrom (ch, 0, input, ch, start, num);

        hosted.processBlock (block, midi);

        for (int ch = 0; ch < settings.numOutputs; ++ch)
            output.copyFrom (ch, start, block, ch, 0, num);
    }

    return output;
}

juce::AudioBuffer<float> TestAudioDevice::process (int numSamples)
{
    juce::AudioBuffer<float> silence (settings.numInputs, numSamples);
    silence.clear();
    return process (silence);
}

juce::AudioBuffer<float> TestAudioDevice::processLoopedBack (int numSamples)
{
    juce::AudioBuffer<float> output (settings.numOutputs, numSamples);

    if (loopBack.getNumSamples() != settings.blockSize)
    {
        loopBack.setSize (settings.numInputs, settings.blockSize);
        loopBack.clear();
    }

    for (int start = 0; start < numSamples; start += settings.blockSize)
    {
        const int num = juce::jmin (settings.blockSize, numSamples - start);
        juce::AudioBuffer<float> in (settings.numInputs, num);

        for (int ch = 0; ch < settings.numInputs; ++ch)
            in.copyFrom (ch, 0, loopBack, ch, 0, num);

        auto out = process (in);
        loopBack.clear();

        for (int ch = 0; ch < juce::jmin (settings.numInputs, settings.numOutputs); ++ch)
            loopBack.copyFrom (ch, 0, out, ch, 0, num);

        for (int ch = 0; ch < settings.numOutputs; ++ch)
            output.copyFrom (ch, start, out, ch, 0, num);
    }

    return output;
}

tracktion::Edit* TestAudioDevice::getEditOf (AudioEngine& engine)
{
    return engine.getEdit();   // TestAudioDevice is one of AudioEngine's friends
}

} // namespace ikaros
