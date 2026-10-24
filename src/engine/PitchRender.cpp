#include "engine/PitchRender.h"
#include "engine/PitchCurve.h"
#include <rubberband/rubberband/RubberBandStretcher.h>   // built into the app through Tracktion (see CMakeLists.txt)

namespace ikaros::pitchrender
{

namespace
{
    // The new audio is crossfaded in over this long, inside a stretch the
    // corrections leave alone (at least neutralSeconds long).
    constexpr double crossfadeSeconds = 0.01, neutralSeconds = 0.03;

    // Rubber Band is fed this much before and after a range, so it has
    // settled by the time the audio that's kept comes out.
    constexpr double prerollSeconds = 0.1;
}

std::vector<juce::Range<juce::int64>> changedRanges (const PitchAnalysis& a, const std::vector<float>& previousCurve,
                                                     const std::vector<float>& curve, juce::int64 length)
{
    const int frames = (int) curve.size();
    auto previous = [&] (int i) { return i < (int) previousCurve.size() ? previousCurve[(size_t) i] : 0.0f; };
    auto neutral = [&] (int i) { return curve[(size_t) i] == 0.0f && previous (i) == 0.0f; };
    auto differs = [&] (int i) { return std::abs (curve[(size_t) i] - previous (i)) > 1.0e-5f; };

    // Cut the recording into zones at every stretch the corrections leave
    // alone. A zone where anything changed is rendered, reaching only a
    // little way into the stretches either side (so a breath or a consonant
    // nearby is still copied exactly), or to their middle if they're short.
    const int neutralFrames = juce::jmax (2, (int) std::ceil (neutralSeconds / a.frameSeconds()));
    const int margin = neutralFrames / 2;
    auto sampleOf = [&] (int frame) { return juce::jlimit ((juce::int64) 0, length, (juce::int64) frame * a.hop + a.frameSize / 2); };

    std::vector<juce::Range<juce::int64>> ranges;
    juce::int64 zoneStart = 0;
    bool zoneChanged = false;

    auto closeZone = [&] (juce::int64 end, juce::int64 nextStart)
    {
        if (zoneChanged && end > zoneStart)
        {
            if (! ranges.empty() && ranges.back().getEnd() >= zoneStart)
                ranges.back().setEnd (end);   // touching the last one: one range
            else
                ranges.push_back ({ zoneStart, end });
        }

        zoneStart = nextStart;
        zoneChanged = false;
    };

    for (int i = 0; i < frames;)
    {
        if (! neutral (i))
        {
            zoneChanged = zoneChanged || differs (i);
            ++i;
            continue;
        }

        int end = i;

        while (end < frames && neutral (end))
            ++end;

        if (end - i >= neutralFrames)
        {
            const int middle = (i + end) / 2;
            closeZone (sampleOf (juce::jmin (i + margin, middle)), sampleOf (juce::jmax (end - margin, middle)));
        }

        i = end;
    }

    closeZone (length, length);
    return ranges;
}

//==============================================================================
bool render (const PitchRenderRequest& request, std::atomic<float>* progress, const std::atomic<bool>* cancel)
{
    auto original = request.openReader (request.original);

    if (original == nullptr || original->lengthInSamples <= 0)
        return false;

    const auto length = original->lengthInSamples;
    const auto rate = original->sampleRate;
    const int channels = (int) juce::jmax (1u, original->numChannels);

    // What's copied where nothing changed: the earlier render, if it's for
    // this recording, or the recording itself.
    std::unique_ptr<juce::AudioFormatReader> previous;
    std::vector<float> previousCurve;

    if (request.previous.existsAsFile())
    {
        previous = request.openReader (request.previous);

        if (previous != nullptr && previous->lengthInSamples == length && (int) previous->numChannels == channels)
            previousCurve = request.previousCurve;
        else
            previous.reset();
    }

    auto& base = previous != nullptr ? *previous : *original;
    const auto ranges = changedRanges (request.analysis, previousCurve, request.curve, length);

    // Written to a temporary file beside the output, which replaces it only once it's complete.
    juce::TemporaryFile temp (request.output);
    std::unique_ptr<juce::AudioFormatWriter> writer;

    {
        // On success the writer takes the stream (and `out` becomes nullptr).
        std::unique_ptr<juce::OutputStream> out = temp.getFile().createOutputStream();

        // 32-bit float: copied parts keep the recording's samples exactly.
        if (out != nullptr)
            writer = juce::WavAudioFormat().createWriterFor (out, juce::AudioFormatWriterOptions()
                                                                      .withSampleRate (rate)
                                                                      .withNumChannels (channels)
                                                                      .withBitsPerSample (32)
                                                                      .withSampleFormat (juce::AudioFormatWriterOptions::SampleFormat::floatingPoint));
        if (writer == nullptr)
            return false;
    }

    juce::int64 toRender = 0;

    for (auto& r : ranges)
        toRender += r.getLength();

    juce::int64 rendered = 0;
    constexpr int blockSize = 8192;
    juce::AudioBuffer<float> block (channels, blockSize);

    auto stopped = [cancel] { return cancel != nullptr && cancel->load(); };

    auto copyBase = [&] (juce::int64 from, juce::int64 to)
    {
        for (auto pos = from; pos < to; pos += blockSize)
        {
            if (stopped())
                return false;

            const int num = (int) juce::jmin ((juce::int64) blockSize, to - pos);
            base.read (&block, 0, num, pos, true, true);

            if (! writer->writeFromAudioSampleBuffer (block, 0, num))
                return false;
        }

        return true;
    };

    // One range, through Rubber Band. Realtime mode is what lets the pitch
    // change from block to block (the offline mode fixes it up front); R3
    // ("finer") is its high-quality engine.
    auto renderRange = [&] (juce::Range<juce::int64> range)
    {
        using RB = RubberBand::RubberBandStretcher;
        const int hop = request.analysis.hop;
        RB stretcher ((size_t) std::lround (rate), (size_t) channels,
                      RB::OptionProcessRealTime | RB::OptionEngineFiner | RB::OptionFormantPreserved
                        | RB::OptionChannelsTogether,
                      1.0, 1.0);
        stretcher.setMaxProcessSize ((size_t) hop);

        const auto preroll = (juce::int64) (prerollSeconds * rate);
        const auto inStart = juce::jmax ((juce::int64) 0, range.getStart() - preroll);
        const auto inEnd = juce::jmin (length, range.getEnd() + preroll);

        juce::AudioBuffer<float> in (channels, hop), out (channels, hop * 4);
        juce::AudioBuffer<float> result (channels, (int) range.getLength());
        result.clear();

        // Rubber Band wants some silence first, and its first few output
        // samples are its delay: dropping those lines the output up with the input.
        auto toDrop = (juce::int64) stretcher.getStartDelay();
        juce::int64 outPos = inStart;   // which input sample the next output sample belongs to

        auto collect = [&]
        {
            for (int got; (got = stretcher.available()) > 0;)
            {
                got = juce::jmin (got, out.getNumSamples());
                stretcher.retrieve (out.getArrayOfWritePointers(), (size_t) got);

                int from = 0;

                if (toDrop > 0)
                {
                    from = (int) juce::jmin ((juce::int64) got, toDrop);
                    toDrop -= from;
                }

                for (int s = from; s < got; ++s, ++outPos)
                    if (range.contains (outPos))
                        for (int ch = 0; ch < channels; ++ch)
                            result.setSample (ch, (int) (outPos - range.getStart()), out.getSample (ch, s));
            }
        };

        {
            in.clear();
            auto pad = (int) stretcher.getPreferredStartPad();

            while (pad > 0)
            {
                const int num = juce::jmin (pad, hop);
                stretcher.process (in.getArrayOfReadPointers(), (size_t) num, false);
                pad -= num;
                collect();
            }
        }

        for (auto pos = inStart; pos < inEnd; pos += hop)
        {
            if (stopped())
                return false;

            const int num = (int) juce::jmin ((juce::int64) hop, inEnd - pos);
            original->read (&in, 0, num, pos, true, true);

            // The pitch for this block: the curve at its middle.
            const auto middle = (double) (pos + num / 2) / rate;
            const auto semitones = pitchcurve::offsetAt (request.analysis, request.curve, middle);
            stretcher.setPitchScale (std::pow (2.0, semitones / 12.0));
            stretcher.process (in.getArrayOfReadPointers(), (size_t) num, pos + num >= inEnd);
            collect();

            if (progress != nullptr && toRender > 0)
                *progress = (float) (rendered + juce::jlimit ((juce::int64) 0, range.getLength(), pos - range.getStart())) / (float) toRender;
        }

        // Whatever's still inside it.
        for (int i = 0; i < 1000 && stretcher.available() >= 0 && outPos < range.getEnd(); ++i)
        {
            collect();

            if (stretcher.available() == 0)
                break;
        }

        // Fade between the copied audio and the new at both ends (both are
        // the untouched recording there, so nothing is heard but a seam
        // that isn't there).
        const int fade = (int) juce::jmin (range.getLength() / 2, (juce::int64) (crossfadeSeconds * rate));
        juce::AudioBuffer<float> edge (channels, juce::jmax (1, fade));

        for (bool atStart : { true, false })
        {
            if (fade <= 0)
                break;

            const auto at = atStart ? range.getStart() : range.getEnd() - fade;
            base.read (&edge, 0, fade, at, true, true);

            for (int s = 0; s < fade; ++s)
            {
                const float w = (float) (s + 0.5f) / (float) fade;           // 0 -> 1 across the fade
                const float rendered01 = atStart ? w : 1.0f - w;
                const int index = (int) (at - range.getStart()) + s;

                for (int ch = 0; ch < channels; ++ch)
                    result.setSample (ch, index, rendered01 * result.getSample (ch, index)
                                                   + (1.0f - rendered01) * edge.getSample (ch, s));
            }
        }

        rendered += range.getLength();
        return writer->writeFromAudioSampleBuffer (result, 0, result.getNumSamples());
    };

    juce::int64 pos = 0;

    for (auto& r : ranges)
    {
        if (! copyBase (pos, r.getStart()) || ! renderRange (r))
            return false;

        pos = r.getEnd();
    }

    if (! copyBase (pos, length))
        return false;

    writer.reset();   // finishes the WAV header and closes the file

    if (progress != nullptr)
        *progress = 1.0f;

    return temp.overwriteTargetFileWithTemporary();
}

} // namespace ikaros::pitchrender
