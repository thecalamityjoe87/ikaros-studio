#include "engine/SongExporter.h"
#include "engine/AudioEngine.h"
#include "engine/Mp3Format.h"
#include "engine/TrackController.h"
#include "engine/TrackHelpers.h"
#include "platform/AppFolders.h"
#include <tracktion_engine/tracktion_engine.h>
#include <thread>

namespace ikaros
{

using namespace detail;   // the shared engine helpers (TrackHelpers.h)

namespace
{
    // Quieter than this counts as silence when looking for the end of the tail.
    const float silenceLevel = juce::Decibels::decibelsToGain (-80.0f);

    // Kept after the last sound, so a fading tail is never cut off abruptly.
    constexpr double tailMarginSeconds = 0.1;

    constexpr int blockSize = 32768;   // samples read and written at a time when encoding

    // Rendering is most of the work; encoding all the files is worth about one more.
    constexpr float encodeShare = 1.0f;

    std::unique_ptr<juce::AudioFormat> createFormat (ExportFormat f)
    {
        switch (f)
        {
            case ExportFormat::flac:  return std::make_unique<juce::FlacAudioFormat>();
            case ExportFormat::ogg:   return std::make_unique<juce::OggVorbisAudioFormat>();
            case ExportFormat::mp3:   return std::make_unique<Mp3AudioFormat>();
            case ExportFormat::wav:   break;
        }

        return std::make_unique<juce::WavAudioFormat>();
    }

    // The tracks the user can hear right now (mute and solo, like the mixer shows).
    juce::Array<te::AudioTrack*> getAudibleTracks (te::Edit& edit)
    {
        auto tracks = getUserTracks (edit);
        const bool anySolo = std::any_of (tracks.begin(), tracks.end(), [] (auto* t) { return t->isSolo (false); });
        tracks.removeIf ([anySolo] (te::AudioTrack* t) { return t->isMuted (false) || (anySolo && ! t->isSolo (false)); });
        return tracks;
    }

    te::TimePosition getSongEnd (te::Edit& edit)
    {
        te::TimePosition end;

        for (auto* t : getUserTracks (edit))
            for (auto* clip : t->getClips())
                end = std::max (end, clip->getPosition().getEnd());

        return end;
    }
}

//==============================================================================
// One file to make: which tracks it renders, where the render goes first and
// where the finished file ends up.
struct ExportTarget
{
    juce::BigInteger tracks;   // indexes into te::getAllTracks
    juce::File rendered;       // the float WAV Tracktion writes
    juce::File encoded;        // the finished file, still in the temporary folder
    juce::File destination;    // where it's moved when everything has worked
};

struct SongExporter::Job
{
    ExportSettings settings;
    std::function<void (const ExportResult&)> onFinished;

    te::TimeRange range;              // what's rendered (the whole song includes room for the tail)
    juce::int64 minLength = 0;        // samples every file keeps, tail or not (the song itself)
    std::vector<ExportTarget> targets;
    juce::File tempFolder;

    size_t current = 0;               // the target being rendered
    std::shared_ptr<te::EditRenderer::Handle> render;

    std::thread encoder;
    std::atomic<float> encodeProgress { 0.0f };
    std::atomic<bool> cancelled { false };
    juce::String encodeError;

    ~Job()
    {
        // Destroying the handle cancels its render and waits for its thread.
        if (render != nullptr)
            render->cancel();

        render.reset();

        if (encoder.joinable())
            encoder.join();

        tempFolder.deleteRecursively();
    }

