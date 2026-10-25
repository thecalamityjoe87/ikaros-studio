#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

namespace ikaros
{

/*  Lets an effect skip its DSP while its input is silent and whatever it was
    still sounding (a reverb's tail, a delay's echoes) has died away.

    Tracktion keeps calling every plug-in while the transport is stopped (so
    live input and tails still work), and on tracks that are silent for a
    while: without this, a stopped session kept the audio thread 14% busy
    (docs/perf-report.md, finding C3).

    Any sound at the input resumes processing in the same block, so nothing
    is cut off. A skipped block is left as it came in, i.e. silent, which is
    what the effect would have put out anyway once its tail was over.

        // initialise():     silence.prepare (sampleRate, tailSeconds);
        // applyToBuffer():  if (! silence.shouldProcess (buffer, start, numSamples)) return;

    Audio thread only (apart from prepare, which Tracktion calls while the
    plug-in isn't processing).
*/
class SilenceSkip
{
public:
    /** tailSeconds: how long the effect can go on sounding after its input
        stops. Generous is fine; too short would cut a tail off. */
    void prepare (double sampleRate, double tailSeconds) noexcept
    {
        rate = sampleRate;
        setTail (tailSeconds);
        silentSamples = 0;
    }

    /** For an effect whose tail depends on a setting (a reverb's size, say). */
    void setTail (double tailSeconds) noexcept
    {
        tailSamples = (juce::int64) (rate * tailSeconds);
    }

    /** False when the block (and the last tailSeconds before it) is silent. */
    bool shouldProcess (const juce::AudioBuffer<float>& buffer, int startSample, int numSamples) noexcept
    {
        if (buffer.getMagnitude (startSample, numSamples) > threshold)
        {
            silentSamples = 0;
            return true;
        }

        silentSamples += numSamples;
        return silentSamples <= tailSamples;
    }

private:
    static constexpr float threshold = 1.0e-6f;   // -120 dB: inaudible, but above denormal noise
    double rate = 44100.0;
    juce::int64 tailSamples = 0, silentSamples = 0;
};

} // namespace ikaros
