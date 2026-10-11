#include "engine/WaveformCache.h"

#include <tracktion_engine/tracktion_engine.h>

namespace te = tracktion;

namespace ikaros
{

//==============================================================================
// Waveform overview for an audio clip: the loudest sample in each of
// `numColumns` slices of the part of the file the clip plays.
std::vector<float> WaveformCache::computePeaks (te::WaveAudioClip& clip, int numColumns)
{
    auto file = clip.getAudioFile().getFile();
    auto pos = clip.getPosition();

    // The part of the file the region shows: from its offset for its length,
    // or one loop's worth if it's looped (the UI draws the repeats).
    const double sourceStart = clip.isLooping() ? clip.getLoopStart().inSeconds() : pos.offset.inSeconds();
    const double sourceLength = clip.isLooping() ? clip.getLoopLength().inSeconds() : pos.getLength().inSeconds();

    auto key = file.getFullPathName() + ":" + juce::String (file.getSize())
             + ":" + juce::String (sourceStart) + ":" + juce::String (sourceLength);

    if (auto it = peakCache.find (key); it != peakCache.end())
        return it->second;

    std::vector<float> peaks;
    auto& formats = clip.edit.engine.getAudioFileFormatManager().readFormatManager;

    // unique_ptr: the reader is deleted automatically when we're done.
    if (std::unique_ptr<juce::AudioFormatReader> reader { formats.createReaderFor (file) })
    {
        const auto start = (juce::int64) (sourceStart * reader->sampleRate);
        const auto length = (juce::int64) (sourceLength * reader->sampleRate);
        const auto step = juce::jmax ((juce::int64) 1, length / numColumns);
        std::vector<juce::Range<float>> levels ((size_t) reader->numChannels);

        for (int i = 0; i < numColumns; ++i)
        {
            reader->readMaxLevels (start + i * step, step, levels.data(), (int) reader->numChannels);
            float peak = 0.0f;

            for (auto& r : levels)
                peak = juce::jmax (peak, std::abs (r.getStart()), std::abs (r.getEnd()));

            peaks.push_back (juce::jmin (1.0f, peak));
        }

        // Don't cache a file that's still being written.
        if (! clip.edit.getTransport().isRecording())
            peakCache[key] = peaks;
    }

    return peaks;
}

} // namespace ikaros
