// The KlonCentaur circuit model (external/kloncentaur), compiled into one
// file. It's kept apart from the rest of the engine because its header
// (our stand-in for ChowCentaur's pch.h) says `using namespace juce`.
//
// The model's own files come first, so they see exactly what they expect.

#include <pch.h>
#include <complex>

// ClippingStage.cpp fills a lookup table of the Wright omega function with
// wrightomega() from toms917, which ChowCentaur bundles. That code's license
// isn't clear, so here's our own, for the real numbers the table needs
// (-1..1): Wright omega of x is the w for which w + ln (w) = x, found with
// Newton's method to full double precision.
static std::complex<double> wrightomega (std::complex<double> z)
{
    const double x = z.real();
    double w = x > 1.0 ? x : std::exp ((x - 1.0) * 0.5);   // close enough to start from

    for (int i = 0; i < 50; ++i)
    {
        const double step = (w + std::log (w) - x) / (1.0 + 1.0 / w);
        w = std::max (w - step, w * 0.1);   // (never at or below zero)

        if (std::abs (step) < 1.0e-16 * w)
            break;
    }

    return { w, 0.0 };
}

#include "../../external/kloncentaur/ChowCentaur/CommonProcessors/InputBufferProcessor.cpp"
#include "../../external/kloncentaur/ChowCentaur/CommonProcessors/ToneFilterProcessor.cpp"
#include "../../external/kloncentaur/ChowCentaur/CommonProcessors/OutputStageProcessor.cpp"
#include "../../external/kloncentaur/ChowCentaur/GainStageProcessors/PreAmpStage.cpp"
#include "../../external/kloncentaur/ChowCentaur/GainStageProcessors/ClippingStage.cpp"
#include "../../external/kloncentaur/ChowCentaur/GainStageProcessors/FeedForward2.cpp"
#include "../../external/kloncentaur/ChowCentaur/GainStageProcessors/AmpStage.h"
#include "../../external/kloncentaur/ChowCentaur/GainStageProcessors/SummingAmp.h"

#include "engine/KlonCentaurDsp.h"

namespace ikaros
{

//==============================================================================
// Everything ChowCentaur's plugin (ChowCentaurPlugin.cpp) and its gain stage
// (GainStageProc.cpp) hold, for two channels. Those two files aren't used
// as they are: they need the plugin's parameter tree. process() below does
// what they do, in the same order.
struct KlonCentaurDsp::Impl
{
    // The circuit's parts hold pointers to each other, so they're never
    // copied or moved: each is made in place, left and right separately,
    // as in GainStageProc.
    explicit Impl (double sampleRate)
        : preAmpL (sampleRate), preAmpR (sampleRate),
          clipL (sampleRate * 2), clipR (sampleRate * 2),
          ff2L (sampleRate), ff2R (sampleRate)
    {
    }

    InputBufferProcessor inProc[2];
    ToneFilterProcessor tone[2];
    OutputStageProc outProc[2];

    // The gain stage. The clipping runs at twice the rate.
    GainStageSpace::PreAmpWDF preAmpL, preAmpR;
    GainStageSpace::PreAmpWDF* preAmp[2] { &preAmpL, &preAmpR };
    GainStageSpace::ClippingWDF clipL, clipR;
    GainStageSpace::ClippingWDF* clip[2] { &clipL, &clipR };
    GainStageSpace::FeedForward2WDF ff2L, ff2R;
    GainStageSpace::FeedForward2WDF* ff2[2] { &ff2L, &ff2R };
    GainStageSpace::AmpStage amp[2];
    GainStageSpace::SummingAmp sumAmp[2];
    juce::dsp::Oversampling<float> os { 2, 1, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR };
    juce::AudioBuffer<float> ff1Buff, ff2Buff;   // the gain stage's side chains

    // The plugin's DC blocker on its output.
    juce::dsp::IIR::Filter<float> dcBlocker[2];

