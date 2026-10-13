#pragma once

#include <juce_core/juce_core.h>
#include <atomic>
#include <functional>
#include <memory>
#include <vector>

namespace ikaros
{

class AudioEngine;

enum class ExportFormat { wav, flac, ogg, mp3 };

/** What File > Export Song makes. */
struct ExportSettings
{
    ExportFormat format = ExportFormat::wav;

    /** WAV and FLAC: the bit depth (16 or 24). OGG and MP3: the bit rate in kbps
        (see SongExporter::getQualityChoices). */
    int quality = 24;

    /** Turn the whole export up (or down) so its loudest moment sits just
        under full scale. Stems all get the same gain, so they still add up
        to the song. */
    bool normalize = false;

    /** Just the cycle range (exactly, no tail), instead of the whole song. */
    bool cycleOnly = false;

    /** One file per track instead of one mixed file. */
    bool stems = false;

    /** The file to write; with stems, the folder to put them in (made if
        needed). The right extension is added to files without one. */
    juce::File destination;
};

struct ExportResult
{
    bool ok = false;
    bool cancelled = false;
    juce::String error;
    juce::Array<juce::File> files;   // what was written
};

//==============================================================================
/**
    Renders the session to audio files, faster than real time, in the
    background (File > Export Song), as WAV, FLAC, OGG or MP3.

    Each file (the song, or one stem per track) is rendered offline by
    Tracktion into a temporary float WAV, playing everything the speakers
    would: automation, sends, plug-ins, master effects, mute and solo. A
    second pass then finds where the reverb/delay tail dies out, applies
    normalizing, and writes the final files, all trimmed to the same length.

    Start an export with start(), poll getProgress() (e.g. on a UI timer)
    and cancel() at any time; onFinished is called on the message thread.
    The session mustn't change while an export runs: the UI covers the
    window meanwhile (see ExportDialog). Call from the message thread.
*/
class SongExporter
{
public:
    explicit SongExporter (AudioEngine&);
    ~SongExporter();   // cancels a running export and waits for it

    /** "wav", "flac", "ogg" or "mp3" (without the dot). */
    static juce::String getFileExtension (ExportFormat);

    /** The quality settings a format offers (bit depths or kbps), best last. */
    static std::vector<int> getQualityChoices (ExportFormat);

    /** E.g. "24-bit" or "192 kbps". */
    static juce::String describeQuality (ExportFormat, int quality);

    /** Where the last region ends (0 when there are no regions). The whole
        song is exported from the start up to here, plus the tail. */
    double getSongEndBeats() const;

    /** Whether the cycle range is long enough to export. */
    bool hasCycleRange() const;

    /** The tracks a stems export makes files for: every track with regions
        that can be heard (not muted or silenced by another track's solo). */
    juce::StringArray getStemNames() const;

    /** Starts exporting. Fails (without calling onFinished) if an export is
        already running or there's nothing to export. */
    juce::Result start (const ExportSettings&, std::function<void (const ExportResult&)> onFinished);

    bool isRunning() const noexcept             { return job != nullptr; }

    /** 0..1, across every file. */
    float getProgress() const;

    /** Stops the export soon; onFinished then reports it as cancelled and
        nothing is left behind. */
    void cancel();

    /** Stops a running export right away, without calling onFinished (e.g.
        before the session is replaced). */
    void cancelAndWait();

    static constexpr double sampleRate = 44100.0;
    static constexpr double maxTailSeconds = 10.0;      // the longest reverb/delay tail kept
    static constexpr float normalizeLevelDb = -1.0f;    // headroom for lossy encoders

private:
    struct Job;

    AudioEngine& engine;

    // shared_ptr so callbacks from the render thread can hold a weak_ptr and
    // notice when the job they belong to has gone (see renderNext).
    std::shared_ptr<Job> job;

    void renderNext();
    void renderFinished (bool ok, const juce::String& error);
    void startEncoding();
    void finish (ExportResult);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SongExporter)
};

} // namespace ikaros
