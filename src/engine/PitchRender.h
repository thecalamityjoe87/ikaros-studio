#pragma once

// Rendering pitch corrections: a recording, moved in pitch along a
// correction curve (see PitchCurve.h), written to a new audio file the same
// length as the recording, so a region can play it in the recording's place.
// Tracktion-free; runs on a background thread.

#include "engine/PitchAnalysis.h"
#include <functional>

namespace ikaros
{

struct PitchRenderRequest
{
    juce::File original;      // the recording as it was made (always the input)
    juce::File output;        // the file to write (32-bit float WAV)

    // A render made earlier with another curve, and that curve: only where
    // the curves differ is rendered again; the rest is copied from it. With
    // no earlier render, the original (whose curve is all zeros) is used.
    juce::File previous;
    std::vector<float> previousCurve;

    PitchAnalysis analysis;   // the recording's frames (for times)
    std::vector<float> curve; // semitones per frame (PitchCurve's offsets)

    // Opens an audio file (the app passes Tracktion's formats).
    std::function<std::unique_ptr<juce::AudioFormatReader> (const juce::File&)> openReader;
};

namespace pitchrender
{
    /** The parts of the recording that need rendering again, as sample
        ranges: where the curves differ, widened to the nearest stretch
        where both leave the sound alone (so the new audio can be crossfaded
        in where both versions are the original). */
    std::vector<juce::Range<juce::int64>> changedRanges (const PitchAnalysis&, const std::vector<float>& previousCurve,
                                                         const std::vector<float>& curve, juce::int64 lengthInSamples);

    /** Renders with Rubber Band (its R3 engine, keeping the voice's formants)
        fed block by block, each block at its own pitch ratio, at the same
        speed. `progress` goes 0..1; it stops (returning false, and deleting
        the half-written file) when `cancel` becomes true. */
    bool render (const PitchRenderRequest&, std::atomic<float>* progress = nullptr, const std::atomic<bool>* cancel = nullptr);
}

} // namespace ikaros
