#pragma once

#include <juce_core/juce_core.h>
#include <map>
#include <vector>

// Forward declaration only, so this header stays Tracktion-free (see AudioEngine.h).
namespace tracktion { inline namespace engine { class WaveAudioClip; } }

namespace ikaros
{

//==============================================================================
/**
    Waveform overviews for audio regions, cached per file and clip range so
    audio files are read once, not on every refresh.
*/
class WaveformCache
{
public:
    /** The loudest sample (0..1) in each of `numColumns` slices of the part
        of the file the clip plays. */
    std::vector<float> computePeaks (tracktion::WaveAudioClip&, int numColumns);

    /** Forgets everything (e.g. when the session is replaced). */
    void clear()    { peakCache.clear(); }

private:
    // Waveform peaks per recorded file and clip range, so audio files are
    // read once, not on every refresh.
    std::map<juce::String, std::vector<float>> peakCache;
};

} // namespace ikaros