    int maxBlockSize = 0;
};

KlonCentaurDsp::KlonCentaurDsp() = default;
KlonCentaurDsp::~KlonCentaurDsp() = default;

void KlonCentaurDsp::prepare (double sampleRate, int maxBlockSize)
{
    // As ChowCentaur::prepareToPlay and GainStageProc::reset. The circuit's
    // parts take the rate when they're made, so they're made again here.
    auto next = std::make_unique<Impl> (sampleRate);
    next->maxBlockSize = maxBlockSize;
    next->os.initProcessing ((size_t) maxBlockSize);
    next->ff1Buff.setSize (2, maxBlockSize);
    next->ff2Buff.setSize (2, maxBlockSize);

    const auto dcCoefficients = juce::dsp::IIR::Coefficients<float>::makeHighPass (sampleRate, 35.0f);

    for (int ch = 0; ch < 2; ++ch)
    {
        next->inProc[ch].prepare ((float) sampleRate);
        next->tone[ch].prepare ((float) sampleRate);
        next->outProc[ch].prepare ((float) sampleRate);
        next->amp[ch].prepare ((float) sampleRate);
        next->sumAmp[ch].prepare ((float) sampleRate);
        next->dcBlocker[ch].coefficients = dcCoefficients;
        next->dcBlocker[ch].reset();
    }

    impl = std::move (next);
}

void KlonCentaurDsp::reset()
{
    if (impl == nullptr)
        return;

    impl->os.reset();

    for (int ch = 0; ch < 2; ++ch)
    {
        impl->preAmp[ch]->reset();
        impl->clip[ch]->reset();
        impl->ff2[ch]->reset();
        impl->dcBlocker[ch].reset();
    }
}

float KlonCentaurDsp::getLatencyInSamples() const
{
    return impl != nullptr ? impl->os.getLatencyInSamples() : 0.0f;
}

void KlonCentaurDsp::process (float* const* channels, int numChannels, int numSamples, float gain, float treble, float level)
{
    if (impl == nullptr)
        return;

    auto& d = *impl;
    numChannels = juce::jmin (numChannels, 2);

    // The side-chain buffers and the oversampler were sized for maxBlockSize.
    for (int done = 0; done < numSamples; done += d.maxBlockSize)
    {
        const int n = juce::jmin (d.maxBlockSize, numSamples - done);
        float* chunk[2] = { channels[0] + done, numChannels > 1 ? channels[1] + done : nullptr };

        // An AudioBuffer that refers to the audio where it is (no copy, no allocation).
        juce::AudioBuffer<float> buffer (chunk, numChannels, n);

        // ChowCentaur::processInternalBuffer, first part: the input buffer.
        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto* x = buffer.getWritePointer (ch);
            juce::FloatVectorOperations::multiply (x, 0.5f, n);
            d.inProc[ch].processBlock (x, n);
            juce::FloatVectorOperations::clip (x, x, -4.5f, 4.5f, n);   // op amp clipping
        }

        // GainStageProc::processBlock
        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto* x = buffer.getWritePointer (ch);
            auto* x1 = d.ff1Buff.getWritePointer (ch);
            auto* x2 = d.ff2Buff.getWritePointer (ch);

            juce::FloatVectorOperations::copy (x2, x, n);   // side chain

            d.preAmp[ch]->setGain (gain);

            for (int i = 0; i < n; ++i)
            {
                x[i] = d.preAmp[ch]->processSample (x[i]);
                x1[i] = d.preAmp[ch]->getFF1();
            }

            d.amp[ch].setGain (gain);
            d.amp[ch].processBlock (x, n);
            juce::FloatVectorOperations::clip (x, x, -4.5f, 4.5f, n);
        }

        juce::dsp::AudioBlock<float> block (buffer);
        auto osBlock = d.os.processSamplesUp (block);

        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto* x = osBlock.getChannelPointer ((size_t) ch);

            for (size_t i = 0; i < osBlock.getNumSamples(); ++i)
                x[i] = d.clip[ch]->processSample (x[i]);
        }

        d.os.processSamplesDown (block);

        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto* x = buffer.getWritePointer (ch);
            auto* x1 = d.ff1Buff.getWritePointer (ch);
            auto* x2 = d.ff2Buff.getWritePointer (ch);

            d.ff2[ch]->setGain (gain);

            for (int i = 0; i < n; ++i)
                x2[i] = d.ff2[ch]->processSample (x2[i]);

            juce::FloatVectorOperations::add (x, x1, n);
            juce::FloatVectorOperations::add (x, x2, n);
            d.sumAmp[ch].processBlock (x, n);
            juce::FloatVectorOperations::clip (x, x, -13.1f, 11.7f, n);
        }

        // ChowCentaur::processInternalBuffer, last part: tone and output.
        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto* x = buffer.getWritePointer (ch);

            d.tone[ch].setTreble (treble);
            d.tone[ch].processBlock (x, n);

            juce::FloatVectorOperations::multiply (x, -1.0f, n);             // inverting amplifier
            juce::FloatVectorOperations::clip (x, x, -13.1f, 11.7f, n);      // op-amp clipping

            d.outProc[ch].setLevel (level);
            d.outProc[ch].processBlock (x, n);

            // ChowCentaur::processAudioBlock's DC blocker.
            for (int i = 0; i < n; ++i)
                x[i] = d.dcBlocker[ch].processSample (x[i]);
        }
    }
}

} // namespace ikaros
