#pragma once

#include <memory>

namespace ikaros
{

//==============================================================================
/**
    The Golden Boost's sound: Jatin Chowdhury's KlonCentaur circuit model
    (external/kloncentaur, BSD 3-clause), run the way its plugin
    (ChowCentaur) runs it in "traditional" mode, for up to two channels,
    each on its own.

    The model's code needs JUCE's names without the juce:: prefix (its
    header does `using namespace juce`), so it's compiled in a file of its
    own (KlonCentaurDsp.cpp) and this class keeps it out of sight: "Impl" is
    only defined in that file (the "pimpl" pattern).

    prepare() allocates, so it's for the message thread; process() is for
    the audio thread and never allocates or locks.
*/
class KlonCentaurDsp
{
public:
    KlonCentaurDsp();
    ~KlonCentaurDsp();

    /** Sets everything up for a sample rate and the most samples a
        process() call will be given. */
    void prepare (double sampleRate, int maxBlockSize);
    void reset();

    /** Processes 1 or 2 channels in place. The knobs are 0..1, as on the
        original. */
    void process (float* const* channels, int numChannels, int numSamples, float gain, float treble, float level);

    /** The delay the oversampling adds, in samples. */
    float getLatencyInSamples() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

} // namespace ikaros
