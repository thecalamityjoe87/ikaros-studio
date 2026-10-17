#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <vector>

// Forward declaration only, so this header stays Tracktion-free (see AudioEngine.h).
namespace tracktion { inline namespace engine { class WaveAudioClip; } }

namespace ikaros
{

/** The quietest and loudest sample (-1..1) in a slice of audio. */
struct WaveformRange
{
    float min = 0.0f, max = 0.0f;

    bool operator== (const WaveformRange&) const = default;
};

//==============================================================================
/**
    Waveforms of audio files, read once and kept, so they aren't read again
    on every refresh.

    Two kinds:
      - computePeaks: a small overview of a region (one value per column) for
        the arrangement, read straight away.
      - getOverview: a detailed overview of a whole file for the audio
        editor, which zooms right in. Reading a long recording takes a while,
        so that happens on a background thread: the first call starts it and
        returns nothing, and later calls return the finished overview.
*/
class WaveformCache
{
public:
    WaveformCache();
    ~WaveformCache();

    /** The loudest sample (0..1) in each of `numColumns` slices of the part
        of the file the clip plays. */
    std::vector<float> computePeaks (tracktion::WaveAudioClip&, int numColumns);

    //==============================================================================
    /** A whole file's waveform: the quietest and loudest sample (all channels
        together) in every `samplesPerBin` samples. Never changes once made, so
        the message thread can read it while the background thread makes others. */
    struct Overview
    {
        static constexpr int samplesPerBin = 128;   // about 3 ms at 44.1 kHz

        double sampleRate = 0.0;
        juce::int64 lengthInSamples = 0;
        std::vector<WaveformRange> bins;

        double getLengthInSeconds() const   { return sampleRate > 0.0 ? (double) lengthInSamples / sampleRate : 0.0; }

        /** The quietest and loudest sample between two times in the file
            (silence outside it). */
        WaveformRange getRange (double fromSeconds, double toSeconds) const;
    };

    /** The overview of a file, or nullptr while it's still being read (in
        the background). Call again later, e.g. from a timer. The formats are
        used on the background thread, so they must outlive this cache. */
    std::shared_ptr<const Overview> getOverview (const juce::File&, juce::AudioFormatManager&);

    /** Forgets everything (e.g. when the session is replaced). */
    void clear();

private:
    class OverviewJob;

    // Waveform peaks per recorded file and clip range, so audio files are
    // read once, not on every refresh. Message thread only.
    std::map<juce::String, std::vector<float>> peakCache;

    // Finished overviews, and the files being read now, by file key (see
    // keyFor). The background thread adds to `overviews` when it finishes,
    // so both are only touched with `lock` held.
    std::mutex lock;
    std::map<juce::String, std::shared_ptr<const Overview>> overviews;
    std::set<juce::String> reading;

    // One background thread reads the files, one after another.
    juce::ThreadPool pool { juce::ThreadPoolOptions().withThreadName ("Waveforms").withNumberOfThreads (1) };

    static juce::String keyFor (const juce::File&);
    void overviewFinished (const juce::String& key, std::shared_ptr<const Overview>);

    JUCE_DECLARE_NON_COPYABLE (WaveformCache)
};

} // namespace ikaros
