#include "engine/WaveformCache.h"
#include "engine/FlexHelpers.h"

#include <tracktion_engine/tracktion_engine.h>

namespace te = tracktion;

namespace ikaros
{

//==============================================================================
WaveformCache::WaveformCache() = default;

WaveformCache::~WaveformCache()
{
    // The jobs refer back to this cache, so they must be finished (or
    // stopped: they check shouldExit() as they go) before it goes away.
    pool.removeAllJobs (true, 10000);
}

void WaveformCache::clear()
{
    peakCache.clear();
    pool.removeAllJobs (true, 10000);

    std::lock_guard<std::mutex> guard (lock);
    overviews.clear();
    reading.clear();
}

//==============================================================================
// Waveform overview for an audio clip: the loudest sample in each of
// `numColumns` slices of the part of the file the clip plays.
std::vector<float> WaveformCache::computePeaks (te::WaveAudioClip& clip)
{
    // A reversed region plays a reversed copy of its file, which Tracktion
    // makes in the background. Rather than wait for it, read the original
    // and turn the result round: the reversed copy's first second is the
    // original's last. A flexed region likewise plays a stretched copy:
    // read the original through its warp markers instead.
    const bool reversed = clip.getIsReversed();
    const auto warp = detail::usesFlex (clip) ? detail::getWarpPoints (clip) : std::vector<WarpPoint>();
    auto file = reversed || ! warp.empty() ? clip.getOriginalFile() : clip.getAudioFile().getFile();
    auto pos = clip.getPosition();

    // The part of the content the region shows: from its offset for its
    // length, or one loop's worth if it's looped (the UI draws the repeats).
    double contentStart = clip.isLooping() ? clip.getLoopStart().inSeconds() : pos.offset.inSeconds();
    const double contentLength = clip.isLooping() ? clip.getLoopLength().inSeconds() : pos.getLength().inSeconds();
    double sourceStart = contentStart;
    const int numColumns = juce::jlimit (minPeaks, maxPeaks, juce::roundToInt (contentLength / secondsPerPeak));

    if (reversed)
        sourceStart = te::AudioFile (clip.edit.engine, file).getLength() - contentStart - contentLength;

    auto key = file.getFullPathName() + ":" + juce::String (file.getSize())
             + ":" + juce::String (sourceStart) + ":" + juce::String (contentLength) + (reversed ? ":r" : "");

    for (auto& m : warp)
        key << ":" << m.source << "/" << m.warp;

    if (auto it = peakCache.find (key); it != peakCache.end())
        return it->second;

    std::vector<float> peaks;
    auto& formats = clip.edit.engine.getAudioFileFormatManager().readFormatManager;

    // unique_ptr: the reader is deleted automatically when we're done.
    if (std::unique_ptr<juce::AudioFormatReader> reader { formats.createReaderFor (file) })
    {
        const auto rate = reader->sampleRate;
        std::vector<juce::Range<float>> levels ((size_t) reader->numChannels);

        // Where a moment of the content is in the file, in samples.
        auto sampleAt = [&] (double contentSeconds)
        {
            const double seconds = warp.empty() ? sourceStart + (contentSeconds - contentStart)
                                                : flexmap::toSource (warp, contentSeconds);
            return (juce::int64) (seconds * rate);
        };

        for (int i = 0; i < numColumns; ++i)
        {
            const auto from = sampleAt (contentStart + contentLength * i / numColumns);
            const auto to = sampleAt (contentStart + contentLength * (i + 1) / numColumns);
            reader->readMaxLevels (from, juce::jmax ((juce::int64) 1, to - from), levels.data(), (int) reader->numChannels);
            float peak = 0.0f;

            for (auto& r : levels)
                peak = juce::jmax (peak, std::abs (r.getStart()), std::abs (r.getEnd()));

            peaks.push_back (juce::jmin (1.0f, peak));
        }

        if (reversed)
            std::reverse (peaks.begin(), peaks.end());

        // Don't cache a file that's still being written.
        if (! clip.edit.getTransport().isRecording())
            peakCache[key] = peaks;
    }

    return peaks;
}

//==============================================================================
// Detailed overviews, made on the background thread
//==============================================================================

/** Reads one file into an Overview. A juce::ThreadPoolJob's runJob() runs on
    one of the pool's threads, so it must not touch anything the message
    thread uses, except through WaveformCache::overviewFinished (which locks). */
class WaveformCache::OverviewJob  : public juce::ThreadPoolJob
{
public:
    OverviewJob (WaveformCache& c, juce::String k, juce::File f, juce::AudioFormatManager& fm)
        : juce::ThreadPoolJob ("Waveform " + f.getFileName()),
          cache (c), key (std::move (k)), file (std::move (f)), formats (fm)
    {
    }

