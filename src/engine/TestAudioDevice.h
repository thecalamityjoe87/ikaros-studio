#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

namespace ikaros
{

class AudioEngine;

//==============================================================================
/**
    For tests only (built into the smoke test, not the app): a pretend audio
    interface that the test drives itself, block by block, instead of a sound
    card. Audio the test passes in is the input; what comes back is what
    would play through the speakers. Nothing touches the computer's real
    audio devices, microphone or speakers.

    It wraps Tracktion's HostedAudioDeviceInterface (made for plug-in hosts),
    which can also act out a device's input and output latency: the audio
    really is delayed by that much, and the device reports it, the way a
    sound card's driver does.

    Create the AudioEngine with openAudioDevice = false, then one of these.
    Runs on the message thread (the "audio thread" is whoever calls process).
*/
class TestAudioDevice
{
public:
    struct Settings
    {
        double sampleRate = 48000.0;
        int blockSize = 256;
        int numInputs = 2, numOutputs = 2;
        int inputLatency = 0, outputLatency = 0;    // samples
    };

    TestAudioDevice (AudioEngine&, Settings);
    ~TestAudioDevice();

    /** Plays `input` (numInputs channels) into the device; returns what came
        out (numOutputs channels, the same length). */
    juce::AudioBuffer<float> process (const juce::AudioBuffer<float>&);

    /** Silence in, for numSamples. */
    juce::AudioBuffer<float> process (int numSamples);

    /** A cable from the outputs back to the inputs: each block's output is
        the next block's input, so the round trip is the device's latency
        plus one block. (Tracktion allows for that block; see RecordingGuard.h.) */
    juce::AudioBuffer<float> processLoopedBack (int numSamples);

    /** The device switches sample rate, as some do mid-session. (Only
        without simulated latency: Tracktion's latency simulation can't be
        prepared twice.) */
    void setSampleRate (double);

    /** The device goes away (closed or unplugged), and comes back. */
    void close();
    void reopen();

    const Settings& getSettings() const noexcept    { return settings; }

private:
    AudioEngine& engine;
    Settings settings;
    juce::AudioBuffer<float> loopBack;      // the last block's output, for processLoopedBack

    JUCE_DECLARE_NON_COPYABLE (TestAudioDevice)
};

} // namespace ikaros