    // Runs on the encoder thread: finds the end of the tail and the loudest
    // sample across every render, then writes the finished files. Returns
    // false if cancelled or something failed (see encodeError).
    bool encode()
    {
        juce::WavAudioFormat wav;
        std::vector<std::unique_ptr<juce::AudioFormatReader>> readers;

        for (auto& t : targets)
        {
            // createReaderFor takes ownership of the stream if it succeeds.
            auto* reader = wav.createReaderFor (t.rendered.createInputStream().release(), true);

            if (reader == nullptr)
            {
                encodeError = "Couldn't read back the rendered audio.";
                return false;
            }

            readers.emplace_back (reader);
        }

        juce::AudioBuffer<float> buffer (2, blockSize);
        float peak = 0.0f;
        juce::int64 lastSound = 0, renderedLength = 0;
        juce::int64 totalWork = 0, workDone = 0;

        for (auto& r : readers)
            totalWork += r->lengthInSamples * 2;   // read once to measure, once to write

        // Pass 1: measure.
        for (auto& reader : readers)
        {
            renderedLength = std::max (renderedLength, reader->lengthInSamples);

            for (juce::int64 pos = 0; pos < reader->lengthInSamples; pos += blockSize)
            {
                if (cancelled)
                    return false;

                const auto num = (int) std::min ((juce::int64) blockSize, reader->lengthInSamples - pos);
                reader->read (&buffer, 0, num, pos, true, true);
                const auto blockPeak = buffer.getMagnitude (0, num);
                peak = std::max (peak, blockPeak);

                // Only a block with sound in it can move the end of the tail.
                if (blockPeak > silenceLevel)
                    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                        for (int i = num; --i >= 0;)
                            if (std::abs (buffer.getSample (ch, i)) > silenceLevel)
                            {
                                lastSound = std::max (lastSound, pos + i + 1);
                                break;
                            }

                workDone += num;
                encodeProgress = (float) workDone / (float) totalWork;
            }
        }

        // Every file gets the same length: the song, plus the longest tail.
        auto length = settings.cycleOnly ? renderedLength
                                         : lastSound + (juce::int64) (tailMarginSeconds * sampleRate);
        length = juce::jlimit (std::min (minLength, renderedLength), renderedLength, length);

        const float gain = settings.normalize && peak > 0.0f
                               ? juce::Decibels::decibelsToGain (normalizeLevelDb) / peak
                               : 1.0f;

        // Pass 2: write the finished files.
        auto format = createFormat (settings.format);
        const bool lossy = settings.format == ExportFormat::ogg || settings.format == ExportFormat::mp3;
        const int bits = lossy ? 16 : settings.quality;
        int qualityIndex = 0;

        if (lossy)
            qualityIndex = juce::jmax (0, format->getQualityOptions().indexOf (juce::String (settings.quality) + " kbps"));

        juce::Random random;

        for (size_t i = 0; i < targets.size(); ++i)
        {
            auto& reader = *readers[i];
            std::unique_ptr<juce::OutputStream> out = targets[i].encoded.createOutputStream();

            // On success the writer takes the stream (and out becomes nullptr).
            auto writer = out == nullptr ? nullptr
                                         : format->createWriterFor (out, juce::AudioFormatWriterOptions()
                                                                             .withSampleRate (sampleRate)
                                                                             .withNumChannels (2)
                                                                             .withBitsPerSample (bits)
                                                                             .withQualityOptionIndex (qualityIndex));
            if (writer == nullptr)
            {
                encodeError = "Couldn't write " + targets[i].destination.getFileName() + ".";
                return false;
            }

            for (juce::int64 pos = 0; pos < length; pos += blockSize)
            {
                if (cancelled)
                    return false;

                const auto num = (int) std::min ((juce::int64) blockSize, length - pos);
                buffer.clear();   // a stem's render may be a little shorter than the others
                reader.read (&buffer, 0, num, pos, true, true);
                buffer.applyGain (0, num, gain);

                // 16-bit: a little random noise (TPDF dither) stops quiet
                // fades from turning into gritty steps. Pure digital silence
                // is left silent.
                if (bits == 16 && ! lossy && buffer.getMagnitude (0, num) > 0.0f)
                    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                        for (int s = 0; s < num; ++s)
                            buffer.addSample (ch, s, (random.nextFloat() - random.nextFloat()) / 32768.0f);

                if (! writer->writeFromAudioSampleBuffer (buffer, 0, num))
                {
                    encodeError = "Couldn't write " + targets[i].destination.getFileName() + ". Is the disk full?";
                    return false;
                }

                workDone += num;
                encodeProgress = (float) workDone / (float) totalWork;
            }
        }

        return true;
    }
};

//==============================================================================
SongExporter::SongExporter (AudioEngine& e) : engine (e) {}

SongExporter::~SongExporter()
{
    cancelAndWait();
}

juce::String SongExporter::getFileExtension (ExportFormat f)
{
    switch (f)
    {
        case ExportFormat::flac:  return "flac";
        case ExportFormat::ogg:   return "ogg";
        case ExportFormat::mp3:   return "mp3";
        case ExportFormat::wav:   break;
    }

    return "wav";
}

std::vector<int> SongExporter::getQualityChoices (ExportFormat f)
{
    if (f == ExportFormat::ogg || f == ExportFormat::mp3)
        return { 128, 192, 256, 320 };

    return { 16, 24 };
}

juce::String SongExporter::describeQuality (ExportFormat f, int quality)
{
    return f == ExportFormat::ogg || f == ExportFormat::mp3 ? juce::String (quality) + " kbps"
                                  : juce::String (quality) + "-bit";
}

double SongExporter::getSongEndBeats() const
{
    auto* edit = engine.getEdit();
    return edit == nullptr ? 0.0 : timeToBeat (*edit, getSongEnd (*edit));
}

bool SongExporter::hasCycleRange() const
{
    auto* edit = engine.getEdit();
    return edit != nullptr && edit->getTransport().getLoopRange().getLength() > te::TimeDuration::fromSeconds (0.01);
}

juce::StringArray SongExporter::getStemNames() const
{
    juce::StringArray names;

    if (auto* edit = engine.getEdit())
        for (auto* t : getAudibleTracks (*edit))
            if (! t->getClips().isEmpty())
                names.add (t->getName());

    return names;
}

float SongExporter::getProgress() const
{
    if (job == nullptr)
        return 0.0f;

    const auto numSteps = (float) job->targets.size() + encodeShare;

    if (job->current < job->targets.size())
        return ((float) job->current + (job->render != nullptr ? job->render->getProgress() : 0.0f)) / numSteps;

    return ((float) job->targets.size() + encodeShare * job->encodeProgress) / numSteps;
}

//==============================================================================
juce::Result SongExporter::start (const ExportSettings& settings, std::function<void (const ExportResult&)> onFinished)
{
    auto* edit = engine.getEdit();

    if (job != nullptr || edit == nullptr)
        return juce::Result::fail ("An export is already running.");

    // Pitch-corrected regions play their corrected audio once it's rendered
    // (see PitchEditor); finish any render still going, so the export is
    // exactly what plays.
    engine.getTracks().pitch().finishRendering();

    auto newJob = std::make_shared<Job>();
    newJob->settings = settings;
    newJob->onFinished = std::move (onFinished);

    // What to render.
    if (settings.cycleOnly)
    {
        if (! hasCycleRange())
            return juce::Result::fail ("There's no cycle range to export.");

        newJob->range = edit->getTransport().getLoopRange();
        newJob->minLength = (juce::int64) (newJob->range.getLength().inSeconds() * sampleRate);
    }
    else
    {
        const auto end = getSongEnd (*edit);

        if (end <= te::TimePosition())
            return juce::Result::fail ("There's nothing to export yet: the song has no regions.");

        newJob->range = { te::TimePosition(), end + te::TimeDuration::fromSeconds (maxTailSeconds) };
        newJob->minLength = (juce::int64) (end.inSeconds() * sampleRate);
    }

    // Leftovers from an export that never finished (e.g. a crash) go first.
    // Only old ones: another copy of the app may be exporting right now.
    auto tempRoot = platform::getLocalDataFolder().getChildFile ("Export");

    for (const auto& leftover : tempRoot.findChildFiles (juce::File::findDirectories, false))
        if (leftover.getLastModificationTime() < juce::Time::getCurrentTime() - juce::RelativeTime::days (1))
            leftover.deleteRecursively();

    newJob->tempFolder = tempRoot.getChildFile (juce::String::toHexString (juce::Random::getSystemRandom().nextInt64()));

    if (! newJob->tempFolder.createDirectory())
        return juce::Result::fail ("Couldn't create a temporary folder for the export.");

    const auto extension = "." + getFileExtension (settings.format);
    const auto allTracks = te::getAllTracks (*edit);

    auto addTarget = [&] (const juce::Array<te::Track*>& tracks, const juce::File& destination)
    {
        ExportTarget t;
        t.tracks = te::toBitSet (tracks);
        t.destination = destination;
        const auto n = juce::String ((int) newJob->targets.size());
        t.rendered = newJob->tempFolder.getChildFile (n + ".render.wav");
        t.encoded = newJob->tempFolder.getChildFile (n + extension);
        newJob->targets.push_back (std::move (t));
    };

    if (settings.stems)
    {
        // Each stem has its own track plus the shared Reverb/Delay buses, so
        // it includes its own reverb and delay (and only its own: tracks
        // left out of a render don't feed the buses). The stems add up to the song.
        juce::Array<te::Track*> buses;

        for (auto* t : te::getAudioTracks (*edit))
            if (isBusTrack (*t))
                buses.add (t);

        int number = 1;

        for (auto* t : getAudibleTracks (*edit))
        {
            if (t->getClips().isEmpty())
                continue;

            auto tracks = buses;
            tracks.add (t);
            const auto name = juce::String (number++).paddedLeft ('0', 2) + " " + t->getName();
            addTarget (tracks, settings.destination.getChildFile (juce::File::createLegalFileName (name) + extension));
        }

        if (newJob->targets.empty())
            return juce::Result::fail ("There are no tracks to export: every track is empty, muted or silenced by solo.");
    }
    else
    {
        addTarget (allTracks, settings.destination.hasFileExtension (extension) ? settings.destination
                                                                                : settings.destination.withFileExtension (extension));
    }

    // Rendering builds its own copy of the playback graph, so playback stops.
    edit->getTransport().stop (false, false);
    job = std::move (newJob);
    renderNext();
    return juce::Result::ok();
}

void SongExporter::renderNext()
{
    auto& target = job->targets[job->current];
    auto& edit = *engine.getEdit();

    te::Renderer::Parameters p (edit);
    p.tracksToDo = target.tracks;
    p.destFile = target.rendered;
    p.audioFormat = edit.engine.getAudioFileFormatManager().getWavFormat();
    p.bitDepth = 32;                   // float: nothing clips or loses detail before normalizing
    p.sampleRateForAudio = sampleRate;
    p.time = job->range;
    p.usePlugins = true;
    p.useMasterPlugins = true;         // master effects and the master volume, as heard
    p.canRenderInMono = false;         // always stereo, so every file matches
    p.checkNodesForAudio = false;      // a quiet cycle range is still a valid (silent) file

    // The finished callback runs on the render thread; hop over to the
    // message thread. The weak_ptr lets the hop notice if the job was
    // cancelled and deleted in the meantime.
    std::weak_ptr<Job> weakJob = job;

    job->render = te::EditRenderer::render (p, [this, weakJob] (tl::expected<juce::File, std::string> result)
    {
        const bool ok = result.has_value();
        const auto error = ok ? juce::String() : juce::String (result.error());

        juce::MessageManager::callAsync ([this, weakJob, ok, error]
        {
            if (auto j = weakJob.lock(); j != nullptr && j == job)
                renderFinished (ok, error);
        });
    });
}

void SongExporter::renderFinished (bool ok, const juce::String& error)
{
    job->render.reset();   // its thread has finished: this just tidies up

    if (job->cancelled)
        return finish ({ false, true, {}, {} });

    if (! ok)
        return finish ({ false, false, "The song couldn't be rendered (" + error + ").", {} });

    if (++job->current < job->targets.size())
        return renderNext();

    startEncoding();
}

void SongExporter::startEncoding()
{
    std::weak_ptr<Job> weakJob = job;

    // The raw pointer is safe: ~Job joins this thread before the Job goes.
    job->encoder = std::thread ([this, weakJob, j = job.get()]
    {
        const bool ok = j->encode();

        juce::MessageManager::callAsync ([this, weakJob, ok]
        {
            auto locked = weakJob.lock();

            if (locked == nullptr || locked != job)
                return;

            job->encoder.join();

            if (job->cancelled)
                return finish ({ false, true, {}, {} });

            if (! ok)
                return finish ({ false, false, job->encodeError, {} });

            // Everything worked: move the files into place (replacing any
            // older export with the same name; the file chooser asked).
            ExportResult result { true, false, {}, {} };

            for (auto& t : job->targets)
            {
                t.destination.getParentDirectory().createDirectory();

                if (! t.encoded.moveFileTo (t.destination))
                    return finish ({ false, false, "Couldn't save " + t.destination.getFullPathName() + ".", result.files });

                result.files.add (t.destination);
            }

            finish (result);
        });
    });
}

void SongExporter::finish (ExportResult result)
{
    auto done = std::move (job->onFinished);
    job.reset();   // deletes the temporary files

    // Rendering freed the playback graph; build it again so Play works at once.
    if (auto* edit = engine.getEdit())
        edit->getTransport().ensureContextAllocated();

    if (done)
        done (result);
}

void SongExporter::cancel()
{
    if (job == nullptr)
        return;

    job->cancelled = true;

    if (job->render != nullptr)
        job->render->cancel();
}

void SongExporter::cancelAndWait()
{
    if (job == nullptr)
        return;

    job->cancelled = true;
    job.reset();   // ~Job stops the render or encoder and waits for it

    if (auto* edit = engine.getEdit())
        edit->getTransport().ensureContextAllocated();
}

} // namespace ikaros