    JobStatus runJob() override
    {
        auto overview = std::make_shared<Overview>();

        if (std::unique_ptr<juce::AudioFormatReader> reader { formats.createReaderFor (file) })
        {
            overview->sampleRate = reader->sampleRate;
            overview->lengthInSamples = reader->lengthInSamples;

            constexpr int binsPerChunk = 512;
            constexpr int chunk = Overview::samplesPerBin * binsPerChunk;
            const int channels = juce::jmax (1, (int) reader->numChannels);
            juce::AudioBuffer<float> buffer (channels, chunk);
            overview->bins.reserve ((size_t) (reader->lengthInSamples / Overview::samplesPerBin + 1));

            for (juce::int64 pos = 0; pos < reader->lengthInSamples; pos += chunk)
            {
                // clear() or the cache's destructor wants this thread back:
                // stop, and keep nothing (a half-read overview would be wrong).
                if (shouldExit())
                    return jobHasFinished;

                const int num = (int) juce::jmin ((juce::int64) chunk, reader->lengthInSamples - pos);
                reader->read (&buffer, 0, num, pos, true, true);

                for (int binStart = 0; binStart < num; binStart += Overview::samplesPerBin)
                {
                    const int binLength = juce::jmin (Overview::samplesPerBin, num - binStart);
                    WaveformRange bin;

                    for (int ch = 0; ch < channels; ++ch)
                    {
                        // findMinAndMax: the lowest and highest value in a block (vectorised by JUCE).
                        auto range = juce::FloatVectorOperations::findMinAndMax (buffer.getReadPointer (ch, binStart), binLength);
                        bin.min = juce::jmin (bin.min, range.getStart());
                        bin.max = juce::jmax (bin.max, range.getEnd());
                    }

                    overview->bins.push_back (bin);
                }
            }
        }

        // An unreadable file gets an empty overview, so it isn't tried again and again.
        cache.overviewFinished (key, std::move (overview));
        return jobHasFinished;
    }

private:
    WaveformCache& cache;
    const juce::String key;
    const juce::File file;
    juce::AudioFormatManager& formats;
};

juce::String WaveformCache::keyFor (const juce::File& file)
{
    // A file that's re-recorded or rewritten gets a new overview.
    return file.getFullPathName() + ":" + juce::String (file.getSize())
             + ":" + juce::String (file.getLastModificationTime().toMilliseconds());
}

std::shared_ptr<const WaveformCache::Overview> WaveformCache::getOverview (const juce::File& file, juce::AudioFormatManager& formats)
{
    const auto key = keyFor (file);

    {
        std::lock_guard<std::mutex> guard (lock);

        if (auto it = overviews.find (key); it != overviews.end())
            return it->second;

        // insert().second is false if it was already there: already being read.
        if (! reading.insert (key).second)
            return nullptr;
    }

    // true = the pool deletes the job when it's done.
    pool.addJob (new OverviewJob (*this, key, file, formats), true);
    return nullptr;
}

void WaveformCache::overviewFinished (const juce::String& key, std::shared_ptr<const Overview> overview)
{
    std::lock_guard<std::mutex> guard (lock);

    // Forgotten in the meantime (clear()): keep nothing.
    if (reading.erase (key) > 0)
        overviews[key] = std::move (overview);
}

WaveformRange WaveformCache::Overview::getRange (double fromSeconds, double toSeconds) const
{
    if (bins.empty() || sampleRate <= 0.0)
        return {};

    const double binsPerSecond = sampleRate / samplesPerBin;
    const auto numBins = (juce::int64) bins.size();
    auto first = (juce::int64) std::floor (fromSeconds * binsPerSecond);
    auto last = juce::jmax (first + 1, (juce::int64) std::ceil (toSeconds * binsPerSecond));   // at least one bin
    first = juce::jlimit ((juce::int64) 0, numBins, first);
    last = juce::jlimit ((juce::int64) 0, numBins, last);

    if (first >= last)
        return {};   // outside the file: silence

    WaveformRange r { bins[(size_t) first].min, bins[(size_t) first].max };

    for (auto i = first + 1; i < last; ++i)
    {
        r.min = juce::jmin (r.min, bins[(size_t) i].min);
        r.max = juce::jmax (r.max, bins[(size_t) i].max);
    }

    return r;
}

} // namespace ikaros
