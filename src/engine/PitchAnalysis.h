#pragma once

// The pitch analysis behind the audio editor's Pitch mode: what was sung
// (or played) in a recording, frame by frame, and the notes in it.
// Tracktion-free, so it runs on a background thread and the tests can call
// it directly. The pitch of each frame comes from probabilistic YIN
// (external/pitch-detection).

#include <juce_audio_formats/juce_audio_formats.h>
#include <atomic>
#include <optional>
#include <vector>

namespace ikaros
{

//==============================================================================
/** One sung note, as found in the recording. */
struct PitchNote
{
    int firstFrame = 0, numFrames = 0;   // its frames in PitchAnalysis::pitch
    double start = 0.0, end = 0.0;       // seconds into the recording
    float centre = 0.0f;                 // the pitch it's sung at: the median of its
                                         // stable middle, as a fractional MIDI note
    float vibratoCents = 0.0f;           // how far it wavers either side of that (the vibrato's depth)

    // Room for splitting and joining notes later: a note made by hand would
    // carry its own boundaries here instead of the analysis's.
};

//==============================================================================
/** A recording's analysis. Frames are `hop` samples apart, each frameSize long. */
struct PitchAnalysis
{
    double sampleRate = 0.0;
    int frameSize = 2048, hop = 256;

    std::vector<float> pitch;     // per frame: a fractional MIDI note, 0 where nothing's pitched
    std::vector<float> voicing;   // per frame: how likely it's pitched, 0..1
    std::vector<PitchNote> notes; // in time order

    // False if the recording is mostly unpitched or sounds like chords: then
    // it isn't a single voice or instrument, and there are no notes.
    bool singleVoice = true;

    /** The middle of frame i, in seconds into the recording. */
    double frameTime (int i) const noexcept    { return (i * (double) hop + frameSize * 0.5) / sampleRate; }

    /** The frame whose middle is nearest a time. */
    int frameAt (double seconds) const noexcept
    {
        return juce::jlimit (0, juce::jmax (0, (int) pitch.size() - 1),
                             juce::roundToInt ((seconds * sampleRate - frameSize * 0.5) / hop));
    }

    double frameSeconds() const noexcept       { return hop / sampleRate; }
};

namespace pitchanalysis
{
    /** The pitch range looked in (Hz): low male voices to high sopranos and most instruments' melodies. */
    inline constexpr float minHz = 60.0f, maxHz = 1000.0f;

    /** Frame and hop sizes for a sample rate: about 2048 and 256 at 44.1 or 48 kHz, scaled with the rate. */
    int frameSizeFor (double sampleRate);

    /** Analyses a recording (its channels mixed to one). Slow: call it on a
        background thread. `progress` (0..1) is written as it goes, and it
        stops early, returning nothing, when `cancel` becomes true. */
    std::optional<PitchAnalysis> analyse (juce::AudioFormatReader&, std::atomic<float>* progress = nullptr,
                                          const std::atomic<bool>* cancel = nullptr);

    /** The same, for samples already in memory (one channel). */
    std::optional<PitchAnalysis> analyse (const float* samples, int numSamples, double sampleRate,
                                          std::atomic<float>* progress = nullptr, const std::atomic<bool>* cancel = nullptr);

    /** Finds the notes in frames already analysed (pitch, voicing), and
        decides whether it's a single voice. Fills in analysis.notes. */
    void findNotes (PitchAnalysis&);

    //==============================================================================
    // The cache: a small binary file per analysed recording (in the
    // session's Audio folder, see PitchEditor::cacheFileFor), so a session
    // never analyses a recording twice.

    /** A fingerprint of a file's contents (its size and parts of its data),
        to tell whether a cached analysis is still for the same audio. */
    juce::uint64 fingerprintOf (const juce::File&);

    bool writeCache (const juce::File& cacheFile, const PitchAnalysis&, juce::uint64 fingerprint);

    /** The cached analysis, if the file is there and has this fingerprint. */
    std::optional<PitchAnalysis> readCache (const juce::File& cacheFile, juce::uint64 fingerprint);
}

} // namespace ikaros
