// Engine-layer smoke test: exercises AudioEngine + TrackController without any UI.
// Build with -DIKAROS_BUILD_TESTS=ON, then run `ctest --test-dir build` or the
// IkarosEngineSmokeTest binary directly.

#include "engine/AudioEngine.h"
#include "engine/AudioDeviceWatcher.h"
#include "engine/PackStore.h"
#include "engine/PluginLibrary.h"
#include "engine/SoundLibrary.h"
#include "engine/TrackController.h"
#include "engine/SessionBundle.h"
#include "engine/SessionRecovery.h"
#include "engine/SessionViews.h"
#include "engine/RecordingGuard.h"
#include "engine/TestAudioDevice.h"
#include "engine/Tuner.h"
#include "engine/PedalStyles.h"
#include "engine/PortedPedals.h"                     // the ported pedals' type names
#include "engine/AmpPlugin.h"
#include "engine/LiveWaveform.h"
#include "engine/TrackHelpers.h"                  // findClip, for looking at regions' stretchers
#include "engine/FlexHelpers.h"                   // ...and their warp markers
#include "engine/TakeHelpers.h"                   // splitIntoPassTakes, to make takes without recording
#include "platform/AppFolders.h"
#include "engine/SongExporter.h"
#include "ui/state/Selection.h"                    // plain logic, no components
#include <juce_audio_formats/juce_audio_formats.h>   // to write test WAV files
#include <juce_graphics/juce_graphics.h>             // a test preview picture
#include <miniz.h>                                   // a deliberately broken bundle
#include <cstdlib>                                    // setenv
#include <functional>                                  // std::function
#include <map>
#include <set>
#include <tuple>
#include <iostream>

using namespace ikaros;

static int failures = 0;

// Tracktion settings of our own, so the test never changes the app's (and
// test plug-ins never show up in the real library).
static const juce::String testSettings ("IkarosStudioSmokeTest");

static void check (bool condition, const char* what)
{
    std::cout << (condition ? "  ok   " : "  FAIL ") << what << "\n";

    if (! condition)
        ++failures;
}

// Lets queued async callbacks run (Tracktion applies some state changes,
// e.g. parameter values restored by undo, on the next message-loop turn).
static void pumpMessages()
{
    // Longer than Tracktion's 350 ms undo-grouping timer, like a real user pause.
    juce::MessageManager::getInstance()->runDispatchLoopUntil (500);
}

// Waits (running the message loop) until the background plug-in scan ends.
static void waitForPluginScan (AudioEngine& engine)
{
    juce::MessageManager::getInstance()->runDispatchLoopUntil (200);

    for (int i = 0; i < 600 && engine.getPlugins().getScanStatus().scanning; ++i)
        juce::MessageManager::getInstance()->runDispatchLoopUntil (200);

    pumpMessages();
}

// Checks two session snapshots (AudioEngine::getStateSnapshot) are the same,
// and if not prints the first line that differs, to make it easy to track down.
// A session snapshot as one line per attribute, e.g.
// "EDIT/TRACK[1007]/PLUGIN[1008]@volume=0.3", sorted, so attribute and
// element order don't matter and a difference is easy to read.
static void flatten (const juce::XmlElement& e, const juce::String& path, juce::StringArray& lines)
{
    // Not part of the song: Tracktion stamps the time of the last edit, a
    // reopened session gets a new project ID, and recording arms whichever
    // track is selected. "parameters" is written by saving only (each
    // automated setting's hand-set value, which the live session already
    // knows), so it's in a reopened session but not the one that was saved.
    if (e.hasTagName ("INPUTDEVICEDESTINATION"))
        return;

    static const juce::StringArray ignored { "lastSignificantChange", "projectID", "base64:parameters" };

    for (int i = 0; i < e.getNumAttributes(); ++i)
        if (! ignored.contains (e.getAttributeName (i)))
            lines.add (path + "@" + e.getAttributeName (i) + "=" + e.getAttributeValue (i));

    // Children are named by their id, or numbered per tag name.
    std::map<juce::String, int> counts;

    for (auto* child : e.getChildIterator())
    {
        const auto n = counts[child->getTagName()]++;
        flatten (*child, path + "/" + child->getTagName()
                           + (child->hasAttribute ("id") ? "[" + child->getStringAttribute ("id") + "]" : "#" + juce::String (n)),
                 lines);
    }
}

static juce::StringArray flatten (const juce::String& xml)
{
    juce::StringArray lines;

    if (auto parsed = juce::parseXML (xml))
        flatten (*parsed, parsed->getTagName(), lines);

    lines.sort (false);
    return lines;
}

// Checks a session snapshot (AudioEngine::getStateSnapshot) is the same as
// the expected one, and if not prints what differs. After an undo, a
// plug-in setting that wasn't stored before may now be stored,
// at its old value: Ikaros stores a setting's value before the first change
// to it (see setParameterUndoably) so undo has a value to go back to. It must
// not still have the edited value, though (pass the edited snapshot as
// "edited" to check that).
static void checkSameState (const juce::String& expected, const juce::String& actual, const juce::String& what,
                            const juce::String& edited = {})
{
    const auto want = flatten (expected), got = flatten (actual), editedLines = flatten (edited);
    juce::StringArray missing, extra;

    auto attributeOf = [] (const juce::String& line) { return line.upToFirstOccurrenceOf ("=", true, false); };

    for (auto& line : want)
        if (! got.contains (line))
            missing.add (line);

    for (auto& line : got)
    {
        if (want.contains (line))
            continue;

        const auto element = line.upToFirstOccurrenceOf ("@", false, false).fromLastOccurrenceOf ("/", false, false);
        const bool storedNow = element.startsWith ("PLUGIN");
        const bool wasStored = std::any_of (want.begin(), want.end(),
                                            [&] (const juce::String& w) { return attributeOf (w) == attributeOf (line); });

        // The transport (loop range, playhead) is never undoable in Tracktion,
        // and a tempo change stores its defaults.
        const bool transport = element.startsWith ("TRANSPORT");

        if (! (edited.isNotEmpty() && (transport || (storedNow && ! wasStored && ! editedLines.contains (line)))))
            extra.add (line);
    }

    check (missing.isEmpty() && extra.isEmpty(), what.toRawUTF8());

    for (int i = 0; i < juce::jmin (3, missing.size()); ++i)
        std::cout << "         missing: " << missing[i].substring (0, 240) << "\n";

    for (int i = 0; i < juce::jmin (3, extra.size()); ++i)
        std::cout << "         extra:   " << extra[i].substring (0, 240) << "\n";
}

// Does one edit and checks it's a single undo step with the given name, that
// undo puts the session back exactly as it was and redo exactly as the edit
// left it (twice, since a second round is where stale state shows up).
static void checkUndoRoundTrip (AudioEngine& engine, const juce::String& stepName, const std::function<void()>& edit)
{
    std::cout << stepName << "\n";
    pumpMessages();
    const auto before = engine.getStateSnapshot();

    edit();
    pumpMessages();
    const auto after = engine.getStateSnapshot();
    check (after != before, "changes the session");
    check (engine.getUndoDescription() == stepName, "is one named undo step");

    if (engine.getUndoDescription() != stepName)
        std::cout << "         got: \"" << engine.getUndoDescription() << "\"\n";

    for (int round = 0; round < 2; ++round)
    {
        engine.undo();
        pumpMessages();
        checkSameState (before, engine.getStateSnapshot(), "undo restores the session exactly", after);

        engine.redo();
        pumpMessages();
        checkSameState (after, engine.getStateSnapshot(), "redo restores the edit exactly");
    }
}

// Saves, reopens, and checks the session reads back exactly as it was.
static void checkSaveAndReopen (AudioEngine& engine, const juce::File& file)
{
    // Undo the last edit first, so there's something to redo.
    engine.undo();
    pumpMessages();
    const auto undoName = engine.getUndoDescription(), redoName = engine.getRedoDescription();
    check (engine.saveSessionAs (file), "(save)");
    pumpMessages();
    check (engine.getUndoDescription() == undoName && engine.getRedoDescription() == redoName,
           "saving leaves undo and redo as they were");
    engine.redo();
    pumpMessages();

    // Saving copies outside audio files into the session (and points the
    // regions at the copies), so compare with the session as saved.
    check (engine.saveSession(), "(save again)");
    const auto saved = engine.getStateSnapshot();
    check (engine.openSession (AudioEngine::getSessionFileFor (file)), "(reopen)");
    pumpMessages();
    checkSameState (saved, engine.getStateSnapshot(), "the reopened session is the same");
}

//==============================================================================
// Crash recovery helpers

// A WAV file as a crash leaves it: the audio is there, but the header still
// says it holds none (the sizes are only filled in when recording stops),
// and the last sample frame is half written. Returns the whole frames in it.
static int writeCutOffWav (const juce::File& f, double seconds)
{
    f.deleteFile();
    const int frames = (int) (seconds * 44100.0);

    {
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> stream (f.createOutputStream().release());

        // Like Tracktion's takes: a "bext" chunk (the recording's start time) before the audio.
        auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (44100.0)
                                                                                    .withNumChannels (1)
                                                                                    .withBitsPerSample (16)
                                                                                    .withMetadata (juce::WavAudioFormat::bwavTimeReference, "0"));
        juce::AudioBuffer<float> buffer (1, frames);

        for (int i = 0; i < frames; ++i)
            buffer.setSample (0, i, 0.5f * std::sin ((float) i * 0.05f));

        if (writer == nullptr || ! writer->writeFromAudioSampleBuffer (buffer, 0, frames))
            return 0;
    }

    // Find the "data" chunk and zero its size and the file's.
    juce::MemoryBlock data;
    f.loadFileAsData (data);
    auto* bytes = static_cast<char*> (data.getData());

    for (size_t i = 12; i + 8 <= data.getSize(); ++i)
    {
        if (std::memcmp (bytes + i, "data", 4) == 0)
        {
            std::memset (bytes + 4, 0, 4);
            std::memset (bytes + i + 4, 0, 4);
            break;
        }
    }

    data.append ("\x01", 1);   // half of the next 16-bit sample
    return f.replaceWithData (data.getData(), data.getSize()) ? frames : 0;
}

static juce::int64 lengthOfWav (const juce::File& f)
{
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatReader> reader (wav.createReaderFor (f.createInputStream().release(), true));
    return reader != nullptr ? reader->lengthInSamples : -1;
}

// The child process the recovery test kills: it saves a session, makes more
// changes, waits for the autosave, then records MIDI and says so on stdout.
// Then it waits to be killed, mid-take.
static int runRecoveryChild (const juce::File& bundle)
{
    AudioEngine engine { testSettings + "Child" };   // settings of its own: it runs beside the test
    auto& tracks = engine.getTracks();
    juce::MessageManager::getInstance()->runDispatchLoopUntil (2500);

    engine.saveSessionAs (bundle);

    // Work the saved session doesn't have.
    auto a1 = tracks.addAudioTrack();
    auto t2 = tracks.addInstrumentTrack();
    engine.beginUndoStep ("Rename");
    tracks.renameTrack (t2, "After Save");
    tracks.regions().createChordRegion (t2, 16.0);   // bars 5 to 8

    // The autosave, on a short schedule (normally a few seconds after the
    // last change).
    engine.setAutosaveTiming (200, 0, 2000);
    juce::MessageManager::getInstance()->runDispatchLoopUntil (1500);

    if (engine.getLastAutosaveTime() == juce::Time())
        std::cout << "CHILD: no autosave" << std::endl;

    // A MIDI take from the start; the last note is still held when it dies.
    // (No count-in: notes played during it aren't part of the take.)
    tracks.setSelectedTrack (t2);
    engine.setCountInEnabled (false);
    engine.goToStart();
    juce::MessageManager::getInstance()->runDispatchLoopUntil (500);
    engine.toggleRecord();

    for (int note : { 60, 64, 67 })
    {
        tracks.inputs().noteOn (note, 0.8f);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (250);
        tracks.inputs().noteOff (note);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (100);
    }

    tracks.inputs().noteOn (72, 0.8f);
    juce::MessageManager::getInstance()->runDispatchLoopUntil (600);   // long enough for the notes to reach the disk

    // std::endl flushes, so the test sees the line straight away.
    std::cout << "RECORDING " << (engine.getTransportState().recording ? 1 : 0) << " " << (juce::int64) a1 << std::endl;
    juce::MessageManager::getInstance()->runDispatchLoopUntil (120000);   // killed long before this
    return 0;
}

//==============================================================================
// Recording through a pretend audio interface (TestAudioDevice): the test
// plays generated audio into it, block by block. Never a real microphone.

// A mono WAV of silence with a 1 ms click (a burst at `level`) at each of clickTimes.
static bool writeClickWav (const juce::File& f, double sampleRate, double seconds,
                           const std::vector<double>& clickTimes, float level)
{
    f.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> stream (f.createOutputStream().release());
    auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (sampleRate)
                                                                                .withNumChannels (1)
                                                                                .withBitsPerSample (24));
    juce::AudioBuffer<float> buffer (1, (int) (seconds * sampleRate));
    buffer.clear();

    for (auto t : clickTimes)
        for (int i = 0; i < (int) (sampleRate * 0.001); ++i)
            buffer.setSample (0, (int) std::round (t * sampleRate) + i, level);

    return writer != nullptr && writer->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples());
}

// The session's takes (the WAVs in its Audio folder). Note them before a
// take, then newTake() finds the one it added.
static juce::Array<juce::File> listTakes (AudioEngine& engine)
{
    return engine.getWorkingCopyFolder().getChildFile (SessionBundle::audioFolder)
                 .findChildFiles (juce::File::findFiles, false, "*.wav");
}

static juce::File newTake (AudioEngine& engine, const juce::Array<juce::File>& before)
{
    for (auto& f : listTakes (engine))
        if (! before.contains (f))
            return f;

    return {};
}

// Where the clicks in a WAV start (sample index of the first sample over
// `threshold` after at least `minGap` quiet samples). Reads in chunks, so
// long takes don't need to fit in memory.
struct Clicks { std::vector<juce::int64> starts; juce::int64 length = 0; double sampleRate = 0.0; };

static Clicks findClicks (const juce::File& f, float threshold = 0.1f, int minGap = 1000)
{
    Clicks result;
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (f));

    if (reader == nullptr)
        return result;

    result.length = reader->lengthInSamples;
    result.sampleRate = reader->sampleRate;
    juce::AudioBuffer<float> chunk (1, 65536);
    juce::int64 quiet = minGap;

    for (juce::int64 pos = 0; pos < reader->lengthInSamples; pos += chunk.getNumSamples())
    {
        const auto num = (int) juce::jmin ((juce::int64) chunk.getNumSamples(), reader->lengthInSamples - pos);
        reader->read (&chunk, 0, num, pos, true, false);

        for (int i = 0; i < num; ++i)
        {
            if (std::abs (chunk.getSample (0, i)) > threshold)
            {
                if (quiet >= minGap)
                    result.starts.push_back (pos + i);

                quiet = 0;
            }
            else
            {
                ++quiet;
            }
        }
    }

    return result;
}

// Runs the pretend device for `seconds`, in ~100 ms chunks, letting the
// message loop run between them (the engine's timers and checks).
// input (start sample, length) makes each chunk's input; looping back
// feeds the output back in instead; neither = silence. Normally faster than
// real time; realTime paces it like a real device (for timer-based checks).
using InputMaker = std::function<juce::AudioBuffer<float> (juce::int64, int)>;

static void runAudio (TestAudioDevice& device, double seconds, const InputMaker& input = {},
                      bool loopBack = false, bool realTime = false)
{
    const auto rate = device.getSettings().sampleRate;
    const auto total = (juce::int64) (seconds * rate);
    const int chunk = (int) (rate * 0.1);

    for (juce::int64 done = 0; done < total; done += chunk)
    {
        const auto num = (int) juce::jmin ((juce::int64) chunk, total - done);

        if (loopBack)       device.processLoopedBack (num);
        else if (input)     device.process (input (done, num));
        else                device.process (num);

        juce::MessageManager::getInstance()->runDispatchLoopUntil (realTime ? 100 : 1);
    }
}

// A steady tone on every input channel, at `level` (1.0 = full scale, which clips).
static InputMaker tone (const TestAudioDevice& device, float level, bool square = false)
{
    const auto channels = device.getSettings().numInputs;
    const auto rate = device.getSettings().sampleRate;

    return [=] (juce::int64 start, int num)
    {
        juce::AudioBuffer<float> b (channels, num);

        for (int i = 0; i < num; ++i)
        {
            auto s = std::sin (2.0 * juce::MathConstants<double>::pi * 440.0 * (double) (start + i) / rate);
            auto v = level * (float) (square ? (s >= 0.0 ? 1.0 : -1.0) : s);

            for (int ch = 0; ch < channels; ++ch)
                b.setSample (ch, i, v);
        }

        return b;
    };
}

static float peakOf (const juce::AudioBuffer<float>& b)
{
    return b.getNumSamples() > 0 ? b.getMagnitude (0, b.getNumSamples()) : 0.0f;
}

// Memory the process is using (resident), in MB. Linux only; 0 elsewhere.
static double residentMB()
{
   #if JUCE_LINUX
    juce::StringArray lines;
    juce::File ("/proc/self/status").readLines (lines);

    for (auto& line : lines)
        if (line.startsWith ("VmRSS:"))
            return line.fromFirstOccurrenceOf (":", false, false).trim().getDoubleValue() / 1024.0;   // kB
   #endif

    return 0.0;
}

static void runRecordingSection (const juce::File& dir)
{
    // The waveform drawn while a take is recorded (see LiveWaveform), fed by
    // hand as the audio thread would feed it: no device needed.
    std::cout << "Live waveform\n";
    {
        juce::AudioDeviceManager noDevice;   // never opened
        LiveWaveform live (noDevice);
        live.prepare (48000.0);
        live.setInputChannels (0, 1);        // a stereo input

        // Plays `seconds` into it in 256-sample blocks: silent left, and the
        // right side at `level` (the louder side counts).
        auto play = [&live] (double seconds, float level)
        {
            std::vector<float> left (256, 0.0f), right (256, level);
            const float* channels[] = { left.data(), right.data() };
            const auto total = juce::roundToInt (seconds * 48000.0);

            for (int done = 0; done < total; done += 256)
                live.pushInput (channels, 2, juce::jmin (256, total - done));   // (the last block may be short)
        };

        auto toBeats = [] (double seconds) { return seconds * 2.0; };   // 120 BPM

        // A take starting at 1 s, after half a second of count-in.
        live.start (7, 1.0);
        play (0.5, 0.0f);    // the count-in: before the take, left out
        play (0.2, 0.0f);
        play (0.3, 0.5f);
        live.collect (1.5, toBeats);   // the take has run for half a second

        const auto& got = live.get();
        bool silenceThenSound = got.peaks.size() == 50;

        for (size_t i = 0; silenceThenSound && i < got.peaks.size(); ++i)
            silenceThenSound = std::abs (got.peaks[i] - (i >= 20 ? 0.5f : 0.0f)) < 0.001f;

        check (got.track == 7 && silenceThenSound,
               "it has one peak per 10 ms of the take (the count-in left out): 0.2 s quiet, then the louder side");
        check (got.beats.size() == got.peaks.size() && std::abs (got.beats.front() - 2.0) < 1.0e-9
                 && std::abs (got.beats.back() - 2.0 * 1.49) < 1.0e-9,
               "each slice knows its beat on the timeline");

        // A cycle recording jumps back: the next pass draws over the first.
        play (0.1, 0.25f);
        live.collect (1.1, toBeats);
        check (got.peaks.size() == 50 && std::abs (got.peaks[0] - 0.25f) < 0.001f && std::abs (got.peaks[9] - 0.25f) < 0.001f
                 && got.peaks[10] == 0.0f,
               "a second pass replaces the first where it plays");

        live.stop();
        check (live.get().isEmpty() && ! live.isRunning(), "stopping forgets it");
    }

    AudioEngine engine { testSettings, false };

    // 10 ms of input latency and 5 ms of output latency, as a USB interface
    // might report. (Replaced by one without latency after the loopback test.)
    TestAudioDevice::Settings settings;
    settings.inputLatency = 480;
    settings.outputLatency = 240;
    auto device = std::make_unique<TestAudioDevice> (engine, settings);
    const auto rate = settings.sampleRate;
    const auto block = settings.blockSize;

    auto& tracks = engine.getTracks();
    juce::MessageManager::getInstance()->runDispatchLoopUntil (2500);
    runAudio (*device, 0.2);

    engine.setBpm (120.0);              // 2 beats a second
    engine.setCountInEnabled (false);
    engine.setMetronomeEnabled (false);

    std::cout << "Audio device and latency\n";
    {
        auto s = engine.getAudioDeviceStatus();
        check (s.running && s.sampleRate == rate && s.bufferSize == block, "the pretend device is running");
        check (std::abs (s.inputLatencyMs - 10.0) < 0.01 && std::abs (s.outputLatencyMs - 5.0) < 0.01,
               "its input and output latency are reported");
        // Plus the 2 ms the master's limiter looks ahead (new sessions have one).
        check (std::abs (s.monitoringLatencyMs - 17.0) < 0.01, "the monitoring delay is input + output + plug-in latency");
        check (std::abs (s.recordingCompensationMs - 17.0) < 0.01, "and recordings are moved back by as much");
        check (s.dropouts == 0, "no dropouts yet");
    }

    // The track that records, from Input 1 (mono, the first input).
    auto inputs = tracks.inputs().getAudioInputs();
    check (inputs.size() == 3 && inputs[0].name == "Input 1", "the device's inputs are offered (1, 2, 1+2)");

    if (inputs.empty())
        return;

    auto rec = tracks.addAudioTrack();
    tracks.inputs().setTrackInput (rec, inputs[0].id);
    tracks.setSelectedTrack (rec);
    tracks.inputs().setArmed (rec, true);
    runAudio (*device, 0.3);

    // Each take starts 20 beats after the last, so they don't overlap.
    double nextTakeBeat = 40.0;

    auto startTake = [&]
    {
        engine.setPositionBeats (nextTakeBeat);
        nextTakeBeat += 20.0;
        engine.toggleRecord();
        return engine.getTransportState().recording;
    };

    auto lastRegion = [&]() -> std::optional<RegionInfo>
    {
        auto regions = tracks.regions().getRegions (rec);

        if (regions.empty())
            return std::nullopt;

        return *std::max_element (regions.begin(), regions.end(),
                                  [] (auto& a, auto& b) { return a.startBeat < b.startBeat; });
    };

    auto regionCount = [&] { return tracks.regions().getRegions (rec).size(); };

    std::cout << "Monitoring\n";
    {
        tracks.inputs().setMonitoring (rec, true);
        runAudio (*device, 0.2);
        auto heard = device->process (tone (*device, 0.5f) (0, (int) rate / 2));
        check (peakOf (heard) > 0.1f, "with monitoring on, the input is heard");

        tracks.inputs().setMonitoring (rec, false);
        runAudio (*device, 0.2);
        heard = device->process (tone (*device, 0.5f) (0, (int) rate / 2));
        check (peakOf (heard) < 0.001f, "with it off, it isn't");
    }

    std::cout << "Loopback: recordings line up with the music\n";
    {
        // Another track plays a click at 2.5 s; a cable takes the outputs
        // back to the inputs, and the armed track records it. The click
        // comes back late by the input and output latency plus the cable's
        // block. Tracktion moves takes back by the reported latency plus one
        // block (see the report in RecordingGuard.h), so the recorded click
        // should land at 2.5 s exactly.
        auto clickFile = dir.getChildFile ("Click.wav");
        check (writeClickWav (clickFile, rate, 2.0, { 0.5 }, 0.5f), "(wrote a click)");

        auto player = tracks.addAudioTrack();
        tracks.regions().importAudioFiles (player, { clickFile.getFullPathName() }, 4.0);   // beat 4 = 2 s
        tracks.setSelectedTrack (rec);
        tracks.inputs().setArmed (rec, true);
        runAudio (*device, 0.3, {}, true);

        // From 1 s in: moved back by the latency, the take still starts
        // after 0, where it can be placed exactly.
        const auto takesBefore = listTakes (engine);
        engine.setPositionBeats (2.0);
        engine.toggleRecord();
        check (engine.getTransportState().recording, "recording");
        runAudio (*device, 3.0, {}, true);
        engine.toggleRecord();
        pumpMessages();

        auto region = lastRegion();
        auto clicks = findClicks (newTake (engine, takesBefore));
        check (region.has_value() && clicks.starts.size() == 1, "the take holds the click");

        if (region && clicks.starts.size() == 1)
        {
            // minStartBeat = where the take's first sample sits on the timeline.
            const auto heardAt = region->minStartBeat / 2.0 + (double) clicks.starts[0] / rate;
            const auto offBy = juce::roundToInt ((heardAt - 2.5) * rate);
            std::cout << "    (round trip " << settings.inputLatency + settings.outputLatency + block
                      << " samples; recorded click " << offBy << " samples off)\n";
            check (std::abs (offBy) <= 2, "the recorded click lines up with the played one, to the sample");
        }

        auto report = engine.takeRecordingReport();
        check (report && report->reason == RecordingStopReason::user && report->audio, "a report on the take");
        check (report && ! report->clipped && report->dropouts == 0, "it didn't clip or drop out");
        check (! engine.takeRecordingReport(), "each report is handed over once");

        tracks.deleteTrack (player);
        pumpMessages();
    }

    // The rest on a device without simulated latency (so it can change
    // sample rate, see TestAudioDevice::setSampleRate).
    device.reset();
    device = std::make_unique<TestAudioDevice> (engine, TestAudioDevice::Settings {});
    juce::MessageManager::getInstance()->runDispatchLoopUntil (300);
    runAudio (*device, 0.3);

    std::cout << "Clipping\n";
    {
        tracks.inputs().getInputMeter (rec);   // the UI's meters keep reading it
        check (startTake(), "recording a too-loud input");
        runAudio (*device, 1.0, tone (*device, 1.0f, true));
        check (tracks.inputs().getInputMeter (rec).clipped, "the input meter's clip light comes on");
        engine.toggleRecord();
        auto report = engine.takeRecordingReport();
        check (report && report->clipped, "the take is reported as clipped");

        tracks.inputs().clearClipIndicator (rec);
        check (! tracks.inputs().getInputMeter (rec).clipped, "clicking the clip light turns it off");

        check (startTake(), "recording a comfortable level");
        runAudio (*device, 1.0, tone (*device, 0.5f));
        auto meter = tracks.inputs().getInputMeter (rec);
        check (! meter.clipped && meter.level.leftDb > -12.0f && meter.level.leftDb < -3.0f, "the meter shows it (-6 dB), no clip");
        engine.toggleRecord();
        report = engine.takeRecordingReport();
        check (report && ! report->clipped && report->dropouts == 0, "no clipping, no dropouts");
    }

    std::cout << "Dropouts\n";
    {
        check (startTake(), "recording");
        runAudio (*device, 1.0, tone (*device, 0.3f));
        juce::Thread::sleep (400);   // the computer falls behind: 400 ms of audio never arrive
        runAudio (*device, 1.0, tone (*device, 0.3f));
        engine.toggleRecord();
        auto report = engine.takeRecordingReport();
        check (report && report->dropouts >= 1, "the gap is counted as a dropout");
        check (engine.getAudioDeviceStatus().dropouts >= 1, "and in the device's count");
    }

    std::cout << "The device stops responding mid-take\n";
    {
        const auto before = regionCount();
        check (startTake(), "recording");
        runAudio (*device, 1.0, tone (*device, 0.3f));
        juce::MessageManager::getInstance()->runDispatchLoopUntil (2500);   // no more audio arrives
        check (! engine.getTransportState().recording, "recording stops by itself");
        auto report = engine.takeRecordingReport();
        check (report && report->reason == RecordingStopReason::deviceStopped, "the report says the device stopped");
        auto region = lastRegion();
        check (regionCount() == before + 1 && region && region->lengthBeats > 1.8 && region->lengthBeats < 2.6,
               "what was recorded (1 s) is kept");
        runAudio (*device, 0.3);
    }

    std::cout << "The device is closed mid-take\n";
    {
        const auto before = regionCount();
        check (startTake(), "recording");
        runAudio (*device, 1.0, tone (*device, 0.3f));
        device->close();
        check (! engine.getTransportState().recording, "recording stops straight away");
        auto report = engine.takeRecordingReport();
        check (report && report->reason == RecordingStopReason::deviceStopped, "the report says the device stopped");
        check (regionCount() == before + 1, "what was recorded is kept");

        device->reopen();
        juce::MessageManager::getInstance()->runDispatchLoopUntil (300);
        runAudio (*device, 0.3);
        check (engine.getAudioDeviceStatus().running, "the device comes back");
    }

    std::cout << "The sample rate changes mid-take\n";
    {
        const auto before = regionCount();
        check (startTake(), "recording at 48 kHz");
        runAudio (*device, 1.0, tone (*device, 0.3f));
        device->setSampleRate (44100.0);
        check (! engine.getTransportState().recording, "recording stops");
        auto report = engine.takeRecordingReport();
        check (report && report->reason == RecordingStopReason::sampleRateChanged, "the report says the sample rate changed");
        auto region = lastRegion();
        check (regionCount() == before + 1 && region && region->lengthBeats > 1.8 && region->lengthBeats < 2.6,
               "what was recorded is kept, and still lasts 1 s");

        juce::MessageManager::getInstance()->runDispatchLoopUntil (300);
        runAudio (*device, 0.3);
        const auto takesBefore = listTakes (engine);
        check (startTake(), "recording again at 44.1 kHz");
        runAudio (*device, 1.0, tone (*device, 0.3f));
        engine.toggleRecord();
        region = lastRegion();
        check (region && region->lengthBeats > 1.8 && region->lengthBeats < 2.6, "works");
        check (findClicks (newTake (engine, takesBefore)).sampleRate == 44100.0, "at the new rate");
        engine.takeRecordingReport();
    }

    std::cout << "Two bars of count-in\n";
    {
        // 4/4 at 120 bpm: two bars = 8 beats = 4 seconds of clicks first.
        // (Tracktion starts half a beat before the first click: 4.25 s.)
        engine.setCountInBars (2);
        const auto takeBeat = nextTakeBeat;
        check (startTake(), "recording");
        runAudio (*device, 3.0, tone (*device, 0.3f));
        auto state = engine.getTransportState();
        check (state.countingIn && state.positionBeats < takeBeat && state.positionBeats > takeBeat - 8.0,
               "it counts in for two bars, from two bars before the take");
        runAudio (*device, 2.25, tone (*device, 0.3f));   // then a second of recording
        check (! engine.getTransportState().countingIn, "then records");
        engine.toggleRecord();
        pumpMessages();

        auto region = lastRegion();
        check (region && std::abs (region->startBeat - takeBeat) < 0.05 && region->lengthBeats > 1.5 && region->lengthBeats < 2.5,
               "the take starts where Record was pressed, without the count-in");
        engine.takeRecordingReport();
        engine.setCountInEnabled (false);
    }

    std::cout << "Disk full\n";
    {
        engine.setMinimumFreeDiskSpace (std::numeric_limits<juce::int64>::max());   // act out a full disk
        const auto before = regionCount();
        engine.toggleRecord();
        check (! engine.getTransportState().recording, "Record won't start without enough disk space");
        auto report = engine.takeRecordingReport();
        check (report && report->reason == RecordingStopReason::notStarted, "and says why");

        engine.setMinimumFreeDiskSpace (RecordingGuard::defaultMinimumFreeSpace);
        check (startTake(), "recording");
        runAudio (*device, 1.0, tone (*device, 0.3f));
        engine.setMinimumFreeDiskSpace (std::numeric_limits<juce::int64>::max());   // the disk fills up
        runAudio (*device, 1.5, tone (*device, 0.3f), false, true);   // in real time: it's checked once a second
        check (! engine.getTransportState().recording, "recording stops when the disk fills up");
        report = engine.takeRecordingReport();
        check (report && report->reason == RecordingStopReason::diskFull, "the report says the disk is full");
        check (regionCount() == before + 1, "what was recorded is kept");
        engine.setMinimumFreeDiskSpace (RecordingGuard::defaultMinimumFreeSpace);
    }
}

// A long take of generated clicks, one a second: the take must hold every
// one exactly a second apart (no drift, no lost samples), and memory must
// stay flat. IKAROS_LONG_TAKE_MINUTES sets the length (default 20).
//==============================================================================
// Takes: cycle recording, recording over regions, choosing, deleting and
// flattening takes, and takes in saved, exported and recovered sessions.
// Through the pretend audio interface, with generated tones whose level
// steps up each pass, so every take can be told apart by how loud it is.

// A mono WAV of a 440 Hz tone, `levels[i]` loud for `secondsEach` apiece,
// `totalSeconds` long (the last level carries on).
static bool writeStepsWav (const juce::File& f, double rate, double secondsEach,
                           const std::vector<float>& levels, double totalSeconds)
{
    const int length = (int) (totalSeconds * rate);
    juce::AudioBuffer<float> audio (1, length);

    for (int i = 0; i < length; ++i)
    {
        const auto step = juce::jmin ((size_t) (i / (secondsEach * rate)), levels.size() - 1);
        audio.setSample (0, i, levels[step] * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 440.0 * i / rate));
    }

    f.getParentDirectory().createDirectory();
    f.deleteFile();
    std::unique_ptr<juce::OutputStream> out = f.createOutputStream();
    auto writer = juce::WavAudioFormat().createWriterFor (out, juce::AudioFormatWriterOptions()
                                                               .withSampleRate (rate).withNumChannels (1).withBitsPerSample (24));
    return writer != nullptr && writer->writeFromAudioSampleBuffer (audio, 0, length);
}

static void runTakesSection (const juce::File& dir)
{
    AudioEngine engine { testSettings, false };
    TestAudioDevice::Settings settings;
    auto device = std::make_unique<TestAudioDevice> (engine, settings);
    const auto rate = settings.sampleRate;

    auto& tracks = engine.getTracks();
    auto& takes = tracks.takes();
    juce::MessageManager::getInstance()->runDispatchLoopUntil (2500);
    runAudio (*device, 0.2);

    engine.setBpm (120.0);              // 2 beats a second
    engine.setCountInEnabled (false);
    engine.setMetronomeEnabled (false);

    auto inputs = tracks.inputs().getAudioInputs();
    check (! inputs.empty(), "(the pretend device has inputs)");

    if (inputs.empty())
        return;

    auto rec = tracks.addAudioTrack();
    tracks.inputs().setTrackInput (rec, inputs[0].id);
    tracks.setSelectedTrack (rec);
    tracks.inputs().setArmed (rec, true);
    runAudio (*device, 0.3);

    // A tone that starts at `first` and gets 0.1 louder every pass.
    auto steps = [&] (float first, double secondsEach) -> InputMaker
    {
        const auto channels = settings.numInputs;

        return [=] (juce::int64 start, int num)
        {
            juce::AudioBuffer<float> b (channels, num);

            for (int i = 0; i < num; ++i)
            {
                const auto pos = (double) (start + i);
                const auto level = first + 0.1f * (float) std::floor (pos / (secondsEach * rate));
                const auto v = level * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 440.0 * pos / rate);

                for (int ch = 0; ch < channels; ++ch)
                    b.setSample (ch, i, v);
            }

            return b;
        };
    };

    // Beats 8 to 12 = 2 seconds a pass.
    auto recordCycle = [&] (float firstLevel, double seconds)
    {
        engine.setLooping (true);
        engine.setLoopRangeBeats (8.0, 12.0);
        engine.setPositionBeats (2.0);   // cycle recording starts at the loop's start anyway
        engine.toggleRecord();
        const bool started = engine.getTransportState().recording;
        runAudio (*device, seconds, steps (firstLevel, 2.0));
        engine.toggleRecord();
        pumpMessages();
        return started;
    };

    auto regionsOn = [&] (TrackId t) { return tracks.regions().getRegions (t); };
    auto loudness = [&] { return engine.renderPeak (8.5, 11.5); };   // the middle of a pass
    auto near = [] (float a, float b) { return std::abs (a - b) < 0.1f * b; };

    std::cout << "Cycle recording\n";
    RegionId cycle = 0;
    float take1 = 0.0f;
    {
        // Three passes and a moment of a fourth.
        check (recordCycle (0.1f, 6.2), "(recording)");
        auto regions = regionsOn (rec);
        check (regions.size() == 1, "a cycle recording makes one region");

        if (regions.size() == 1)
        {
            auto& r = regions[0];
            cycle = r.id;
            check (std::abs (r.startBeat - 8.0) < 0.01 && std::abs (r.lengthBeats - 4.0) < 0.01, "covering the loop");
            check (r.takeCount == 3, "with a take per pass (a last pass of a moment is left out)");
            check (r.currentTake == 2, "playing the newest");
            std::cout << "    (" << r.takeCount << " takes, playing " << r.currentTake + 1 << ")\n";

            auto list = takes.getTakes (cycle);
            check (list.size() == 3 && list[0].name == "Take 1" && list[2].current && ! list[0].current,
                   "the take menu lists them, with the current one ticked");
        }

        const auto take3 = loudness();
        takes.setCurrentTake (cycle, 0);
        pumpMessages();
        take1 = loudness();
        check (take1 > 0.01f && near (take3 / take1, 3.0f), "each take plays what its pass recorded");
        std::cout << "    (take 1 peaks at " << take1 << ", take 3 at " << take3 << ")\n";
        engine.undo();
        pumpMessages();
        check (near (loudness(), take3), "(undo)");

        engine.undo();   // the recording
        pumpMessages();
        check (regionsOn (rec).empty(), "undoing the recording removes the region, takes and all");
        engine.redo();
        pumpMessages();
        check (regionsOn (rec).size() == 1 && regionsOn (rec)[0].takeCount == 3, "and redo brings it back");
    }

    checkUndoRoundTrip (engine, "Choose Take", [&] { takes.setCurrentTake (cycle, 0); });
    check (tracks.regions().getRegion (cycle)->currentTake == 0, "choosing a take makes it current");
    check (! takes.setCurrentTake (cycle, 0) && ! takes.setCurrentTake (cycle, 7), "(choosing the current take, or one that isn't there, does nothing)");

    std::cout << "The same cycle again\n";
    {
        check (recordCycle (0.4f, 4.1), "(recording)");
        auto regions = regionsOn (rec);
        check (regions.size() == 1 && regions[0].id == cycle, "the new passes join the region");
        check (! regions.empty() && regions[0].takeCount == 5 && regions[0].currentTake == 4,
               "as takes 4 and 5, playing the newest");
        check (near (loudness() / take1, 5.0f), "(which is take 5's audio)");
        engine.setLooping (false);
    }

    std::cout << "Export plays the current take\n";
    {
        takes.setCurrentTake (cycle, 1);
        pumpMessages();
        const auto take2 = loudness();
        check (near (take2 / take1, 2.0f), "(take 2 chosen)");

        // The loudest sample of the cycle, exported.
        auto exportPeak = [&]
        {
            ExportSettings s;
            s.cycleOnly = true;
            s.destination = dir.getChildFile ("Takes Export.wav");
            s.destination.deleteFile();
            bool done = false, ok = false;

            if (engine.getExporter().start (s, [&] (const ExportResult& r) { ok = r.ok; done = true; }).failed())
                return 0.0f;

            for (int i = 0; i < 1200 && ! done; ++i)
                juce::MessageManager::getInstance()->runDispatchLoopUntil (50);

            juce::AudioFormatManager formats;
            formats.registerBasicFormats();
            float peak = 0.0f;

            if (std::unique_ptr<juce::AudioFormatReader> reader { formats.createReaderFor (s.destination) }; ok && reader != nullptr)
            {
                juce::AudioBuffer<float> audio ((int) reader->numChannels, (int) reader->lengthInSamples);
                reader->read (&audio, 0, audio.getNumSamples(), 0, true, true);
                peak = audio.getMagnitude (0, audio.getNumSamples());
            }

            return peak;
        };

        const auto exported2 = exportPeak();
        takes.setCurrentTake (cycle, 3);
        pumpMessages();
        const auto exported4 = exportPeak();
        check (exported2 > 0.01f && near (exported4 / exported2, 2.0f), "the exported song has the current take in it");
        std::cout << "    (exported peaks: take 2 " << exported2 << ", take 4 " << exported4 << ")\n";
    }

    std::cout << "Saving and reopening\n";
    {
        auto file = dir.getChildFile ("Takes.iksession");
        takes.setCurrentTake (cycle, 3);
        pumpMessages();
        checkSaveAndReopen (engine, file);
        auto regions = regionsOn (rec);
        check (regions.size() == 1 && regions[0].takeCount == 5 && regions[0].currentTake == 3, "the takes are saved");

        if (! regions.empty())
            cycle = regions[0].id;

        // A bundle is a zip file: count the audio in it.
        auto wavsIn = [] (const juce::File& bundle)
        {
            juce::ZipFile zip (bundle);
            int wavs = 0;

            for (int i = 0; i < zip.getNumEntries(); ++i)
                wavs += zip.getEntry (i)->filename.endsWith (".wav") ? 1 : 0;

            return wavs;
        };

        check (wavsIn (AudioEngine::getSessionFileFor (file)) == 5, "every take's file is in the saved session");

        auto copy = dir.getChildFile ("Takes Copy.iksession");
        check (engine.saveSessionAs (copy) && wavsIn (AudioEngine::getSessionFileFor (copy)) == 5, "and in a copy saved with Save As");
        std::cout << "    (" << wavsIn (AudioEngine::getSessionFileFor (copy)) << " files in the copy)\n";
        check (near (loudness() / take1, 4.0f), "the reopened region plays its current take");
        takes.setCurrentTake (cycle, 0);
        pumpMessages();
        check (near (loudness(), take1), "and its other takes can be chosen");
        takes.setCurrentTake (cycle, 3);
        pumpMessages();
    }

    // An instrument track for the MIDI tests (and for a recovered MIDI take).
    auto keys = tracks.addInstrumentTrack();

    std::cout << "Autosave and recovery\n";
    {
        // After a crash: the autosave (with the takes region in it) and a
        // cycle recording cut off by the crash. The crash is acted out on a
        // copy of this session's working copy: its lock isn't held, which is
        // what a crash leaves.
        engine.autosaveNow();
        engine.waitForAutosave();
        auto work = engine.getWorkingCopyFolder();
        auto crashed = work.getSiblingFile (work.getFileName() + "-crashed");
        check (work.copyDirectoryTo (crashed), "(copied the working copy)");

        auto info = SessionRecovery::readInfo (crashed).value_or (SessionRecovery::Info {});
        info.name = "Crashed Cycle";
        info.takes.clear();
        juce::String autosaveId;

        if (auto xml = juce::parseXML (crashed.getChildFile (SessionRecovery::autosaveFile)))
            autosaveId = xml->getStringAttribute (SessionRecovery::autosaveIdProperty);

        check (autosaveId.isNotEmpty(), "(the autosave has its ID)");

        // Audio: three passes of beats 24 to 28 and a moment of a fourth, all in one file.
        SessionRecovery::Take audio;
        audio.track = rec;
        audio.path = "Audio/Cut Off Cycle.wav";
        audio.startBeats = 24.0;
        audio.loopEndBeats = 28.0;
        audio.recordedAfter = autosaveId;
        writeStepsWav (crashed.getChildFile (audio.path), rate, 2.0, { 0.1f, 0.2f, 0.3f }, 6.2);
        info.takes.push_back (audio);

        // MIDI, kept as takes: times are folded into the loop (beats 32 to
        // 34 = 16 to 17 seconds), so the second pass starts where they jump back.
        SessionRecovery::Take midi;
        midi.midi = true;
        midi.midiTakes = true;
        midi.track = keys;
        midi.path = juce::String (SessionRecovery::takesFolder) + "/cycle.txt";
        midi.startBeats = 32.0;
        midi.loopEndBeats = 34.0;
        midi.recordedAfter = autosaveId;
        juce::Array<juce::MidiMessage> played;

        for (auto [pitch, on, off] : { std::tuple { 60, 16.2, 16.4 }, std::tuple { 62, 16.6, 16.8 },
                                       std::tuple { 64, 16.1, 16.3 }, std::tuple { 65, 16.5, 16.7 }, std::tuple { 67, 16.8, 16.9 } })
        {
            played.add (juce::MidiMessage::noteOn (1, pitch, (juce::uint8) 100).withTimeStamp (on));
            played.add (juce::MidiMessage::noteOff (1, pitch).withTimeStamp (off));
        }

        crashed.getChildFile (midi.path).getParentDirectory().createDirectory();
        crashed.getChildFile (midi.path).replaceWithText (SessionRecovery::toJournalLines (played));
        info.takes.push_back (midi);
        check (SessionRecovery::writeInfo (crashed, info), "(noted the takes)");

        auto found = engine.findRecoverableSessions();
        auto session = std::find_if (found.begin(), found.end(), [] (auto& s) { return s.name == "Crashed Cycle"; });
        check (session != found.end(), "the crashed session is offered");

        if (session != found.end())
        {
            check (engine.recoverSession (*session), "Recover opens it");
            juce::MessageManager::getInstance()->runDispatchLoopUntil (3500);   // past the clean-slate reset

            auto regions = regionsOn (rec);
            auto at = [&] (double beat) -> std::optional<RegionInfo>
            {
                for (auto& r : regions)
                    if (std::abs (r.startBeat - beat) < 0.01)
                        return r;

                return std::nullopt;
            };

            auto autosaved = at (8.0), cutOff = at (24.0);
            check (autosaved && autosaved->takeCount == 5 && autosaved->currentTake == 3, "the autosaved takes are recovered");
            check (cutOff && cutOff->takeCount == 3 && cutOff->currentTake == 2 && std::abs (cutOff->lengthBeats - 4.0) < 0.01,
                   "a cycle recording cut off by the crash comes back as one region with its takes");

            if (cutOff)
            {
                const auto newest = engine.renderPeak (24.5, 27.5);
                takes.setCurrentTake (cutOff->id, 0);
                pumpMessages();
                check (near (newest / engine.renderPeak (24.5, 27.5), 3.0f), "(and its takes are each pass)");
            }

            auto midiRegions = regionsOn (keys);
            check (midiRegions.size() == 1 && midiRegions[0].takeCount == 2 && midiRegions[0].notes.size() == 3,
                   "a cut-off MIDI cycle comes back with a take per pass");

            if (autosaved)
                cycle = autosaved->id;
        }
    }

    checkUndoRoundTrip (engine, "Delete Unused Takes", [&] { takes.deleteUnusedTakes (cycle); });
    {
        auto r = tracks.regions().getRegion (cycle);
        check (r && r->takeCount == 1 && near (loudness() / take1, 4.0f), "deleting unused takes keeps the current one");
    }

    checkUndoRoundTrip (engine, "Flatten Takes", [&] { takes.flattenTakes (cycle); });
    {
        auto r = tracks.regions().getRegion (cycle);
        check (r && r->takeCount == 0 && near (loudness() / take1, 4.0f), "flattening leaves an ordinary region");
        check (! takes.flattenTakes (cycle) && ! takes.deleteUnusedTakes (cycle), "(an ordinary region has nothing to flatten)");
    }

    std::cout << "Recording over a region\n";
    {
        for (auto& r : regionsOn (rec))
            if (r.id != cycle)
                tracks.regions().deleteRegion (r.id);

        engine.setPositionBeats (0.0);
        pumpMessages();
        const auto before = engine.getStateSnapshot();
        const auto oldLevel = engine.renderPeak (9.5, 10.5);

        // Beats 9 to 11, not cycling.
        tracks.setSelectedTrack (rec);
        tracks.inputs().setArmed (rec, true);
        runAudio (*device, 0.3);
        engine.setPositionBeats (9.0);
        engine.toggleRecord();
        runAudio (*device, 1.0, steps (0.6f, 100.0));
        engine.toggleRecord();
        pumpMessages();

        auto regions = regionsOn (rec);
        std::sort (regions.begin(), regions.end(), [] (auto& a, auto& b) { return a.startBeat < b.startBeat; });
        check (regions.size() == 3, "the old region is cut around the new one");

        if (regions.size() == 3)
        {
            auto& added = regions[1];
            check (std::abs (regions[0].getEndBeat() - 9.0) < 0.02 && std::abs (regions[2].getEndBeat() - 12.0) < 0.01
                    && std::abs (regions[2].startBeat - added.getEndBeat()) < 0.01, "(either side of it)");
            check (added.takeCount == 2 && added.currentTake == 1, "what was recorded over is kept as a take");
            check (near (engine.renderPeak (9.5, 10.5) / oldLevel, 1.5f), "(the new take plays)");

            takes.setCurrentTake (added.id, 0);
            pumpMessages();
            check (near (engine.renderPeak (9.5, 10.5), oldLevel), "choosing the first take brings back what was there");
            engine.undo();
        }

        engine.undo();
        engine.setPositionBeats (0.0);   // (the playhead isn't part of the undo)
        pumpMessages();
        checkSameState (before, engine.getStateSnapshot(), "undo puts the old region back exactly");
    }

    std::cout << "Recording over a reversed or transposed region\n";
    {
        // The audio editor's Reverse and Transpose change how a region
        // sounds, so what it plays can't simply be kept as a take (see
        // TakeEditor's canBecomeTake). Like a stretched loop, it's left as
        // it was, under the new recording.
        engine.setPositionBeats (0.0);
        pumpMessages();
        const auto before = engine.getStateSnapshot();

        for (bool reversed : { true, false })
        {
            if (reversed)
                tracks.regions().setRegionReversed (cycle, true);
            else
                tracks.regions().setRegionTranspose (cycle, 5);

            tracks.setSelectedTrack (rec);
            tracks.inputs().setArmed (rec, true);
            runAudio (*device, 0.3);
            engine.setPositionBeats (9.0);
            engine.toggleRecord();
            runAudio (*device, 1.0, steps (0.6f, 100.0));
            engine.toggleRecord();
            pumpMessages();

            auto regions = regionsOn (rec);
            std::sort (regions.begin(), regions.end(), [] (auto& a, auto& b) { return a.startBeat < b.startBeat; });
            const auto old = tracks.regions().getRegion (cycle);
            check (regions.size() == 2 && regions[1].id != cycle && regions[1].takeCount == 0
                     && old && old->reversed == reversed && old->transposeSemitones == (reversed ? 0 : 5)
                     && std::abs (old->startBeat - 8.0) < 0.01 && std::abs (old->getEndBeat() - 12.0) < 0.01,
                   reversed ? "recording over a reversed region doesn't keep it as a take, and leaves it as it was"
                            : "nor over a transposed one");

            engine.undo();   // the recording
            engine.undo();   // reversing or transposing
            engine.setPositionBeats (0.0);
            pumpMessages();
        }

        checkSameState (before, engine.getStateSnapshot(), "(undo puts the region back)");
    }

    std::cout << "MIDI cycle recording\n";
    {
        tracks.setSelectedTrack (keys);
        engine.setLooping (true);
        engine.takeRecordingReport();   // (the last audio take's)

        // Tracktion can't cycle-record a loop under 2 seconds.
        engine.setLoopRangeBeats (4.0, 7.0);
        engine.toggleRecord();
        auto refused = engine.takeRecordingReport();
        check (! engine.getTransportState().recording && refused && refused->reason == RecordingStopReason::loopTooShort,
               "a loop under 2 seconds is refused, with a report saying why");

        // MIDI from the on-screen keyboard is timed by the clock, so this
        // part runs in real time. Beats 4 to 8 = 2 seconds a pass.
        engine.setLoopRangeBeats (4.0, 8.0);
        runAudio (*device, 0.5, {}, false, true);

        // One note in the first pass, two in the second.
        auto recordPasses = [&] (std::vector<std::vector<int>> passes)
        {
            engine.toggleRecord();

            for (auto& notes : passes)
            {
                runAudio (*device, 0.2, {}, false, true);

                for (int note : notes)
                {
                    tracks.inputs().noteOn (note, 0.8f);
                    runAudio (*device, 0.1, {}, false, true);
                    tracks.inputs().noteOff (note);
                    runAudio (*device, 0.1, {}, false, true);
                }

                runAudio (*device, 1.8 - 0.2 * (double) notes.size(), {}, false, true);
            }

            runAudio (*device, 0.2, {}, false, true);   // a moment of a pass without notes
            engine.toggleRecord();
            pumpMessages();
        };

        // Merged (the default): one region with every pass's notes.
        check (! engine.getMidiCycleTakes(), "MIDI passes are merged by default");
        recordPasses ({ { 60 }, { 64, 67 } });
        auto regions = regionsOn (keys);
        std::optional<RegionInfo> merged;

        for (auto& r : regions)
            if (std::abs (r.startBeat - 4.0) < 0.01)
                merged = r;

        check (merged && merged->takeCount == 0 && merged->notes.size() == 3, "merged: one region with every pass's notes");
        engine.undo();
        pumpMessages();

        // As takes, over a region that was there: it becomes the first take.
        tracks.regions().createChordRegion (keys, 0.0);   // bars 1 to 4
        engine.setMidiCycleTakes (true);
        recordPasses ({ { 60 }, { 64, 67 } });

        regions = regionsOn (keys);
        std::optional<RegionInfo> cycled;

        for (auto& r : regions)
            if (std::abs (r.startBeat - 4.0) < 0.01)
                cycled = r;

        check (cycled && cycled->takeCount == 3 && cycled->currentTake == 2 && cycled->notes.size() == 2,
               "as takes: one region, a take per pass, playing the newest");
        check (regionsOn (keys).size() == 4, "(the chord region is cut either side, as is the recovered one)");

        if (cycled)
        {
            takes.setCurrentTake (cycled->id, 0);
            pumpMessages();
            auto first = tracks.regions().getRegion (cycled->id);
            check (first && ! first->notes.empty() && first->notes.size() != 1, "the first take is what was recorded over");

            checkUndoRoundTrip (engine, "Choose Take", [&] { takes.setCurrentTake (cycled->id, 1); });
            check (tracks.regions().getRegion (cycled->id)->notes.size() == 1, "(the first pass)");

            recordPasses ({ { 72 } });
            auto again = tracks.regions().getRegion (cycled->id);
            check (again && again->takeCount == 4 && again->currentTake == 3, "recording the cycle again adds to the takes");

            checkUndoRoundTrip (engine, "Delete Unused Takes", [&] { takes.deleteUnusedTakes (cycled->id); });
            check (tracks.regions().getRegion (cycled->id)->takeCount == 1, "(one take left)");
            checkUndoRoundTrip (engine, "Flatten Takes", [&] { takes.flattenTakes (cycled->id); });
            auto flat = tracks.regions().getRegion (cycled->id);
            check (flat && flat->takeCount == 0 && flat->notes.size() == 1, "flattening keeps the current take's notes");
        }

        engine.setMidiCycleTakes (false);
        engine.setLooping (false);
    }
}

static void runLongRecordingSection()
{
    const auto minutes = juce::jmax (1, juce::SystemStats::getEnvironmentVariable ("IKAROS_LONG_TAKE_MINUTES", "20").getIntValue());
    std::cout << "A " << minutes << " minute take\n";

    AudioEngine engine { testSettings, false };
    TestAudioDevice::Settings settings;
    settings.blockSize = 512;
    TestAudioDevice device (engine, settings);
    const auto rate = (juce::int64) settings.sampleRate;
    auto& tracks = engine.getTracks();
    juce::MessageManager::getInstance()->runDispatchLoopUntil (2500);
    runAudio (device, 0.2);
    engine.setCountInEnabled (false);

    auto rec = tracks.addAudioTrack();
    tracks.inputs().setTrackInput (rec, tracks.inputs().getAudioInputs().front().id);
    tracks.setSelectedTrack (rec);
    tracks.inputs().setArmed (rec, true);
    runAudio (device, 0.3);

    // A 1 ms click half way through every second. (Not at the very start:
    // a take starts at the punch-in, part way into a block.)
    auto clicks = [&] (juce::int64 start, int num)
    {
        juce::AudioBuffer<float> b (settings.numInputs, num);
        b.clear();

        for (int i = 0; i < num; ++i)
            if ((start + i + rate / 2) % rate < rate / 1000)
                b.setSample (0, i, 0.5f);

        return b;
    };

    const auto takesBefore = listTakes (engine);
    engine.setPositionBeats (0.0);
    engine.toggleRecord();
    check (engine.getTransportState().recording, "recording");

    const auto startTime = juce::Time::getMillisecondCounterHiRes();
    double memoryAfterOneMinute = 0.0, memoryPeak = 0.0;
    const juce::int64 total = rate * 60 * minutes;
    const auto chunk = rate / 10;

    for (juce::int64 done = 0; done < total; done += chunk)
    {
        device.process (clicks (done, (int) chunk));

        // Let the engine's timers run, and the disk writer keep up (the
        // test runs much faster than real time).
        juce::MessageManager::getInstance()->runDispatchLoopUntil (1);

        if (done % (rate * 10) == 0)
            juce::Thread::sleep (5);

        if (done == rate * 60)
            memoryAfterOneMinute = residentMB();

        memoryPeak = juce::jmax (memoryPeak, done % (rate * 10) == 0 ? residentMB() : 0.0);
    }

    engine.toggleRecord();
    pumpMessages();
    const auto memoryAtEnd = residentMB();
    std::cout << "    (" << minutes << " min recorded in " << juce::roundToInt ((juce::Time::getMillisecondCounterHiRes() - startTime) / 1000.0)
              << " s; memory " << juce::roundToInt (memoryAfterOneMinute) << " MB after 1 min, peak "
              << juce::roundToInt (memoryPeak) << " MB, " << juce::roundToInt (memoryAtEnd) << " MB at the end)\n";

    check (! engine.getTransportState().recording, "recording stops");

    auto report = engine.takeRecordingReport();
    check (report && report->reason == RecordingStopReason::user && report->dropouts == 0, "with no dropouts");

    auto take = newTake (engine, takesBefore);
    auto found = findClicks (take);
    check (std::abs (found.length - total) <= settings.blockSize, "the take is as long as what was played");
    check ((juce::int64) found.starts.size() == 60 * minutes, "it holds every click");

    juce::int64 worstDrift = 0;

    for (size_t i = 0; i < found.starts.size(); ++i)
        worstDrift = juce::jmax (worstDrift, std::abs (found.starts[i] - found.starts[0] - (juce::int64) i * rate));

    check (worstDrift == 0, "exactly a second apart all the way through (no drift)");

    if (memoryAfterOneMinute > 0.0)
        check (memoryPeak - memoryAfterOneMinute < 64.0, "memory stays flat while recording");

    take.deleteFile();
}

// The app-wide settings: the store itself (no engine needed), then what the
// engine does with them.
// Root-mean-square level of the first channel (or of part of it).
static float rmsOf (const juce::AudioBuffer<float>& b, int start = 0, int num = -1)
{
    if (num < 0)
        num = b.getNumSamples() - start;

    return num > 0 ? b.getRMSLevel (0, start, num) : 0.0f;
}

// The built-in effects (Limiter, Noise Gate, Distortion, Tremolo), heard
// through the pretend audio interface: a monitored audio track plays the
// test's tone through its effects and the master's.
static void runEffectsSection (const juce::File& dir)
{
    AudioEngine engine { testSettings, false };
    auto device = std::make_unique<TestAudioDevice> (engine, TestAudioDevice::Settings {});
    const auto rate = device->getSettings().sampleRate;
    auto& tracks = engine.getTracks();
    auto& effects = tracks.effects();
    juce::MessageManager::getInstance()->runDispatchLoopUntil (2500);
    runAudio (*device, 0.2);

    std::cout << "Built-in effects\n";
    {
        auto available = effects.getAvailableEffects();
        auto offered = [&] (const juce::String& name, const juce::String& group)
        {
            return std::any_of (available.begin(), available.end(),
                                [&] (const EffectType& e) { return e.name == name && e.category == group && ! e.isThirdParty; });
        };

        check (offered ("Limiter", "Dynamics") && offered ("Noise Gate", "Dynamics")
                 && offered ("Distortion", "Distortion") && offered ("Tremolo", "Modulation"),
               "Limiter, Noise Gate, Distortion and Tremolo are in the effects menu");
    }

    std::cout << "Master limiter\n";
    auto master = effects.getEffectChain (masterTrackId);
    check (master.size() == 1 && master[0].name == "Limiter" && master[0].enabled, "a new session has a limiter on the master");

    if (master.empty())
        return;

    const auto limiter = master[0].id;
    {
        auto params = effects.getEffectParameters (masterTrackId, limiter);
        check (params.size() == 3 && params[0].name == "Gain" && params[1].name == "Ceiling" && params[2].name == "Release",
               "it has Gain, Ceiling and Release knobs");
        check (params.size() == 3 && params[1].valueText == juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x92")) + "1.0 dB",
               "its ceiling is -1 dB");
        check (! engine.canUndo() && ! engine.hasUnsavedChanges(), "adding it isn't an undo step or a change to save");
    }

    // A monitored audio track: what goes into Input 1 comes out through its
    // effects and the master's.
    auto inputs = tracks.inputs().getAudioInputs();
    check (! inputs.empty(), "(the pretend device has inputs)");

    if (inputs.empty())
        return;

    auto t = tracks.addAudioTrack();
    tracks.inputs().setTrackInput (t, inputs[0].id);
    tracks.setSelectedTrack (t);
    tracks.inputs().setArmed (t, true);
    tracks.inputs().setMonitoring (t, true);
    runAudio (*device, 0.3);

    // Plays a tone for a moment (so the effects settle), then returns what
    // comes out over the next `seconds`.
    auto listen = [&] (float level, bool square = false, double seconds = 0.5)
    {
        runAudio (*device, 0.4, tone (*device, level, square));
        return device->process (tone (*device, level, square) (0, (int) (rate * seconds)));
    };

    const auto quietLimited = peakOf (listen (0.25f));
    effects.setEffectEnabled (masterTrackId, limiter, false);
    const auto quietPlain = peakOf (listen (0.25f));
    check (quietPlain > 0.05f && std::abs (quietLimited - quietPlain) < quietPlain * 0.01f,
           "the limiter leaves audio under its ceiling alone");

    effects.setEffectEnabled (masterTrackId, limiter, true);
    engine.beginUndoStep ("Limiter Gain");
    effects.setEffectParameter (masterTrackId, limiter, 0, 1.0f);   // +12 dB into it
    const auto loud = peakOf (listen (1.0f, true));
    std::cout << "    (a full-scale square, 12 dB up, peaks at " << loud << ")\n";
    // The master fader comes after the master's effects (it starts at -3 dB).
    const auto ceiling = juce::Decibels::decibelsToGain (-1.0f + tracks.getMasterVolumeDb());
    check (loud <= ceiling * 1.001f && loud > ceiling * 0.95f, "a loud signal is held at the -1 dB ceiling");
    engine.undo();
    check (effects.getEffectParameters (masterTrackId, limiter)[0].value == 0.0f, "undo turns its gain back down");

    std::cout << "Noise gate\n";
    {
        const float hiss = juce::Decibels::decibelsToGain (-70.0f);
        const auto hissPlain = peakOf (listen (hiss));

        check (! effects.getNoiseGate (t).exists, "a track starts without a noise gate");
        check (effects.setNoiseGateOn (t, true), "the inspector can turn one on");
        auto gate = effects.getNoiseGate (t);
        auto chain = effects.getEffectChain (t);
        check (gate.exists && gate.on && ! chain.empty() && chain[0].name == "Noise Gate" && chain[0].id == gate.pluginId,
               "which adds a Noise Gate at the top of the chain");

        const auto hissGated = peakOf (listen (hiss));
        std::cout << "    (-70 dB hiss peaks at " << hissPlain << " without the gate, " << hissGated << " with it)\n";
        check (hissGated < hissPlain * 0.1f, "it silences sound under its threshold");
        check (std::abs (peakOf (listen (0.25f)) - quietPlain) < quietPlain * 0.05f, "and lets louder sound through");

        engine.beginUndoStep ("Noise Gate Threshold");
        effects.setNoiseGateThreshold (t, 0.0f);   // -80 dB
        check (peakOf (listen (hiss)) > hissPlain * 0.5f, "a lower threshold lets quieter sound through");
        engine.undo();
        check (effects.getNoiseGate (t).thresholdText == juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x92")) + "50.0 dB",
               "undo puts the threshold back");

        effects.setNoiseGateOn (t, false);
        gate = effects.getNoiseGate (t);
        check (gate.exists && ! gate.on && effects.getEffectChain (t).size() == 1, "turning it off bypasses it (it stays in the chain)");
        check (peakOf (listen (hiss)) > hissPlain * 0.5f, "and the quiet sound comes back");
        engine.undo();
        check (effects.getNoiseGate (t).on, "undo turns it back on");
        effects.removeEffect (t, gate.pluginId);
    }

    std::cout << "Distortion\n";
    {
        // A sine's peak is 1.41x its RMS; saturation flattens the peaks.
        const auto plain = listen (0.25f);
        const auto distortion = effects.addEffect (t, "ikarosDistortion");
        auto params = effects.getEffectParameters (t, distortion);
        check (params.size() == 3 && params[0].name == "Drive" && params[1].name == "Tone" && params[2].name == "Output",
               "it has Drive, Tone and Output knobs");

        const auto driven = listen (0.25f);
        const auto crestPlain = peakOf (plain) / rmsOf (plain), crestDriven = peakOf (driven) / rmsOf (driven);
        std::cout << "    (peak/RMS " << crestPlain << " clean, " << crestDriven << " driven; peak "
                  << peakOf (plain) << " -> " << peakOf (driven) << ")\n";
        check (crestDriven < crestPlain - 0.1f, "it changes the shape of the sound");
        check (peakOf (driven) > peakOf (plain) * 0.5f && peakOf (driven) < peakOf (plain) * 2.5f,
               "without making it much louder or quieter");
        effects.removeEffect (t, distortion);
    }

    std::cout << "Tremolo\n";
    {
        const auto tremolo = effects.addEffect (t, "ikarosTremolo");
        auto params = effects.getEffectParameters (t, tremolo);
        check (params.size() == 3 && params[0].name == "Rate" && params[0].valueText == "5.0 Hz"
                 && params[1].name == "Depth" && params[2].name == "Shape",
               "it has Rate, Depth and Shape knobs (5 Hz to start with)");

        // The level in 10 ms slices over a second (five pulses).
        const auto heard = listen (0.25f, false, 1.0);
        const int slice = (int) (rate * 0.01);
        float lowest = 1.0f, highest = 0.0f;

        for (int start = 0; start + slice <= heard.getNumSamples(); start += slice)
        {
            lowest = juce::jmin (lowest, rmsOf (heard, start, slice));
            highest = juce::jmax (highest, rmsOf (heard, start, slice));
        }

        std::cout << "    (level swings " << lowest << " to " << highest << ")\n";
        check (highest > lowest * 1.6f && highest < lowest * 2.5f, "the level pulses (down by about half at 50% depth)");
        effects.removeEffect (t, tremolo);
    }

    std::cout << "Saving effect settings\n";
    {
        const auto distortion = effects.addEffect (t, "ikarosDistortion");
        const auto tremolo = effects.addEffect (t, "ikarosTremolo");
        effects.addEffect (t, "ikarosLimiter");
        effects.setNoiseGateOn (t, true);
        engine.beginUndoStep ("Drive");
        effects.setEffectParameter (t, distortion, 0, 0.8f);
        engine.beginUndoStep ("Rate");
        effects.setEffectParameter (t, tremolo, 0, 1.0f);
        engine.beginUndoStep ("Noise Gate Threshold");
        effects.setNoiseGateThreshold (t, 0.25f);
        pumpMessages();

        auto session = dir.getChildFile ("Effects");
        check (engine.saveSessionAs (session), "(save)");
        check (engine.openSession (AudioEngine::getSessionFileFor (session)), "(reopen)");
        pumpMessages();

        t = tracks.getTracks().back().id;
        auto chain = effects.getEffectChain (t);
        check (chain.size() == 4 && chain[0].name == "Noise Gate" && chain[1].name == "Distortion"
                 && chain[2].name == "Tremolo" && chain[3].name == "Limiter",
               "the new effects survive save and reopen");

        if (chain.size() == 4)
        {
            check (std::abs (effects.getEffectParameters (t, chain[1].id)[0].value - 0.8f) < 0.01f
                     && effects.getEffectParameters (t, chain[2].id)[0].valueText == "15 Hz"
                     && std::abs (effects.getNoiseGate (t).threshold - 0.25f) < 0.01f,
                   "and so do their settings");
        }

        check (effects.getEffectChain (masterTrackId).size() == 1, "and the master limiter");

        // Sessions from before the default limiter don't get one when they open.
        effects.removeEffect (masterTrackId, effects.getEffectChain (masterTrackId)[0].id);
        check (engine.saveSession(), "(save)");
        check (engine.openSession (AudioEngine::getSessionFileFor (session)), "(reopen)");
        pumpMessages();
        check (effects.getEffectChain (masterTrackId).empty(), "a session saved without a master limiter opens without one");

        engine.newSession ({ SessionTemplate::voice, 120.0, 4, 4, 0, false });
        juce::MessageManager::getInstance()->runDispatchLoopUntil (2500);
        master = effects.getEffectChain (masterTrackId);
        check (master.size() == 1 && master[0].name == "Limiter", "every new session (from any template) starts with one");
        check (! engine.canUndo(), "(and it's not an undo step there either)");
    }

    device.reset();
}

//==============================================================================
// The window's view of a session (see SessionViews): saved in the bundle,
// and remembered on this computer when the session closes, saved or not.
static void runViewsSection (const juce::File& dir)
{
    const auto songA = dir.getChildFile ("Views A.iksession"), songB = dir.getChildFile ("Views B.iksession");

    // The UI's view, reduced to one number.
    int shown = 1;
    const auto describe = [&shown]
    {
        juce::ValueTree v (SessionViews::viewType);
        v.setProperty ("x", shown, nullptr);
        return v;
    };
    const auto viewOf = [] (AudioEngine& e) { return (int) e.getViewState().getProperty ("x", 0); };

    {
        AudioEngine engine { testSettings };
        juce::MessageManager::getInstance()->runDispatchLoopUntil (2500);
        engine.newSession();
        pumpMessages();
        engine.setViewStateSource (describe);
        const auto firstView = engine.getViewState();
        check (firstView.getNumProperties() == 0, "a new session has no view of its own");
        engine.newSession();
        pumpMessages();
        check (engine.getViewState() != firstView, "each new session has a view tree of its own");

        check (engine.saveSessionAs (songA), "(save)");
        check (juce::ZipFile (songA).getEntry (SessionBundle::viewEntry) != nullptr, "the bundle holds the view");

        check (engine.openSession (songA), "(reopen)");
        pumpMessages();
        check (viewOf (engine) == 1, "a saved session opens with its view");

        // Change the view only, and close: no save needed, nothing unsaved.
        shown = 2;
        check (! engine.hasUnsavedChanges(), "a view change isn't an unsaved change");
        engine.newSession();
        pumpMessages();
        check (engine.openSession (songA), "(reopen)");
        pumpMessages();
        check (viewOf (engine) == 2, "a view changed without saving is remembered when the session closes");

        // Save As: the copy has the view, the original keeps its own.
        shown = 3;
        check (engine.saveSessionAs (songB), "(save as)");
        check (engine.openSession (songA), "(open the original)");
        pumpMessages();
        check (viewOf (engine) == 2, "Save As leaves the original's view alone");
        check (engine.openSession (songB), "(open the copy)");
        pumpMessages();
        check (viewOf (engine) == 3, "Save As keeps the view under the new name");

        engine.setViewStateSource (nullptr);
    }

    // Another computer (settings of its own) only has the view in the file.
    {
        AudioEngine engine { testSettings + "Other" };
        juce::MessageManager::getInstance()->runDispatchLoopUntil (2500);
        check (engine.openSession (songA), "(open elsewhere)");
        pumpMessages();
        check (viewOf (engine) == 1, "another computer opens the view saved in the file");

        shown = 7;
        engine.setViewStateSource (describe);
        check (engine.saveSession(), "(save elsewhere)");
        engine.setViewStateSource (nullptr);
    }

    // Back here: the file's view is newer than the one remembered.
    {
        AudioEngine engine { testSettings };
        juce::MessageManager::getInstance()->runDispatchLoopUntil (2500);
        check (engine.openSession (songA), "(open back here)");
        pumpMessages();
        check (viewOf (engine) == 7, "a newer view in the file wins over the one remembered");
    }
}

static void runPreferencesSection (const juce::File& dir)
{
    auto folder = dir.getChildFile ("Preferences");
    folder.createDirectory();
    auto file = folder.getChildFile ("Preferences.xml");

    std::cout << "Preferences: the store\n";
    {
        Preferences prefs (file);
        check (prefs.get (settings::onStartup) == settings::OnStartup::sessionChooser
                 && prefs.get (settings::sessionFolder).isEmpty()
                 && prefs.get (settings::keyboardVelocity) == settings::Velocity::normal
                 && ! prefs.get (settings::monitoringWarningShown),
               "General and Audio & MIDI start with their defaults");
        check (prefs.get (settings::countIn) == 0 && ! prefs.get (settings::midiCycleTakes) && prefs.get (settings::autoArmTrack),
               "Recording starts with no count-in, merged MIDI passes and auto-arm on (today's behaviour)");
        check (prefs.get (settings::snapEnabled) && prefs.get (settings::snapGrid) == settings::Grid::beat
                 && prefs.get (settings::followPlayhead) && prefs.get (settings::wheelMode) == settings::Wheel::zoomWithModifier,
               "Editing starts with snap to beats, following the playhead and Ctrl-wheel zoom (today's behaviour)");
        check (prefs.get (settings::userSoundsFolder).isEmpty() && prefs.getList (settings::pluginFolders).isEmpty()
                 && prefs.getList (settings::disabledPluginFormats).isEmpty(),
               "no extra folders, and every plug-in format is on");
        check (prefs.get (settings::exportFormat) == "wav" && prefs.get (settings::exportQuality ("wav")) == 24
                 && prefs.get (settings::exportQuality ("mp3")) == 320 && prefs.get (settings::exportQuality ("ogg")) == 192
                 && ! prefs.get (settings::exportNormalize) && prefs.get (settings::interfaceScale) == 100
                 && prefs.getInterfaceScale() == 1.0f,
               "Export and Appearance start with their defaults");

        int heard = 0;
        struct Counter : Preferences::Listener
        {
            int& count;
            juce::String lastKey;
            explicit Counter (int& c) : count (c) {}
            void preferenceChanged (const juce::String& key) override   { ++count; lastKey = key; }
        } listener (heard);
        prefs.addListener (&listener);

        prefs.set (settings::onStartup, settings::OnStartup::reopenLastSession);
        check (heard == 1 && listener.lastKey == settings::onStartup.key, "a change is announced at once, with its key");
        prefs.removeListener (&listener);

        prefs.set (settings::sessionFolder, folder.getFullPathName());
        prefs.set (settings::keyboardVelocity, settings::Velocity::hard);
        prefs.set (settings::monitoringWarningShown, true);
        prefs.set (settings::countIn, 2);
        prefs.set (settings::midiCycleTakes, true);
        prefs.set (settings::autoArmTrack, false);
        prefs.set (settings::snapEnabled, false);
        prefs.set (settings::snapGrid, settings::Grid::bar);
        prefs.set (settings::followPlayhead, false);
        prefs.set (settings::wheelMode, settings::Wheel::scroll);
        prefs.setList (settings::pluginFolders, { "/plugins/a", "/plugins/b" });
        prefs.setList (settings::disabledPluginFormats, { "LV2" });
        prefs.set (settings::exportFormat, "flac");
        prefs.set (settings::exportQuality ("flac"), 16);
        prefs.set (settings::exportNormalize, true);
        prefs.set (settings::interfaceScale, 150);
    }

    {
        Preferences prefs (file);   // the file as the first one left it
        check (prefs.get (settings::onStartup) == settings::OnStartup::reopenLastSession
                 && prefs.getFolder (settings::sessionFolder, {}) == folder
                 && prefs.get (settings::keyboardVelocity) == settings::Velocity::hard
                 && prefs.get (settings::monitoringWarningShown),
               "General and Audio & MIDI settings round-trip through the file");
        check (prefs.get (settings::countIn) == 2 && prefs.get (settings::midiCycleTakes) && ! prefs.get (settings::autoArmTrack),
               "Recording settings round-trip");
        check (! prefs.get (settings::snapEnabled) && prefs.get (settings::snapGrid) == settings::Grid::bar
                 && ! prefs.get (settings::followPlayhead) && prefs.get (settings::wheelMode) == settings::Wheel::scroll,
               "Editing settings round-trip");
        check (prefs.getList (settings::pluginFolders) == juce::StringArray ({ "/plugins/a", "/plugins/b" })
                 && prefs.getList (settings::disabledPluginFormats) == juce::StringArray ({ "LV2" }),
               "lists round-trip");
        check (prefs.get (settings::exportFormat) == "flac" && prefs.get (settings::exportQuality ("flac")) == 16
                 && prefs.get (settings::exportNormalize) && prefs.getInterfaceScale() == 1.5f,
               "Export and Appearance settings round-trip");
        check (prefs.getFolder (settings::userSoundsFolder, folder) == folder
                 && prefs.getFolder (settings::sessionFolder, {}) != juce::File(),
               "a folder setting that's empty (or gone) falls back");

        int heard = 0;
        struct Counter : Preferences::Listener
        {
            int& count;
            juce::String lastKey = "unset";
            explicit Counter (int& c) : count (c) {}
            void preferenceChanged (const juce::String& key) override   { ++count; lastKey = key; }
        } listener (heard);
        prefs.addListener (&listener);

        prefs.resetAll();
        check (heard == 1 && listener.lastKey.isEmpty(), "resetting tells listeners everything changed");
        prefs.removeListener (&listener);

        check (prefs.get (settings::onStartup) == settings::OnStartup::sessionChooser && prefs.get (settings::autoArmTrack)
                 && prefs.get (settings::snapEnabled) && prefs.get (settings::wheelMode) == settings::Wheel::zoomWithModifier
                 && prefs.getList (settings::pluginFolders).isEmpty() && prefs.get (settings::exportFormat) == "wav"
                 && prefs.get (settings::exportQuality ("flac")) == 24 && prefs.get (settings::interfaceScale) == 100,
               "reset puts every setting back to its default");
    }

    {
        Preferences prefs (file);
        check (prefs.get (settings::exportFormat) == "wav" && prefs.get (settings::countIn) == 0 && prefs.get (settings::autoArmTrack),
               "and the reset is what was saved");
    }

    {
        // A file from before most settings existed: only a few keys.
        auto oldFile = folder.getChildFile ("Old.xml");
        oldFile.replaceWithText ("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n\n<PROPERTIES>\n"
                                 "  <VALUE name=\"exportFormat\" val=\"ogg\"/>\n"
                                 "  <VALUE name=\"midiCycleTakes\" val=\"1\"/>\n"
                                 "</PROPERTIES>\n");
        Preferences prefs (oldFile);
        check (prefs.get (settings::exportFormat) == "ogg" && prefs.get (settings::midiCycleTakes),
               "an old settings file keeps the values it has");
        check (prefs.get (settings::onStartup) == settings::OnStartup::sessionChooser && prefs.get (settings::autoArmTrack)
                 && prefs.get (settings::snapEnabled) && prefs.get (settings::followPlayhead)
                 && prefs.get (settings::keyboardVelocity) == settings::Velocity::normal
                 && prefs.get (settings::wheelMode) == settings::Wheel::zoomWithModifier
                 && prefs.get (settings::exportQuality ("ogg")) == 192 && prefs.get (settings::interfaceScale) == 100,
               "and has the defaults for the keys it lacks");
    }

    std::cout << "Preferences: settings from the older settings file\n";
    {
        juce::PropertiesFile::Options options;
        options.storageFormat = juce::PropertiesFile::storeAsXML;
        juce::PropertiesFile legacy (folder.getChildFile ("Settings.xml"), options);
        legacy.setValue ("countInMode", 2);
        legacy.setValue ("midiCycleTakes", true);
        legacy.setValue ("monitoringWarningShown", true);
        legacy.setValue ("exportFormat", "flac");
        legacy.setValue ("exportQuality.flac", 16);
        legacy.setValue ("exportNormalize", true);
        legacy.setValue ("exportFolder", folder.getFullPathName());

        auto migrated = folder.getChildFile ("Migrated.xml");
        {
            Preferences prefs (migrated, &legacy);
            check (prefs.get (settings::countIn) == 2 && prefs.get (settings::midiCycleTakes)
                     && prefs.get (settings::monitoringWarningShown),
                   "the first time, an existing user's choices are copied over (count-in, MIDI loops, the warning)");
            check (prefs.get (settings::exportFormat) == "flac" && prefs.get (settings::exportQuality ("flac")) == 16
                     && prefs.get (settings::exportNormalize) && prefs.getFolder (settings::exportFolder, {}) == folder,
                   "and the Export window's remembered choices");
            check (prefs.get (settings::autoArmTrack) && prefs.get (settings::snapEnabled),
                   "settings that didn't exist before have their defaults");
        }

        legacy.setValue ("exportFormat", "ogg");
        Preferences again (migrated, &legacy);
        check (again.get (settings::exportFormat) == "flac", "later, the older file isn't read again");

        again.resetAll();
        Preferences afterReset (migrated, &legacy);
        check (afterReset.get (settings::exportFormat) == "wav" && afterReset.get (settings::countIn) == 0,
               "and a reset isn't undone by it");
    }

    std::cout << "Preferences: what the engine does\n";
    {
        AudioEngine engine { testSettings, false };
        TestAudioDevice::Settings deviceSettings;
        auto device = std::make_unique<TestAudioDevice> (engine, deviceSettings);
        auto& tracks = engine.getTracks();
        auto& prefs = engine.getPreferences();
        juce::MessageManager::getInstance()->runDispatchLoopUntil (2500);
        runAudio (*device, 0.2);

        prefs.set (settings::countIn, 2);
        check (engine.getCountInBars() == 2 && engine.getTransportState().countIn, "the count-in setting is the session's count-in");
        engine.setCountInBars (1);
        check (prefs.get (settings::countIn) == 1, "and the toolbar's change is the setting");
        prefs.resetAll();
        check (engine.getCountInBars() == 0 && ! engine.getTransportState().countIn, "resetting turns the count-in off");

        engine.setMidiCycleTakes (true);
        check (prefs.get (settings::midiCycleTakes) && engine.getMidiCycleTakes(), "MIDI loop recording uses the setting");
        engine.setMidiCycleTakes (false);

        check (engine.getPlugins().getFormatNames().contains ("VST3"), "the plug-in formats are listed");

        {
            check (! engine.getSounds().findPack ("ikaros-studio.test").has_value(), "the test sound pack isn't in the library at first");
            prefs.set (settings::userSoundsFolder, juce::File (IKAROS_TEST_FIXTURES).getFullPathName());
            check (engine.getSounds().findPack ("ikaros-studio.test").has_value()
                     && engine.getPackStore().getInstallFolder() == juce::File (IKAROS_TEST_FIXTURES),
                   "choosing a sound pack folder loads the packs in it, and downloads go there");
            prefs.set (settings::userSoundsFolder, "");
        }

        std::cout << "Audio device changes\n";
        {
            const juce::String scarlett = "Scarlett 2i2 USB, USB Audio; Direct hardware device without any conversions";
            const juce::String headphones = "USB Headphones, USB Audio; Direct hardware device without any conversions";

            AudioDeviceState before;
            before.inputs = { "Default ALSA Input" };
            before.outputs = { "Default ALSA Output" };
            before.inUseInput = before.defaultInput = "Default ALSA Input";
            before.inUseOutput = before.defaultOutput = "Default ALSA Output";

            check (! findDeviceChange (before, before), "no change, nothing to ask");

            auto usb = before;
            usb.inputs.add (scarlett);
            usb.outputs.add (scarlett);
            auto prompt = findDeviceChange (before, usb);
            check (prompt && prompt->kind == AudioDevicePrompt::Kind::appeared && prompt->deviceName == "Scarlett 2i2 USB"
                     && prompt->inputName == scarlett && prompt->outputName == scarlett,
                   "an interface that appears is offered for both recording and playback, under its short name");

            auto modes = before;
            const juce::String front = "MiniFuse 2, USB Audio; Front output / input";
            modes.outputs.addArray ({ "MiniFuse 2, USB Audio; Surround", front });
            modes.inputs.add (front);
            prompt = findDeviceChange (before, modes);
            check (prompt && prompt->inputName == front && prompt->outputName == front,
                   "of a card's modes, the one with both inputs and outputs is offered");

            auto dac = before;
            dac.outputs.add (headphones);
            prompt = findDeviceChange (before, dac);
            check (prompt && prompt->inputName.isEmpty() && prompt->outputName == headphones,
                   "headphones are offered for playback only");

            auto mixed = before;
            mixed.inputs.add (scarlett);
            mixed.outputs.add (headphones);
            prompt = findDeviceChange (before, mixed);
            check (prompt && prompt->inputName.isEmpty() && prompt->outputName == headphones,
                   "an input and an output of different devices at once: one question, about the output");

            auto alreadyUsed = usb;
            alreadyUsed.inUseInput = alreadyUsed.inUseOutput = scarlett;
            check (! findDeviceChange (before, alreadyUsed), "a device that's already in use isn't offered");

            auto inUse = usb;
            inUse.inUseInput = inUse.inUseOutput = scarlett;

            auto unplugged = before;
            unplugged.inUseInput = unplugged.inUseOutput = scarlett;   // JUCE on Linux keeps the old name
            prompt = findDeviceChange (inUse, unplugged);
            check (prompt && prompt->kind == AudioDevicePrompt::Kind::disappeared && prompt->deviceName == "Scarlett 2i2 USB"
                     && prompt->inputName == "Default ALSA Input" && prompt->outputName == "Default ALSA Output"
                     && prompt->useName == "Default ALSA Output",
                   "the device in use going away offers the default one");

            check (! findDeviceChange (inUse, before), "if the system already moved to the default device, there's nothing to offer");

            // The watcher, with the lists acted out.
            auto& watcher = engine.getDeviceWatcher();
            auto shown = before;
            watcher.setStateSource ([&shown] { return shown; });
            watcher.check();
            check (! engine.takeAudioDevicePrompt(), "the first look only notes the devices");

            shown = usb;
            watcher.check();
            prompt = engine.takeAudioDevicePrompt();
            check (prompt && prompt->deviceName == "Scarlett 2i2 USB", "a device plugged in is offered");
            check (! engine.takeAudioDevicePrompt(), "a prompt comes once");
            watcher.check();
            check (! engine.takeAudioDevicePrompt(), "and the same device isn't offered again");

            prefs.set (settings::deviceChange, settings::DeviceChange::neverAsk);
            shown = before;
            watcher.check();
            shown = usb;
            watcher.check();
            check (! engine.takeAudioDevicePrompt(), "with \"never ask\", it stays quiet");

            prefs.set (settings::deviceChange, settings::DeviceChange::ask);
            shown = before;
            watcher.check();
            shown = usb;
            watcher.check();
            check (engine.takeAudioDevicePrompt().has_value(), "and asks again when the setting is back");

            watcher.setStateSource ({});
        }

        engine.setBpm (120.0);
        engine.setMetronomeEnabled (false);

        auto inputs = tracks.inputs().getAudioInputs();
        check (! inputs.empty(), "the pretend device has inputs");

        if (inputs.empty())
            return;

        const auto first = tracks.getTracks().front().id;
        auto rec = tracks.addAudioTrack();
        tracks.inputs().setTrackInput (rec, inputs[0].id);
        runAudio (*device, 0.3);

        auto regionCount = [&] { return tracks.regions().getRegions (rec).size(); };
        double nextTakeBeat = 8.0;

        auto record = [&] (double seconds)
        {
            engine.setPositionBeats (nextTakeBeat);
            nextTakeBeat += 20.0;
            engine.toggleRecord();
            const bool started = engine.getTransportState().recording;

            if (started)
            {
                runAudio (*device, seconds, tone (*device, 0.3f));
                engine.toggleRecord();
            }

            return started;
        };

        std::cout << "Arming (auto-arm on, the default)\n";
        {
            check (prefs.get (settings::autoArmTrack), "it's on by default");
            tracks.setSelectedTrack (first);
            tracks.setSelectedTrack (rec);
            check (tracks.inputs().getArmedTrack() == rec, "selecting a track arms it");
            tracks.inputs().setArmed (rec, false);

            const auto before = regionCount();
            engine.setPositionBeats (nextTakeBeat);
            nextTakeBeat += 20.0;
            engine.toggleRecord();
            check (engine.getTransportState().recording && tracks.inputs().getArmedTrack() == rec,
                   "Record arms the selected track for the take");
            runAudio (*device, 1.0, tone (*device, 0.3f));
            engine.toggleRecord();
            check (regionCount() == before + 1 && tracks.inputs().getArmedTrack() == 0,
                   "and disarms it again afterwards");
            engine.takeRecordingReport();
        }

        std::cout << "Arming (auto-arm off)\n";
        {
            prefs.set (settings::autoArmTrack, false);
            tracks.setSelectedTrack (first);
            tracks.setSelectedTrack (rec);
            check (tracks.inputs().getArmedTrack() == 0, "selecting a track doesn't arm it");

            const auto before = regionCount();
            check (! record (1.0), "Record with no track armed doesn't start recording");
            auto report = engine.takeRecordingReport();
            check (report && report->reason == RecordingStopReason::noTrackArmed, "the report says no track is armed");
            check (regionCount() == before && tracks.inputs().getArmedTrack() == 0, "nothing was recorded, and nothing got armed");

            tracks.inputs().setArmed (rec, true);
            check (record (1.0), "with a track armed by hand, Record starts");
            check (regionCount() == before + 1, "and records onto it");
            check (tracks.inputs().getArmedTrack() == rec, "which is still armed afterwards");
            engine.takeRecordingReport();

            // Turning it back on goes back to the old behaviour.
            tracks.inputs().setArmed (rec, false);
            prefs.set (settings::autoArmTrack, true);
            check (record (1.0) && regionCount() == before + 2 && tracks.inputs().getArmedTrack() == 0,
                   "with it on again, Record arms the selected track for its take, as before");
            engine.takeRecordingReport();
        }

        prefs.resetAll();
    }

    std::cout << "Preferences: kept between runs\n";
    {
        {
            AudioEngine engine { testSettings, false };
            engine.getPreferences().set (settings::exportFormat, "mp3");
            engine.setCountInBars (2);
        }

        AudioEngine engine { testSettings, false };
        check (engine.getPreferences().get (settings::exportFormat) == "mp3" && engine.getCountInBars() == 2
                 && engine.getTransportState().countIn,
               "the engine's settings are there next time (and the count-in is on)");
        engine.getPreferences().resetAll();
    }
}

//==============================================================================
// Tempo and time-signature changes (the tempo lane).
static void runTempoSection (const juce::File& dir)
{
    std::cout << "Tempo map\n";
    {
        const TempoMap map ({ { 0.0, 120.0 }, { 16.0, 90.0 } }, { { 0.0, 0, 4, 4 }, { 16.0, 0, 3, 4 } });
        check (map.getTimeSignatureChanges()[1].bar == 4, "a 3/4 change at beat 16 starts the 5th bar");
        check (map.barToBeat (6) == 22.0 && map.barAt (21.9) == 5 && map.barAt (22.0) == 6 && map.beatsPerBarAt (20.0) == 3.0,
               "bars after it are 3 beats long");
        check (map.nearestBarLine (17.4) == 16.0 && map.nearestBarLine (17.6) == 19.0 && map.nearestBar (17.6) == 5,
               "the nearest bar line");
        check (map.tempoAt (15.9).bpm == 120.0 && map.tempoAt (16.0).bpm == 90.0, "the tempo in effect at a beat");

        int bars = 0;
        map.forEachBar (0.0, 22.0, 1, [&] (int, double) { ++bars; });
        check (bars == 7, "every bar line is visited");

        const TempoMap odd ({}, { { 0.0, 0, 4, 4 }, { 18.0, 0, 3, 4 } });
        check (odd.getTimeSignatureChanges()[1].beat == 20.0 && odd.getTimeSignatureChanges()[1].bar == 5,
               "a time signature off a bar line starts at the next one, as in Tracktion");
    }

    AudioEngine engine { testSettings };
    auto& tracks = engine.getTracks();
    auto& regions = tracks.regions();
    juce::MessageManager::getInstance()->runDispatchLoopUntil (2500);
    engine.newSession();
    pumpMessages();

    const auto midi = tracks.getTracks().front().id;
    const auto audio = tracks.addAudioTrack();
    engine.setBpm (120.0);              // 2 beats a second

    // A chord region at bar 9, a loop (auto-tempo) at bar 5 and a plain
    // 2-second file (a recording, as far as tempo goes) at bar 13.
    const auto chords = regions.createChordRegion (midi, 32.0);
    std::optional<LibraryFile> loop;

    for (auto& l : engine.getSounds().getLoops())
        if (l.name == "Glass Chords")
            loop = l;

    const auto plainFile = dir.getChildFile ("Two Seconds.wav");
    writeClickWav (plainFile, 44100.0, 2.0, { 0.0 }, 0.5f);
    check (loop.has_value(), "(the bundled loop is there)");

    if (loop)
        regions.importAudioFiles (audio, { loop->file.getFullPathName() }, 16.0);

    regions.importAudioFiles (audio, { plainFile.getFullPathName() }, 48.0);
    pumpMessages();

    auto regionAt = [&] (TrackId track, double startBeat) -> std::optional<RegionInfo>
    {
        for (auto& r : regions.getRegions (track))
            if (std::abs (r.startBeat - startBeat) < 0.001)
                return r;

        return std::nullopt;
    };

    auto secondsAt = [&] (double beat)
    {
        engine.setPositionBeats (beat);
        return engine.getTransportState().positionSeconds;
    };

    const auto loopBeats = loop ? regionAt (audio, 16.0)->lengthBeats : 0.0;
    check (std::abs (regionAt (audio, 48.0).value_or (RegionInfo {}).lengthBeats - 4.0) < 0.01, "(the 2-second file is 4 beats long at 120 BPM)");

    std::cout << "Tempo changes\n";
    checkUndoRoundTrip (engine, "Add Tempo Change", [&] { engine.addTempoChange (2, 60.0); });   // from bar 3 (beat 8)
    {
        const auto map = engine.getTempoMap();
        check (map.getTempoChanges().size() == 2 && map.getTempoChanges()[1].beat == 8.0 && map.getTempoChanges()[1].bpm == 60.0,
               "a tempo change goes on its bar line");
        check (std::abs (secondsAt (10.0) - 6.0) < 1.0e-6, "beat 10 is 6 seconds in (4 at 120 BPM, then 2 at 60)");
        check (engine.getTransportState().bpm == 60.0, "the LCD shows the tempo at the playhead");
        check (secondsAt (4.0) >= 0.0 && engine.getTransportState().bpm == 120.0, "...before the change too");

        auto chordRegion = regions.getRegion (chords);
        check (chordRegion && chordRegion->startBeat == 32.0 && chordRegion->lengthBeats == 16.0,
               "MIDI regions stay on their beats");
        check (! loop || std::abs (regionAt (audio, 16.0).value_or (RegionInfo {}).lengthBeats - loopBeats) < 0.01,
               "loops stay on their beats and follow the new tempo (the same length in beats)");
        check (std::abs (regionAt (audio, 48.0).value_or (RegionInfo {}).lengthBeats - 2.0) < 0.01,
               "a recording keeps its length in seconds (2 beats at 60 BPM)");
    }

    checkUndoRoundTrip (engine, "Tempo Change", [&]
    {
        engine.beginUndoStep ("Tempo Change");   // as the tempo lane does when a drag starts
        engine.setTempoChange (1, 12.0, 90.0);
    });
    check (! engine.setTempoChange (1, 0.0, 90.0) && engine.getTempoMap().getTempoChanges()[1].beat == 12.0,
           "a tempo change can't be moved onto or past its neighbour");

    engine.setPositionBeats (20.0);
    checkUndoRoundTrip (engine, "Tempo", [&] { engine.setBpm (100.0); });
    check (engine.getTempoMap().getTempoChanges()[0].bpm == 120.0 && engine.getTempoMap().getTempoChanges()[1].bpm == 100.0,
           "the LCD's tempo changes the tempo change at the playhead");
    checkUndoRoundTrip (engine, "Delete Tempo Change", [&] { engine.removeTempoChange (1); });
    check (engine.getTempoMap().getTempoChanges().size() == 1 && std::abs (secondsAt (10.0) - 5.0) < 1.0e-6,
           "deleting it puts the song back to one tempo");

    std::cout << "Time-signature changes\n";
    engine.addTempoChange (8, 100.0);   // bar 9 (beat 32)
    checkUndoRoundTrip (engine, "Add Time Signature Change", [&] { engine.addTimeSignatureChange (4, 3, 4); });
    {
        const auto map = engine.getTempoMap();
        check (map.getTimeSignatureChanges().size() == 2 && map.getTimeSignatureChanges()[1].beat == 16.0 && map.barToBeat (8) == 28.0,
               "a 3/4 change from bar 5 makes later bars 3 beats long");
        check (map.getTempoChanges()[1].beat == 32.0 && regions.getRegion (chords)->startBeat == 32.0,
               "tempo changes and regions stay on their beats");

        engine.setPositionBeats (17.0);
        const auto t = engine.getTransportState();
        check (t.timeSigNumerator == 3 && t.timeSigDenominator == 4 && t.bar == 5 && t.beat == 2,
               "the LCD shows the time signature, bar and beat at the playhead");
    }

    engine.addTimeSignatureChange (8, 4, 4);   // bar 9 (beat 28)
    checkUndoRoundTrip (engine, "Time Signature", [&] { engine.setTimeSignatureChange (1, 2, 4); });
    check (engine.getTempoMap().getTimeSignatureChanges()[2].bar == 8 && engine.getTempoMap().getTimeSignatureChanges()[2].beat == 24.0,
           "a later time-signature change stays on its bar when earlier bars get shorter");

    {
        const double before = secondsAt (60.0), beforeLoop = secondsAt (16.0);
        const auto temposBefore = engine.getTempoMap().getTempoChanges();
        checkUndoRoundTrip (engine, "Add Time Signature Change", [&] { engine.addTimeSignatureChange (10, 6, 8); });
        check (std::abs (secondsAt (60.0) - before) < 1.0e-6 && std::abs (secondsAt (16.0) - beforeLoop) < 1.0e-6,
               "a 6/8 change doesn't change when anything plays");
        check (engine.getTempoMap().getTempoChanges() == temposBefore, "...nor the tempo the lane shows");

        engine.setPositionBeats (engine.getTempoMap().barToBeat (10) + 1.0);
        check (engine.getTransportState().bpm == 100.0 && engine.getTransportState().timeSigDenominator == 8,
               "the LCD's tempo counts the eighth notes in 6/8");

        checkSaveAndReopen (engine, dir.getChildFile ("Tempo"));
        check (engine.getTempoMap().getTempoChanges() == temposBefore && engine.getTempoMap().getTimeSignatureChanges().size() == 4,
               "tempo and time-signature changes are saved");

        const int sixEight = engine.getTempoMap().indexOfSignatureAt (engine.getTempoMap().barToBeat (10));
        checkUndoRoundTrip (engine, "Delete Time Signature Change", [&] { engine.removeTimeSignatureChange (sixEight); });
        check (std::abs (secondsAt (60.0) - before) < 1.0e-6 && engine.getTempoMap().getTempoChanges() == temposBefore,
               "deleting it doesn't change when anything plays either");
    }

    std::cout << "Count-in\n";
    {
        // A bar of the time signature where recording starts (Tracktion only
        // looked at the first one until external/patches/tracktion-count-in-time-signature.patch).
        const auto map = engine.getTempoMap();
        engine.setCountInBars (1);
        const auto at = [&] (double beat) { return engine.getCountInBeatsAt (beat); };
        // 4/4, then 2/4 from bar 5 and 4/4 again from bar 9 (see above).
        check (at (0.0) == 4 && at (map.barToBeat (4)) == 2 && at (map.barToBeat (6)) == 2 && at (map.barToBeat (8)) == 4,
               "the count-in is a bar of the time signature where recording starts");
        engine.setCountInBars (0);
    }

    std::cout << "Chord regions and the loop range follow the bars\n";
    {
        const auto map = engine.getTempoMap();   // 4/4, 3/4 from bar 5, 2/4 from bar 6... (see above)
        const auto id = regions.createChordRegion (midi, map.barToBeat (5) + 0.5);
        auto r = regions.getRegion (id);
        check (r && r->startBeat == map.barToBeat (5) && std::abs (r->lengthBeats - (map.barToBeat (9) - map.barToBeat (5))) < 1.0e-6,
               "a chord region starts on its bar and is 4 bars long, whatever their lengths");
    }
}

//==============================================================================
// Arrangement markers: sections of the song, and moving, copying and
// deleting what's inside them on every track.
static void runSectionsSection (const juce::File& dir)
{
    AudioEngine engine { testSettings };
    auto& tracks = engine.getTracks();
    auto& regions = tracks.regions();
    auto& sections = tracks.sections();
    auto& automation = tracks.automation();
    juce::MessageManager::getInstance()->runDispatchLoopUntil (2500);
    engine.newSession();
    pumpMessages();
    engine.setBpm (120.0);   // 4/4, so a bar is 4 beats

    const auto midi = tracks.getTracks().front().id;
    const auto audio = tracks.addAudioTrack();
    check (sections.getSections().empty(), "a new session has no sections");

    auto sectionAt = [&] (double startBeat) -> std::optional<SectionInfo>
    {
        for (auto& s : sections.getSections())
            if (std::abs (s.startBeat - startBeat) < 1.0e-6)
                return s;

        return std::nullopt;
    };

    auto regionStarts = [&] (TrackId track)
    {
        std::vector<std::pair<double, double>> result;   // start and end, in song order

        for (auto& r : regions.getRegions (track))
            result.push_back ({ std::round (r.startBeat * 1000.0) / 1000.0, std::round ((r.startBeat + r.lengthBeats) * 1000.0) / 1000.0 });

        std::sort (result.begin(), result.end());
        return result;
    };

    auto samePan = [&] (std::vector<AutomationPoint> expected)
    {
        const auto points = automation.getPoints (midi, automationTargets::pan);
        bool same = points.size() == expected.size();

        for (size_t i = 0; same && i < points.size(); ++i)
            same = std::abs (points[i].beat - expected[i].beat) < 1.0e-4 && std::abs (points[i].value - expected[i].value) < 1.0e-4f;

        if (! same)
            for (auto& p : points)
                std::cout << "         (" << p.beat << ", " << p.value << ")\n";

        return same;
    };

    // The pan curve's value at a beat, as it plays (straight lines between
    // points), and the shape it's given below.
    auto panAt = [&] (double beat)
    {
        const auto points = automation.getPoints (midi, automationTargets::pan);

        for (size_t i = 1; i < points.size(); ++i)
            if (beat < points[i].beat)
                return (float) juce::jmap (beat, points[i - 1].beat, points[i].beat, (double) points[i - 1].value, (double) points[i].value);

        return points.back().value;
    };

    auto originalPan = [] (double beat)
    {
        return (float) (beat < 20.0 ? 0.2 + 0.03 * beat : beat < 40.0 ? 0.8 - 0.02 * (beat - 20.0) : 0.4);
    };

    auto panFollows = [&] (std::function<double (double)> originalBeatOf)
    {
        for (double beat = 0.5; beat < 80.0; beat += 1.0)
            if (std::abs (panAt (beat) - originalPan (originalBeatOf (beat))) > 1.0e-4f)
                return false;

        return true;
    };

    std::cout << "Adding sections\n";
    SectionId a = 0, b = 0, c = 0;
    checkUndoRoundTrip (engine, "Add Section", [&] { c = sections.addSection (8); });
    c = sectionAt (32.0).value_or (SectionInfo {}).id;
    check (c != 0 && sectionAt (32.0)->endBeat == 64.0, "a section starts on its bar and is 8 bars long");
    b = sections.addSection (4);
    a = sections.addSection (0);
    check (sectionAt (16.0) && sectionAt (16.0)->endBeat == 32.0 && sectionAt (0.0) && sectionAt (0.0)->endBeat == 16.0,
           "...unless the next section starts sooner");
    check (sections.addSection (5) == 0 && sections.getSections().size() == 3, "no section starts inside another");
    check (sectionAt (0.0)->name != sectionAt (16.0)->name && sectionAt (0.0)->colour != sectionAt (16.0)->colour,
           "new sections get names and colors of their own");

    checkUndoRoundTrip (engine, "Rename Section", [&] { sections.renameSection (b, "Chorus 1"); });
    check (sections.getSection (b)->name == "Chorus 1", "a section can be renamed");
    checkUndoRoundTrip (engine, "Section Color", [&] { sections.setSectionColour (b, SectionEditor::getSectionColours()[3]); });
    check (sections.getSection (b)->colour == SectionEditor::getSectionColours()[3], "a section can be recolored");

    checkUndoRoundTrip (engine, "Resize Section", [&]
    {
        engine.beginUndoStep ("Resize Section");
        sections.setSectionRange (c, 32.0, 56.0);
        sections.setSectionRange (c, 32.0, 64.0 + 8.0);   // a drag: only where it ends up counts
    });
    check (sections.getSection (c)->endBeat == 72.0, "a section can be resized");
    check (! sections.setSectionRange (c, 28.0, 72.0) && sections.getSection (c)->startBeat == 32.0, "sections can't overlap");
    engine.beginUndoStep ("Resize Section");
    sections.setSectionRange (c, 32.0, 64.0);

    // The music: 4 bars of chords in A [0, 16) and B [16, 32), a 2-second
    // (4-beat) file from beat 30 (across the B/C boundary at 32), and pan
    // automation running through all three.
    regions.createChordRegion (midi, 0.0);
    const auto chordsB = regions.createChordRegion (midi, 16.0);
    const auto plainFile = dir.getChildFile ("Section Clip.wav");
    writeClickWav (plainFile, 44100.0, 2.0, { 0.0 }, 0.5f);
    regions.importAudioFiles (audio, { plainFile.getFullPathName() }, 30.0);
    automation.setPoints (midi, automationTargets::pan, { { 0.0, 0.2f }, { 20.0, 0.8f }, { 40.0, 0.4f } });
    pumpMessages();
    check (regionStarts (audio) == std::vector<std::pair<double, double>> { { 30.0, 34.0 } }, "(the file is 4 beats long)");

    std::cout << "Moving a section\n";
    check (! sections.canMoveSectionTo (b, 40.0, false) && ! sections.canMoveSectionTo (b, 16.0, false)
            && sections.canMoveSectionTo (b, 16.0, true) && sections.canMoveSectionTo (b, 64.0, false),
           "a section goes between sections (not inside one, or where it is)");
    check (! sections.moveSection (b, 40.0, false), "...and nowhere else");

    checkUndoRoundTrip (engine, "Move Section", [&] { sections.moveSection (b, 64.0, false); });
    check (sections.getSection (b)->startBeat == 48.0 && sections.getSection (b)->endBeat == 64.0
            && sections.getSection (c)->startBeat == 16.0 && sections.getSection (c)->endBeat == 48.0
            && sections.getSection (a)->startBeat == 0.0,
           "moved right, it ends where it's dropped and the section after it slides left");
    check (regions.getRegion (chordsB) && regions.getRegion (chordsB)->startBeat == 48.0
            && regionStarts (midi) == std::vector<std::pair<double, double>> { { 0.0, 16.0 }, { 48.0, 64.0 } },
           "its regions go with it");
    check (regionStarts (audio) == std::vector<std::pair<double, double>> { { 16.0, 18.0 }, { 62.0, 64.0 } },
           "a region across its edge is split there, and each part goes with its section");

    // Pan was 0.2 at 0 rising to 0.8 at 20, then falling to 0.4 at 40: 0.68
    // at 16 and 0.56 at 32. Each section's part keeps its shape, and the
    // curve jumps where parts that didn't belong together now meet.
    check (samePan ({ { 0.0, 0.2f }, { 16.0, 0.68f }, { 16.0, 0.56f }, { 24.0, 0.4f }, { 48.0, 0.4f },
                      { 48.0, 0.68f }, { 52.0, 0.8f }, { 64.0, 0.56f }, { 64.0, 0.4f } }),
           "automation goes with its section");

    // Back the other way: B to the start of C, so the song is as it was
    // (the split file stays split).
    checkUndoRoundTrip (engine, "Move Section", [&] { sections.moveSection (b, 16.0, false); });
    check (sections.getSection (b)->startBeat == 16.0 && sections.getSection (c)->startBeat == 32.0,
           "moved left, it starts where it's dropped");
    check (regionStarts (midi) == std::vector<std::pair<double, double>> { { 0.0, 16.0 }, { 16.0, 32.0 } }
            && regionStarts (audio) == std::vector<std::pair<double, double>> { { 30.0, 32.0 }, { 32.0, 34.0 } },
           "regions go back to where they were");
    check (panFollows ([] (double beat) { return beat; }), "so does the automation");

    std::cout << "Copying a section\n";
    checkUndoRoundTrip (engine, "Copy Section", [&] { sections.moveSection (a, 16.0, true); });
    {
        const auto copy = sectionAt (16.0);
        check (sections.getSections().size() == 4 && copy && copy->id != a && copy->name == sections.getSection (a)->name
                && copy->endBeat == 32.0 && sections.getSection (a)->startBeat == 0.0,
               "a copy goes in where it's dropped, and the original stays");
        check (sections.getSection (b)->startBeat == 32.0 && sections.getSection (c)->startBeat == 48.0,
               "everything after it moves later");
        check (regionStarts (midi) == std::vector<std::pair<double, double>> { { 0.0, 16.0 }, { 16.0, 32.0 }, { 32.0, 48.0 } }
                && regionStarts (audio) == std::vector<std::pair<double, double>> { { 46.0, 48.0 }, { 48.0, 50.0 } },
               "its regions are copied, and later ones move");
        check (panFollows ([] (double beat) { return beat < 16.0 ? beat : beat - 16.0; }), "automation too");
    }

    std::cout << "Deleting a section\n";
    checkUndoRoundTrip (engine, "Delete Section", [&] { sections.deleteSection (sectionAt (16.0)->id); });
    check (sections.getSections().size() == 3 && sections.getSection (b)->startBeat == 16.0 && sections.getSection (c)->startBeat == 32.0,
           "deleting a section closes the gap");
    check (regionStarts (midi) == std::vector<std::pair<double, double>> { { 0.0, 16.0 }, { 16.0, 32.0 } }
            && regionStarts (audio) == std::vector<std::pair<double, double>> { { 30.0, 32.0 }, { 32.0, 34.0 } },
           "...and takes its regions with it");
    check (panFollows ([] (double beat) { return beat; }), "...and its automation");

    checkUndoRoundTrip (engine, "Remove Section Marker", [&] { sections.removeSectionMarker (a); });
    check (sections.getSections().size() == 2 && regionStarts (midi).size() == 2, "a marker can go without its music");
    engine.undo();
    pumpMessages();

    std::cout << "Saving sections\n";
    {
        const auto before = sections.getSections();
        checkSaveAndReopen (engine, dir.getChildFile ("Sections"));
        const auto after = engine.getTracks().sections().getSections();
        bool same = before.size() == after.size();

        for (size_t i = 0; same && i < before.size(); ++i)
            same = before[i].id == after[i].id && before[i].name == after[i].name && before[i].colour == after[i].colour
                    && before[i].startBeat == after[i].startBeat && before[i].endBeat == after[i].endBeat;

        check (same, "sections are saved with the session");
    }
}

//==============================================================================
// Time-stretching: loops that follow the tempo and key play through Rubber
// Band (see engine/TimeStretch.h). The bundled loops are at 120 BPM.
static void runStretchSection (const juce::File& dir)
{
    namespace te = tracktion;
    std::cout << "Time-stretching\n";

    AudioEngine engine { testSettings, false };   // the pretend device, to time playback
    TestAudioDevice device { engine, {} };
    auto& tracks = engine.getTracks();
    auto& regions = tracks.regions();
    auto& exporter = engine.getExporter();
    juce::MessageManager::getInstance()->runDispatchLoopUntil (2500);
    engine.newSession();
    pumpMessages();
    engine.setBpm (100.0);   // so the loops are stretched to 1.2 times as long

    std::optional<LibraryFile> beat, chords;

    for (auto& l : engine.getSounds().getLoops())
    {
        if (l.name == "Electro Beat")  beat = l;
        if (l.name == "Glass Chords")  chords = l;
    }

    check (beat && chords, "(the bundled loops are there)");

    if (! beat || ! chords)
        return;

    auto clipOf = [&] (RegionId id) { return dynamic_cast<te::AudioClipBase*> (detail::findClip (TestAudioDevice::getEditOf (engine), id)); };
    auto modeOf = [&] (RegionId id)
    {
        auto* clip = clipOf (id);
        return clip != nullptr ? clip->getTimeStretchMode() : te::TimeStretcher::disabled;
    };

    // The drum loop at beat 4; the chords well after it, at beat 40, so the
    // drums' hits can be found on their own.
    const auto drums = tracks.addAudioTrack();
    const auto keys = tracks.addAudioTrack();
    RegionId beatRegion = 0, chordsRegion = 0;
    checkUndoRoundTrip (engine, "Import Audio File", [&]
    {
        beatRegion = regions.importAudioFiles (drums, { beat->file.getFullPathName() }, 4.0).front();
    });
    chordsRegion = regions.importAudioFiles (keys, { chords->file.getFullPathName() }, 40.0).front();
    pumpMessages();
    check (modeOf (beatRegion) == te::TimeStretcher::rubberbandPercussive, "a drum loop is stretched by Rubber Band, in its percussive mode");
    check (modeOf (chordsRegion) == te::TimeStretcher::rubberbandMelodic, "a loop with a key, in its melodic mode");

    // A plain recording only gets a stretcher when it's transposed.
    const auto plainFile = dir.getChildFile ("Plain.wav");
    writeClickWav (plainFile, 44100.0, 2.0, { 0.0 }, 0.5f);
    const auto plainRegion = regions.importAudioFiles (keys, { plainFile.getFullPathName() }, 60.0).front();
    pumpMessages();
    check (modeOf (plainRegion) == te::TimeStretcher::disabled, "a recording isn't stretched");
    checkUndoRoundTrip (engine, "Transpose Region", [&] { regions.setRegionTranspose (plainRegion, 3); });
    check (modeOf (plainRegion) == te::TimeStretcher::rubberbandMelodic, "a transposed recording is (melodic)");
    engine.undo();
    pumpMessages();
    check (modeOf (plainRegion) == te::TimeStretcher::disabled, "undo takes the stretcher away again");
    regions.setRegionTranspose (beatRegion, 2);
    pumpMessages();
    check (modeOf (beatRegion) == te::TimeStretcher::rubberbandPercussive, "a transposed drum loop keeps its percussive mode");
    engine.undo();
    pumpMessages();

    // Where the hits start in the loop's file, in seconds: the first sample
    // over a third of the file's peak after 50 ms below it.
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();

    auto findHits = [&] (const juce::File& f, double toSeconds)
    {
        std::vector<double> hits;
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (f));

        if (reader == nullptr)
            return hits;

        const auto rate = reader->sampleRate;
        juce::AudioBuffer<float> audio (1, (int) juce::jmin ((juce::int64) (toSeconds * rate), reader->lengthInSamples));
        reader->read (&audio, 0, audio.getNumSamples(), 0, true, false);
        const auto threshold = audio.getMagnitude (0, audio.getNumSamples()) / 3.0f;
        const int minGap = (int) (0.05 * rate);
        int quiet = minGap;

        for (int i = 0; i < audio.getNumSamples(); ++i)
        {
            if (std::abs (audio.getSample (0, i)) > threshold)
            {
                if (quiet >= minGap)
                    hits.push_back (i / rate);

                quiet = 0;
            }
            else
            {
                ++quiet;
            }
        }

        return hits;
    };

    // Exports the song (the file it wrote).
    auto exportSong = [&] (const juce::String& name)
    {
        ExportSettings settings;
        settings.destination = dir.getChildFile ("Stretch Export").getChildFile (name + ".wav");
        bool done = false;

        if (exporter.start (settings, [&] (const ExportResult&) { done = true; }).failed())
            return juce::File();

        for (int i = 0; i < 1200 && ! done; ++i)
            juce::MessageManager::getInstance()->runDispatchLoopUntil (50);

        return settings.destination;
    };

    // The loop's own hits, moved to where they should play: from the
    // region's start, stretched by (loop tempo / session tempo).
    const auto loopHits = findHits (beat->file, 60.0);

    auto expectedHits = [&] (double bpm)
    {
        std::vector<double> hits;

        for (auto h : loopHits)
            hits.push_back (4.0 * 60.0 / bpm + h * 120.0 / bpm);

        return hits;
    };

    // How far the drum loop's hits are from where they should be in an
    // export, at worst, in ms. Each one is looked for within 80 ms of its
    // place: where the sound first gets over half as loud as it gets there.
    // (Not with findHits: stretching changes how loud the hits are against
    // each other, and Rubber Band keeps the hi-hats sharp enough to count.)
    auto worstMs = [&] (const juce::File& f, const std::vector<double>& expected)
    {
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (f));

        if (reader == nullptr || expected.empty())
            return 1.0e6;

        const auto rate = reader->sampleRate;
        const int window = (int) (0.08 * rate);
        juce::AudioBuffer<float> audio (1, 2 * window);
        double worst = 0.0;

        for (auto e : expected)
        {
            const auto from = (juce::int64) (e * rate) - window;
            reader->read (&audio, 0, audio.getNumSamples(), from, true, false);
            const auto threshold = audio.getMagnitude (0, audio.getNumSamples()) / 2.0f;
            int onset = 0;

            while (onset < audio.getNumSamples() && std::abs (audio.getSample (0, onset)) <= threshold)
                ++onset;

            worst = std::max (worst, std::abs ((double) (from + onset) / rate - e) * 1000.0);
        }

        return worst;
    };

    constexpr double toleranceMs = 5.0;
    const auto rubberBand = exportSong ("Rubber Band");
    std::cout << "  (" << loopHits.size() << " hits in the loop; Rubber Band puts them within "
              << worstMs (rubberBand, expectedHits (100.0)) << " ms)\n";
    check (loopHits.size() >= 4 && worstMs (rubberBand, expectedHits (100.0)) < toleranceMs,
           "a stretched drum loop's hits land on their (stretched) beats");

    // A session from before Rubber Band: its loops were stretched with SoundTouch.
    clipOf (beatRegion)->setTimeStretchMode (te::TimeStretcher::soundtouchBetter);
    clipOf (chordsRegion)->setTimeStretchMode (te::TimeStretcher::soundtouchBetter);
    pumpMessages();
    // (SoundTouch lets hits wander tens of ms off the beat, as it picks
    // where to splice the audio; Rubber Band keeps them on it.)
    const auto soundTouch = exportSong ("SoundTouch");
    std::cout << "  (SoundTouch puts them within " << worstMs (soundTouch, expectedHits (100.0)) << " ms)\n";

    const auto oldSession = dir.getChildFile ("Old Stretch.iksession");
    check (engine.saveSessionAs (oldSession) && engine.openSession (oldSession), "(an older session is saved and reopened)");
    pumpMessages();
    check (modeOf (beatRegion) == te::TimeStretcher::rubberbandPercussive && modeOf (chordsRegion) == te::TimeStretcher::rubberbandMelodic,
           "an older session's SoundTouch loops switch to Rubber Band when it opens (drums percussive, chords melodic)");
    check (! engine.hasUnsavedChanges(), "without the session showing as changed");
    check (worstMs (exportSong ("Upgraded"), expectedHits (100.0)) < toleranceMs,
           "the upgraded loop plays in the same place, its hits on the beat");

    checkSaveAndReopen (engine, dir.getChildFile ("Stretch"));
    check (modeOf (beatRegion) == te::TimeStretcher::rubberbandPercussive, "the stretcher is saved with the session");

    // Tempo and time signature changes (the loops stay on their beats).
    engine.setBpm (80.0);
    pumpMessages();
    const auto slower = exportSong ("80 BPM");
    std::cout << "  (at 80 BPM: within " << worstMs (slower, expectedHits (80.0)) << " ms)\n";
    check (worstMs (slower, expectedHits (80.0)) < toleranceMs, "at a new tempo, the hits follow the beats");

    engine.setTimeSignature (6, 8);
    pumpMessages();
    check (worstMs (exportSong ("Six Eight"), expectedHits (80.0)) < toleranceMs, "a new time signature doesn't move them");
    engine.undo();
    pumpMessages();

    // A key change moves the chords (pitch shifted by Rubber Band) and they
    // still fill their region.
    engine.setKey (2, false);   // D major: up 2
    pumpMessages();
    {
        const auto song = exportSong ("Key");
        auto chordsInfo = regions.getRegion (chordsRegion);
        engine.setPositionBeats (chordsInfo->startBeat);
        const auto from = engine.getTransportState().positionSeconds;
        engine.setPositionBeats (chordsInfo->startBeat + chordsInfo->lengthBeats);
        const auto to = engine.getTransportState().positionSeconds;

        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (song));
        float early = 1.0f, late = 0.0f;

        if (reader != nullptr)
        {
            juce::AudioBuffer<float> audio (2, (int) reader->lengthInSamples);
            reader->read (&audio, 0, audio.getNumSamples(), 0, true, true);
            const auto at = [&] (double t) { return juce::jlimit (0, audio.getNumSamples() - 1, (int) (t * reader->sampleRate)); };
            early = audio.getMagnitude (at (from - 1.0), at (from - 0.05) - at (from - 1.0));
            late = audio.getMagnitude (at (to - 0.5), at (to - 0.1) - at (to - 0.5));
        }

        check (early < 1.0e-4 && late > 0.01f, "a loop moved to a new key plays from its start to its end");
    }

    // A dozen stretched loops: how long Tracktion takes to stretch them
    // (in the background, after placing them or changing the tempo), and
    // how much of a core playing them takes.
    engine.newSession();
    pumpMessages();
    engine.setBpm (100.0);
    std::vector<RegionId> dozen;

    for (int i = 0; i < 12; ++i)
    {
        const auto& l = engine.getSounds().getLoops()[(size_t) i % engine.getSounds().getLoops().size()];
        const auto t = tracks.addAudioTrack();
        auto ids = regions.importAudioFiles (t, { l.file.getFullPathName() }, 0.0);
        regions.loopRegion (ids.front(), 32.0);   // 8 bars
        dozen.push_back (ids.front());
    }

    pumpMessages();

    // Waits for every loop's stretched copy; returns how long that took (s).
    auto timeStretching = [&]
    {
        const auto start = juce::Time::getMillisecondCounterHiRes();

        for (auto id : dozen)
            if (auto* clip = clipOf (id))
                clip->beginRenderingNewProxyIfNeeded();

        // A stretched copy can be read once it's finished. (Tracktion's
        // isGeneratingPlaybackFile is true for any stretched clip.)
        for (int i = 0; i < 1500; ++i)
        {
            bool busy = false;

            for (auto id : dozen)
                if (auto* clip = clipOf (id))
                    busy = busy || clip->getPlaybackFile().getSampleRate() <= 0.0;

            if (! busy)
                break;

            juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
        }

        return (juce::Time::getMillisecondCounterHiRes() - start) / 1000.0;
    };

    auto timePlaying = [&]
    {
        engine.setPositionBeats (0.0);
        engine.play();
        const auto start = juce::Time::getMillisecondCounterHiRes();
        runAudio (device, 10.0);
        const auto seconds = (juce::Time::getMillisecondCounterHiRes() - start) / 1000.0;
        engine.stop();
        pumpMessages();
        return seconds / 10.0 * 100.0;   // % of one core
    };

    const auto rubberBandRender = timeStretching();
    const auto rubberBandPlay = timePlaying();

    for (auto id : dozen)
        clipOf (id)->setTimeStretchMode (te::TimeStretcher::soundtouchBetter);

    const auto soundTouchRender = timeStretching();
    const auto soundTouchPlay = timePlaying();
    std::cout << "  (12 loops, 8 bars each: stretching takes " << rubberBandRender << " s with Rubber Band, "
              << soundTouchRender << " s with SoundTouch; playing them takes " << rubberBandPlay << "% / "
              << soundTouchPlay << "% of a core, Debug build)\n";
    check (rubberBandRender < 30.0, "a dozen loops are stretched in good time");
}

//==============================================================================
// Exports the song to "Song <number>.wav" in `folder` and returns the file
// (waiting for the export to finish).
static juce::File exportForHits (AudioEngine& engine, const juce::File& folder, int number)
{
    ExportSettings settings;
    settings.destination = folder.getChildFile ("Song " + juce::String (number) + ".wav");
    bool done = false;

    if (engine.getExporter().start (settings, [&] (const ExportResult&) { done = true; }).failed())
        return juce::File();

    for (int i = 0; i < 1200 && ! done; ++i)
        juce::MessageManager::getInstance()->runDispatchLoopUntil (50);

    return settings.destination;
}

// Checks each hit in an exported click track is where it should be (seconds
// on the timeline): returns how far off the worst one is, in ms. Each one
// is looked for within 80 ms of its place: where the sound first gets over
// half as loud as it gets there.
static double worstHitErrorMs (const juce::File& f, const std::vector<double>& expected)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (f));

    if (reader == nullptr || expected.empty())
        return 1.0e6;

    const auto rate = reader->sampleRate;
    const int window = (int) (0.08 * rate);
    juce::AudioBuffer<float> audio (1, 2 * window);
    double worst = 0.0;

    for (auto e : expected)
    {
        const auto from = (juce::int64) (e * rate) - window;
        reader->read (&audio, 0, audio.getNumSamples(), from, true, false);
        const auto threshold = audio.getMagnitude (0, audio.getNumSamples()) / 2.0f;
        int onset = 0;

        if (threshold < 0.01f)
            return 1.0e6;   // silence where a hit should be

        while (onset < audio.getNumSamples() && std::abs (audio.getSample (0, onset)) <= threshold)
            ++onset;

        worst = std::max (worst, std::abs ((double) (from + onset) / rate - e) * 1000.0);
    }

    return worst;
}

//==============================================================================
// Flex Time (see engine/FlexEditor.h): dragging the hits in a recording.
// A click track stands in for a drum recording: its hits are easy to find
// in an export, so the test can check exactly where they play.
static void runFlexSection (const juce::File& dir)
{
    namespace te = tracktion;
    std::cout << "Flex Time\n";

    AudioEngine engine { testSettings, false };   // the pretend device (export needs no real one)
    TestAudioDevice device { engine, {} };
    auto& tracks = engine.getTracks();
    auto& regions = tracks.regions();
    auto& flex = tracks.flex();
    juce::MessageManager::getInstance()->runDispatchLoopUntil (2500);
    engine.newSession();
    pumpMessages();
    engine.setBpm (120.0);   // a beat every half second

    auto clipOf = [&] (RegionId id) { return dynamic_cast<te::WaveAudioClip*> (detail::findClip (TestAudioDevice::getEditOf (engine), id)); };

    // A click every half second from 0.5 s to 3 s, in a 4 s file. Placed at
    // beat 4 (2 s), they play on beats 5 to 10.
    const auto clicksFile = dir.getChildFile ("Clicks.wav");
    const std::vector<double> clickSeconds { 0.5, 1.0, 1.5, 2.0, 2.5, 3.0 };
    writeClickWav (clicksFile, 44100.0, 4.0, clickSeconds, 0.5f);

    const auto drums = tracks.addAudioTrack();
    const auto other = tracks.addAudioTrack();
    const auto region = regions.importAudioFiles (drums, { clicksFile.getFullPathName() }, 4.0).front();
    pumpMessages();

    check (! flex.isFlexOn (drums) && ! flex.getFlex (region).on, "Flex is off for a new audio track");
    check (! clipOf (region)->state.getChildWithName (te::IDs::WARPTIME).isValid(), "a region that's never been flexed has no warp markers");

    checkUndoRoundTrip (engine, "Flex On", [&] { flex.setFlexOn (drums, true); });
    check (flex.isFlexOn (drums) && flex.getFlex (region).on, "Flex On flexes the track's regions");
    check (! clipOf (region)->getWarpTime(), "...which play their recording as it is until a hit is moved");

    // The hits are found in the background.
    auto waitForHits = [&] (RegionId id)
    {
        for (int i = 0; i < 400 && flex.getFlex (id).analysing; ++i)
            juce::MessageManager::getInstance()->runDispatchLoopUntil (25);

        return flex.getFlex (id);
    };

    auto info = waitForHits (region);
    bool hitsFound = info.transients.size() == clickSeconds.size();

    for (size_t i = 0; hitsFound && i < clickSeconds.size(); ++i)
        hitsFound = std::abs (info.transients[i].sourceSeconds - clickSeconds[i]) < 0.003
                     && std::abs (info.transients[i].beat - (5.0 + (double) i)) < 0.01;

    check (hitsFound, "the hits in the recording are found, where they play");

    if (! hitsFound)
        for (auto& t : info.transients)
            std::cout << "         hit at " << t.sourceSeconds << " s, beat " << t.beat << "\n";

    int exportNumber = 0;
    auto exportSong = [&] { return exportForHits (engine, dir.getChildFile ("Flex Export"), ++exportNumber); };
    auto worstMs = [] (const juce::File& f, const std::vector<double>& expected) { return worstHitErrorMs (f, expected); };

    constexpr double toleranceMs = 5.0;
    auto hitsAt = [&] (const char* what, const std::vector<double>& beats, double secondsPerBeat)
    {
        std::vector<double> expected;

        for (auto b : beats)
            expected.push_back (b * secondsPerBeat);

        const auto worst = worstMs (exportSong(), expected);
        std::cout << "  (" << what << ": hits within " << worst << " ms)\n";
        return worst < toleranceMs;
    };

    check (hitsAt ("flexed, nothing moved", { 5, 6, 7, 8, 9, 10 }, 0.5), "an untouched Flex region plays its hits where they were");

    // Drag the second hit (1 s into the file, beat 6) a quarter of a beat
    // later. Its neighbours get markers, so nothing else moves.
    checkUndoRoundTrip (engine, "Move Flex Marker", [&] { flex.moveMarker (region, info.transients[1].sourceSeconds, 6.25); });
    info = flex.getFlex (region);
    // (The hits are found a fraction of a millisecond before they start.)
    check (info.markers.size() == 5 && std::abs (info.markers[2].beat - 6.25) < 1.0e-6
             && std::abs (info.markers[1].beat - 5.0) < 0.005 && std::abs (info.markers[3].beat - 7.0) < 0.005,
           "dragging a hit pins it, and its neighbours stay where they are");
    check (info.hasEdits && clipOf (region)->getWarpTime(), "the region now plays warped");

    // Straight away: the export waits for the stretched audio.
    check (hitsAt ("one hit moved", { 5, 6.25, 7, 8, 9, 10 }, 0.5), "the export plays the moved hit where it was dragged, and the others in place");

    flex.moveMarker (region, info.markers[2].sourceSeconds, 100.0);
    pumpMessages();
    info = flex.getFlex (region);
    check (info.markers[2].beat < 7.0 && info.markers[2].beat > 6.25, "a marker can't be dragged past its neighbour");
    engine.undo();
    pumpMessages();

    // Removing a marker; Reset Flex.
    checkUndoRoundTrip (engine, "Remove Flex Marker", [&] { flex.removeMarker (region, info.markers[1].sourceSeconds); });
    engine.undo();
    pumpMessages();
    checkUndoRoundTrip (engine, "Reset Flex", [&] { flex.resetFlex (region); });
    check (! flex.getFlex (region).hasEdits && ! clipOf (region)->getWarpTime(), "Reset Flex takes every marker away");
    engine.undo();
    pumpMessages();

    // Flex Off: the region plays its recording; the markers are kept for
    // when Flex comes back on.
    checkUndoRoundTrip (engine, "Flex Off", [&] { flex.setFlexOn (drums, false); });
    check (! clipOf (region)->getWarpTime() && ! clipOf (region)->state.getParent().getParent().hasProperty (detail::flexProp),
           "Flex Off plays the recording as it was (and leaves nothing on the track)");
    engine.undo();
    pumpMessages();
    check (clipOf (region)->getWarpTime(), "Flex back on plays the moved hit again");

    //==============================================================================
    std::cout << "Flex: tempo and time signature\n";

    // The flexed region follows the tempo: every hit stays on its beat.
    checkUndoRoundTrip (engine, "Tempo", [&] { engine.setBpm (100.0); });
    info = flex.getFlex (region);
    check (std::abs (info.markers[2].beat - 6.25) < 1.0e-6 && std::abs (regions.getRegion (region)->lengthBeats - 8.0) < 1.0e-6,
           "a new tempo keeps the markers (and the region's length) on their beats");
    check (hitsAt ("at 100 BPM", { 5, 6.25, 7, 8, 9, 10 }, 0.6), "at a new tempo, the flexed hits play on their beats");

    // An imported file on a track without Flex keeps its timing in seconds.
    const auto plain = regions.importAudioFiles (other, { clicksFile.getFullPathName() }, 20.0).front();
    pumpMessages();
    const double plainLength = regions.getRegion (plain)->lengthBeats;
    engine.setBpm (120.0);
    pumpMessages();
    check (std::abs (regions.getRegion (plain)->lengthBeats - plainLength * 1.2) < 1.0e-3, "a region on a track without Flex doesn't follow the tempo");
    regions.deleteRegion (plain);
    pumpMessages();

    engine.setTimeSignature (6, 8);
    pumpMessages();
    check (hitsAt ("in 6/8", { 5, 6.25, 7, 8, 9, 10 }, 0.5), "a new time signature doesn't move them");
    engine.setTimeSignature (4, 4);
    pumpMessages();

    // A tempo change in the middle of the region (bar 3 = beat 8). The hits
    // after it (beats 8, 9 and 10) have no markers of their own: they stay
    // on their beats too, held by a tempo marker the user doesn't see.
    auto shownMarkers = [&] (const FlexInfo& i)
    {
        return std::count_if (i.markers.begin(), i.markers.end(), [] (const FlexMarker& m) { return ! m.tempoMarker; });
    };

    auto unmarkedHitsOnBeats = [&] (const FlexInfo& i)
    {
        bool onBeats = i.transients.size() == clickSeconds.size();

        for (size_t h = 3; onBeats && h < i.transients.size(); ++h)
            onBeats = std::abs (i.transients[h].beat - (5.0 + (double) h)) < 0.005;

        return onBeats;
    };

    const auto beforeTempoChange = flex.getFlex (region);
    checkUndoRoundTrip (engine, "Add Tempo Change", [&] { engine.addTempoChange (2, 80.0); });
    info = flex.getFlex (region);
    check (std::abs (info.markers[2].beat - 6.25) < 1.0e-6 && std::abs (info.markers[3].beat - 7.0) < 0.005,
           "a tempo change inside the region keeps its markers on their beats");
    check (unmarkedHitsOnBeats (info) && shownMarkers (info) == shownMarkers (beforeTempoChange),
           "...and the hits between markers too (with no new markers to see)");
    {
        // 120 BPM up to beat 8 (4 s), 80 after: beats 9 and 10 are at 4.75 s and 5.5 s.
        const auto worst = worstMs (exportSong(), { 2.5, 3.125, 3.5, 4.0, 4.75, 5.5 });
        std::cout << "  (tempo change inside: hits within " << worst << " ms)\n";
        check (worst < toleranceMs, "...where they play (80 BPM from beat 8)");
    }

    // Taking the tempo change away again: the tempo marker isn't needed any more.
    engine.removeTempoChange (1);
    pumpMessages();
    info = flex.getFlex (region);
    check (unmarkedHitsOnBeats (info) && info.markers.size() == beforeTempoChange.markers.size(),
           "without the tempo change, the hits stay on their beats and the tempo marker goes");
    engine.undo();
    engine.undo();
    pumpMessages();

    checkSaveAndReopen (engine, dir.getChildFile ("Flex"));
    check (flex.isFlexOn (drums) && std::abs (flex.getFlex (region).markers[2].beat - 6.25) < 1.0e-6,
           "Flex and its markers are saved with the session");
    check (hitsAt ("reopened", { 5, 6.25, 7, 8, 9, 10 }, 0.5), "the reopened session plays the flexed hits the same");

    //==============================================================================
    std::cout << "Flex: editing regions\n";

    // Moving the region takes its hits with it, on their beats, even into
    // a part of the song at another tempo.
    auto markerOffsets = [&] (RegionId id)
    {
        std::vector<double> offsets;
        const auto r = regions.getRegion (id);

        for (auto& m : flex.getFlex (id).markers)
            offsets.push_back (m.beat - r->startBeat);

        return offsets;
    };

    auto sameOffsets = [] (const std::vector<double>& a, const std::vector<double>& b)
    {
        bool same = a.size() == b.size();

        for (size_t i = 0; same && i < a.size(); ++i)
            same = std::abs (a[i] - b[i]) < 1.0e-6;

        return same;
    };

    const auto offsets = markerOffsets (region);
    engine.addTempoChange (8, 90.0);   // from beat 32
    pumpMessages();
    checkUndoRoundTrip (engine, "Move Region", [&] { regions.moveRegions ({ { region, drums, 36.0 } }); });
    check (sameOffsets (markerOffsets (region), offsets) && std::abs (regions.getRegion (region)->lengthBeats - 8.0) < 1.0e-6,
           "a flexed region moved to another tempo keeps its hits on their beats");
    {
        // 120 BPM up to beat 32 (16 s), 90 after.
        std::vector<double> expected;

        for (double b : { 37.0, 38.25, 39.0, 40.0, 41.0, 42.0 })
            expected.push_back (16.0 + (b - 32.0) * 60.0 / 90.0);

        const auto worst = worstMs (exportSong(), expected);
        std::cout << "  (moved to 90 BPM: hits within " << worst << " ms)\n";
        check (worst < toleranceMs, "...and plays them there");
    }
    engine.undo();
    engine.undo();
    pumpMessages();
    engine.undo();   // the tempo change
    pumpMessages();
    check (sameOffsets (markerOffsets (region), offsets) && regions.getRegion (region)->startBeat == 4.0, "undo puts it back");

    // Moved so a tempo change (90 BPM from beat 32) falls inside it: every
    // hit keeps its beat, the ones without markers too.
    engine.addTempoChange (8, 90.0);
    pumpMessages();
    regions.moveRegions ({ { region, drums, 28.0 } });
    pumpMessages();
    {
        std::vector<double> expected;

        for (double b : { 29.0, 30.25, 31.0, 32.0, 33.0, 34.0 })
            expected.push_back (b <= 32.0 ? b * 0.5 : 16.0 + (b - 32.0) * 60.0 / 90.0);

        const auto worst = worstMs (exportSong(), expected);
        std::cout << "  (moved across a tempo change: hits within " << worst << " ms)\n";
        check (worst < toleranceMs && shownMarkers (flex.getFlex (region)) == (long) offsets.size(),
               "a flexed region moved across a tempo change keeps every hit on its beat");
    }
    engine.undo();
    engine.undo();
    pumpMessages();

    // Split: both halves keep the markers, and the hits play as before.
    std::vector<RegionId> right;
    checkUndoRoundTrip (engine, "Split Region", [&] { right = regions.splitRegions ({ region }, 7.5); });
    check (right.size() == 1 && flex.getFlex (right.front()).on && flex.getFlex (right.front()).markers.size() == 5,
           "both halves of a split flexed region keep its markers");
    check (hitsAt ("split", { 5, 6.25, 7, 8, 9, 10 }, 0.5), "a split flexed region plays its hits where they were");
    engine.undo();
    pumpMessages();

    // Trim: the markers stay where they are on the timeline.
    checkUndoRoundTrip (engine, "Trim Region", [&] { regions.trimRegion (region, 4.5, 11.0); });
    check (std::abs (flex.getFlex (region).markers[2].beat - 6.25) < 1.0e-6, "trimming a flexed region leaves its hits in place");
    engine.undo();
    pumpMessages();

    // Duplicate and copy: the copies are flexed like the original, unless
    // pasted on a track without Flex.
    std::vector<RegionId> copies;
    checkUndoRoundTrip (engine, "Duplicate Region", [&] { copies = regions.duplicateRegions ({ region }); });
    check (copies.size() == 1 && sameOffsets (markerOffsets (copies.front()), offsets), "a duplicate of a flexed region is flexed the same");
    engine.undo();
    pumpMessages();

    regions.copyRegions ({ region });
    checkUndoRoundTrip (engine, "Paste", [&] { copies = regions.pasteRegions (other, 40.0); });
    check (copies.size() == 1 && ! flex.getFlex (copies.front()).on && ! clipOf (copies.front())->getWarpTime(),
           "pasted on a track without Flex, a copy plays its recording as it is");
    engine.undo();
    pumpMessages();
    copies = regions.pasteRegions (drums, 40.0);
    pumpMessages();
    check (copies.size() == 1 && clipOf (copies.front())->getWarpTime() && sameOffsets (markerOffsets (copies.front()), offsets),
           "pasted on a Flex track, it plays flexed");
    engine.undo();
    pumpMessages();

    // Looping: the loop repeats the flexed audio.
    checkUndoRoundTrip (engine, "Loop Region", [&] { regions.loopRegion (region, 20.0); });
    check (hitsAt ("looped", { 5, 6.25, 7, 8, 9, 10, 13, 14.25, 15 }, 0.5), "a looped flexed region repeats its flexed hits");
    engine.undo();
    pumpMessages();

    // Played backwards, a region can't use Flex: its markers wait.
    regions.setRegionReversed (region, true);
    pumpMessages();
    check (! flex.getFlex (region).available && flex.getFlex (region).unavailableReason.isNotEmpty() && ! clipOf (region)->getWarpTime(),
           "a reversed region can't use Flex (and says why)");
    engine.undo();
    pumpMessages();
    check (clipOf (region)->getWarpTime(), "played forwards again, it's flexed again");

    // Loops that follow the tempo can't use Flex.
    std::optional<LibraryFile> loop;

    for (auto& l : engine.getSounds().getLoops())
        if (l.bpm > 0.0)
            loop = l;

    if (loop)
    {
        const auto loopRegion = regions.importAudioFiles (drums, { loop->file.getFullPathName() }, 60.0).front();
        pumpMessages();
        check (! flex.getFlex (loopRegion).available && ! clipOf (loopRegion)->getWarpTime(),
               "a loop that follows the tempo can't use Flex, even on a Flex track");
        engine.undo();
        pumpMessages();
    }

    // Arrangement markers: moving a section moves its flexed regions, hits and all.
    {
        auto& sections = tracks.sections();
        const auto verse = sections.addSection (0);    // bars 1-8 (beats 0-32) hold the region
        const auto chorus = sections.addSection (8);
        pumpMessages();
        checkUndoRoundTrip (engine, "Move Section", [&] { sections.moveSection (verse, 64.0, false); });
        check (sameOffsets (markerOffsets (region), offsets), "a section move keeps a flexed region's hits on their beats");
        engine.undo();
        engine.undo();
        engine.undo();
        pumpMessages();
        juce::ignoreUnused (chorus);
    }

    //==============================================================================
    std::cout << "Flex: takes\n";

    // Each take keeps its own Flex edits. (Takes made as crash recovery
    // makes them: one per 2 s pass of the 4 s file.)
    const auto taken = regions.importAudioFiles (other, { clicksFile.getFullPathName() }, 80.0).front();
    detail::splitIntoPassTakes (*clipOf (taken), te::TimeDuration::fromSeconds (2.0));
    flex.setFlexOn (other, true);
    pumpMessages();
    const auto takeInfo = waitForHits (taken);

    check (takeInfo.transients.size() >= 2, "(the take's hits are found)");

    if (takeInfo.transients.size() >= 2)
    {
        flex.moveMarker (taken, takeInfo.transients[1].sourceSeconds, takeInfo.transients[1].beat + 0.25);
        pumpMessages();
        const auto edited = flex.getFlex (taken).markers;
        const int current = regions.getRegion (taken)->currentTake;
        checkUndoRoundTrip (engine, "Choose Take", [&] { tracks.takes().setCurrentTake (taken, current == 0 ? 1 : 0); });
        check (! flex.getFlex (taken).hasEdits, "another take plays without the first one's Flex edits");
        tracks.takes().setCurrentTake (taken, current);
        pumpMessages();
        check (flex.getFlex (taken).markers == edited, "choosing the first take again brings its Flex edits back");
    }

    flex.setFlexOn (drums, false);
    flex.setFlexOn (other, false);
    pumpMessages();
}

//==============================================================================
// Quantize (engine/Quantize.h): lining notes, and the hits in a recording
// (FlexEditor::quantize), up with the grid.
static void runQuantizeSection (const juce::File& dir)
{
    namespace te = tracktion;
    std::cout << "Quantize: the grid\n";

    // The maths, on a song that's in 4/4 for two bars (beats 0-8), then 6/8
    // (a beat is an eighth note there, so its bars are 6 beats: 8, 14, 20).
    {
        const TempoMap song ({ TempoChange {} }, { TimeSignatureChange {}, TimeSignatureChange { 8.0, 2, 6, 8 } });
        auto settings = [] (double note, double strength = 1.0, double swing = 0.0) { return QuantizeSettings { note, strength, swing }; };
        auto near = [] (double a, double b) { return std::abs (a - b) < 1.0e-9; };

        check (near (quantize::gridLine (1.1, settings (1.0 / 16.0), song), 1.0)
                 && near (quantize::gridLine (9.3, settings (1.0 / 16.0), song), 9.5),
               "a sixteenth is a quarter of a beat in 4/4, half of one in 6/8");
        check (near (quantize::gridLine (12.9, settings (0.0), song), 14.0) && near (quantize::gridLine (7.1, settings (0.0), song), 8.0),
               "the bar grid follows the bars of each time signature");
        check (near (quantize::quantizedBeat (1.1, settings (1.0 / 16.0, 0.5), song), 1.05), "strength 50% moves halfway to the grid");
        check (near (quantize::gridLine (0.7, settings (1.0 / 8.0, 0.0, 1.0), song), 0.5 + 0.5 / 3.0)
                 && near (quantize::gridLine (0.9, settings (1.0 / 8.0, 0.0, 1.0), song), 1.0),
               "full swing makes every second eighth a triplet late");
        check (near (quantize::gridLine (8.6, settings (1.0 / 16.0, 0.0, 1.0), song), 8.5 + 0.5 / 3.0),
               "swing pairs count from the bar line, in each time signature");
        check (near (quantize::gridLineAtOrAfter (8.1, settings (0.0), song), 14.0)
                 && near (quantize::gridLineAtOrAfter (8.1, settings (1.0 / 16.0), song), 8.5),
               "the next grid line after a beat");
    }

    AudioEngine engine { testSettings, false };   // the pretend device (export needs no real one)
    TestAudioDevice device { engine, {} };
    auto& tracks = engine.getTracks();
    auto& regions = tracks.regions();
    auto& flex = tracks.flex();
    juce::MessageManager::getInstance()->runDispatchLoopUntil (2500);
    engine.newSession();
    pumpMessages();
    engine.setBpm (120.0);   // a beat every half second

    //==============================================================================
    std::cout << "Quantize: notes\n";
    {
        const auto keys = tracks.addInstrumentTrack();
        const auto midi = regions.createChordRegion (keys, 0.0);
        pumpMessages();
        std::vector<NoteId> chord;

        // A copy first: looping over a member of the optional getRegion
        // returns would read it after it's gone.
        const auto chordRegion = regions.getRegion (midi);

        for (auto& n : chordRegion->notes)
            chord.push_back (n.id);

        tracks.notes().deleteNotes (midi, chord);
        const auto a = tracks.notes().addNote (midi, { 60, 1.1, 0.5, 100, 0 });
        const auto b = tracks.notes().addNote (midi, { 62, 9.3, 0.5, 100, 0 });
        pumpMessages();


        auto startOf = [&] (NoteId id)
        {
            const auto r = regions.getRegion (midi);

            for (auto& n : r->notes)
                if (n.id == id)
                    return n.startBeat;

            return -1.0;
        };

        checkUndoRoundTrip (engine, "Quantize", [&] { tracks.notes().quantiseNotes (midi, {}, { 1.0 / 16.0, 0.5, 0.0 }); });
        check (std::abs (startOf (a) - 1.05) < 1.0e-6 && std::abs (startOf (b) - 9.275) < 1.0e-6, "notes quantized at 50% strength move halfway");

        engine.undo();
        pumpMessages();

        // In 6/8 from bar 3, the bar grid follows its 6-beat bars.
        engine.addTimeSignatureChange (2, 6, 8);
        pumpMessages();
        tracks.notes().quantiseNotes (midi, {}, { 0.0 });
        pumpMessages();
        check (std::abs (startOf (a) - 0.0) < 1.0e-6 && std::abs (startOf (b) - 8.0) < 1.0e-6,
               "notes quantized to bars follow the time signature");
        engine.undo();
        engine.undo();
        pumpMessages();
        regions.deleteRegion (midi);
        pumpMessages();
    }

    //==============================================================================
    std::cout << "Quantize: Flex\n";
    auto clipOf = [&] (RegionId id) { return dynamic_cast<te::WaveAudioClip*> (detail::findClip (TestAudioDevice::getEditOf (engine), id)); };

    // Hits a little off the beat. Placed at beat 4 (2 s), they play on
    // beats 5.1, 6, 6.86, 7.6, 8.16 and 9.
    const auto clicksFile = dir.getChildFile ("Loose Clicks.wav");
    const std::vector<double> playedBeats { 5.1, 6.0, 6.86, 7.6, 8.16, 9.0 };
    std::vector<double> clickSeconds;

    for (auto b : playedBeats)
        clickSeconds.push_back ((b - 4.0) * 0.5);

    writeClickWav (clicksFile, 44100.0, 4.0, clickSeconds, 0.5f);

    const auto drums = tracks.addAudioTrack();
    const auto region = regions.importAudioFiles (drums, { clicksFile.getFullPathName() }, 4.0).front();
    pumpMessages();

    // The markers' beats, for the hits that have one (by which click it is).
    auto markerBeatOf = [&] (size_t click)
    {
        for (auto& m : flex.getFlex (region).markers)
            if (std::abs (m.sourceSeconds - clickSeconds[click]) < 0.003)
                return m.beat;

        return -1.0;
    };

    auto waitForQuantize = [&]
    {
        for (int i = 0; i < 400 && flex.getFlex (region).quantizePending; ++i)
            juce::MessageManager::getInstance()->runDispatchLoopUntil (25);

        pumpMessages();
    };

    int exportNumber = 0;
    auto hitsAt = [&] (const char* what, const std::vector<double>& seconds)
    {
        const auto worst = worstHitErrorMs (exportForHits (engine, dir.getChildFile ("Quantize Export"), ++exportNumber), seconds);
        std::cout << "  (" << what << ": hits within " << worst << " ms)\n";
        return worst < 5.0;
    };

    auto atBpm120 = [] (std::vector<double> beats)
    {
        for (auto& b : beats)
            b *= 0.5;

        return beats;
    };

    // Flex is off and the region has never been flexed: its hits are found
    // first, and only then does anything change, all as one undo step.
    {
        pumpMessages();
        const auto before = engine.getStateSnapshot();
        const auto undoBefore = engine.getUndoDescription();
        check (flex.quantize (region, { 1.0 / 4.0 }) == FlexQuantizeResult::findingHits, "Quantize with Flex off finds the hits first");
        check (flex.getFlex (region).quantizePending && engine.getStateSnapshot() == before && engine.getUndoDescription() == undoBefore,
               "...without changing the session (or its undo history) while it looks");
        check (flex.quantize (region, { 1.0 / 4.0 }) == FlexQuantizeResult::findingHits && engine.getStateSnapshot() == before,
               "asking again while it looks just waits for the same hits");

        waitForQuantize();
        check (! flex.getFlex (region).quantizePending, "(the hits were found)");
        check (engine.getUndoDescription() == "Quantize" && flex.isFlexOn (drums), "then it quantizes, turning Flex on, as one undo step");

        // Quarter notes: 8.16 is nearer beat 8 than 7.6 is, so 7.6 gets no
        // marker and moves along with the audio around it.
        const std::vector<double> expected { 5.0, 6.0, 7.0, -1.0, 8.0, 9.0 };
        bool onGrid = true;

        for (size_t i = 0; i < expected.size(); ++i)
            onGrid = onGrid && std::abs (markerBeatOf (i) - expected[i]) < 1.0e-6;

        check (onGrid, "each hit is on its quarter note (and two hits never share one)");

        engine.undo();
        pumpMessages();
        checkSameState (before, engine.getStateSnapshot(), "one undo takes Quantize and Flex back off");
        engine.redo();
        pumpMessages();
        check (flex.isFlexOn (drums) && std::abs (markerBeatOf (2) - 7.0) < 1.0e-6, "redo quantizes again");
        check (hitsAt ("quantized", atBpm120 ({ 5.0, 6.0, 7.0, 8.0, 9.0 })), "the export plays the quantized hits on the beat");
        engine.undo();
        pumpMessages();
    }

    // With Flex on (and the hits known), Quantize is immediate.
    flex.setFlexOn (drums, true);
    pumpMessages();

    for (int i = 0; i < 400 && flex.getFlex (region).analysing; ++i)
        juce::MessageManager::getInstance()->runDispatchLoopUntil (25);

    // (The step that first flexed the region was undone above: its warp
    // markers must come back whole, or the hits can't be shown or moved.)
    check (flex.getFlex (region).transients.size() == playedBeats.size() && clipOf (region)->state.getChildWithName (te::IDs::WARPTIME)
                                                                                .getChildWithName (te::IDs::WARPMARKERS).getNumChildren() == 2,
           "Flex back on after undoing the step that first flexed a region brings back its markers and hits");

    checkUndoRoundTrip (engine, "Quantize", [&] { flex.quantize (region, { 1.0 / 16.0, 0.5, 0.0 }); });
    // (The hits are found a fraction of a millisecond early, so halfway is
    // from there.)
    check (std::abs (markerBeatOf (0) - 5.05) < 0.005 && std::abs (markerBeatOf (2) - 6.805) < 0.005,
           "at 50% strength each hit moves halfway to its sixteenth");
    engine.undo();
    pumpMessages();

    checkUndoRoundTrip (engine, "Quantize", [&] { flex.quantize (region, { 1.0 / 8.0, 1.0, 1.0 }); });
    check (std::abs (markerBeatOf (3) - (7.5 + 0.5 / 3.0)) < 1.0e-6 && std::abs (markerBeatOf (2) - 7.0) < 1.0e-6,
           "with swing, hits near the second eighth go a triplet late");
    engine.undo();
    pumpMessages();

    // A selection: only the hits in it move, and the ones either side stay.
    checkUndoRoundTrip (engine, "Quantize", [&] { flex.quantize (region, { 1.0 / 4.0 }, juce::Range<double> (6.5, 7.5)); });
    {
        const auto info = flex.getFlex (region);
        bool othersStay = info.transients.size() == playedBeats.size();

        for (size_t i = 0; othersStay && i < playedBeats.size(); ++i)
            if (i != 2)
                othersStay = std::abs (info.transients[i].beat - playedBeats[i]) < 0.005;

        check (std::abs (markerBeatOf (2) - 7.0) < 1.0e-6 && othersStay, "only the selected hits are quantized");
        check (hitsAt ("selection quantized", atBpm120 ({ 5.1, 6.0, 7.0, 7.6, 8.16, 9.0 })), "...and only they play somewhere else");
    }
    engine.undo();
    pumpMessages();

    // Quantizing what's already on the grid changes nothing.
    flex.quantize (region, { 1.0 / 4.0 });
    pumpMessages();
    const auto onGridState = engine.getStateSnapshot();
    check (flex.quantize (region, { 1.0 / 4.0 }) == FlexQuantizeResult::alreadyOnGrid && engine.getStateSnapshot() == onGridState,
           "quantizing again changes nothing (and adds no undo step)");

    //==============================================================================
    std::cout << "Quantize: tempo and time signature\n";

    // Quantized hits stay on their beats at a new tempo (100 BPM: 0.6 s a beat).
    engine.setBpm (100.0);
    pumpMessages();
    check (std::abs (markerBeatOf (2) - 7.0) < 1.0e-6, "a new tempo keeps quantized hits on their beats");
    {
        std::vector<double> seconds;

        for (double b : { 5.0, 6.0, 7.0, 8.0, 9.0 })
            seconds.push_back (b * 0.6);

        check (hitsAt ("quantized, at 100 BPM", seconds), "...and plays them there");
    }
    engine.undo();   // the tempo
    engine.undo();   // the quantize
    pumpMessages();

    // A tempo change inside the region (80 BPM from beat 8, 4 s in): each
    // hit is quantized to the beat at its own tempo. (Made with Flex off, so
    // the recording keeps its timing in seconds: the hits after beat 8 play
    // on beats 8.11 and 8.67.)
    flex.setFlexOn (drums, false);
    engine.addTempoChange (2, 80.0);
    pumpMessages();
    checkUndoRoundTrip (engine, "Quantize", [&] { flex.quantize (region, { 1.0 / 4.0 }); });
    check (std::abs (markerBeatOf (4) - 8.0) < 1.0e-6 && std::abs (markerBeatOf (5) - 9.0) < 1.0e-6,
           "quantize follows a tempo change inside the region");
    check (hitsAt ("across a tempo change", { 2.5, 3.0, 3.5, 4.0, 4.75 }), "...and the hits play on those beats");
    engine.undo();   // the quantize
    engine.undo();   // the tempo change
    engine.undo();   // Flex Off
    pumpMessages();

    // In 6/8 a sixteenth is half a beat: 6.86 goes to 7, not 6.75.
    engine.setTimeSignature (6, 8);
    pumpMessages();
    checkUndoRoundTrip (engine, "Quantize", [&] { flex.quantize (region, { 1.0 / 16.0 }); });
    check (std::abs (markerBeatOf (2) - 7.0) < 1.0e-6, "quantize follows the time signature");
    engine.undo();
    pumpMessages();
    engine.setTimeSignature (4, 4);
    pumpMessages();

    flex.quantize (region, { 1.0 / 4.0 });
    pumpMessages();
    checkSaveAndReopen (engine, dir.getChildFile ("Quantize"));
    check (flex.isFlexOn (drums) && std::abs (markerBeatOf (2) - 7.0) < 1.0e-6, "quantized hits are saved with the session");

    // A region that can't be flexed can't be quantized either.
    regions.setRegionReversed (region, true);
    pumpMessages();
    check (flex.quantize (region, { 1.0 / 4.0 }) == FlexQuantizeResult::unavailable, "a reversed region can't be quantized");
    engine.undo();
    pumpMessages();

    flex.setFlexOn (drums, false);
    pumpMessages();
}

//==============================================================================
// Recordings follow the tempo (as in GarageBand), with Flex on or off: a
// click track recorded at 84 BPM, played at other tempos. Where each click
// lands in beats comes from the recorded file itself, so the checks don't
// depend on how the recording lines up with the timeline.
static void runFollowTempoSection (const juce::File& dir)
{
    namespace te = tracktion;
    std::cout << "Recordings follow the tempo\n";

    AudioEngine engine { testSettings, false };
    TestAudioDevice::Settings settings;
    auto device = std::make_unique<TestAudioDevice> (engine, settings);
    const auto rate = settings.sampleRate;

    auto& tracks = engine.getTracks();
    auto& regions = tracks.regions();
    auto& flex = tracks.flex();
    auto& takes = tracks.takes();
    juce::MessageManager::getInstance()->runDispatchLoopUntil (2500);
    runAudio (*device, 0.2);

    engine.setBpm (84.0);
    engine.setCountInEnabled (false);
    engine.setMetronomeEnabled (false);

    auto inputs = tracks.inputs().getAudioInputs();
    check (! inputs.empty(), "(the pretend device has inputs)");

    if (inputs.empty())
        return;

    const auto rec = tracks.addAudioTrack();
    tracks.inputs().setTrackInput (rec, inputs[0].id);
    tracks.setSelectedTrack (rec);
    tracks.inputs().setArmed (rec, true);
    runAudio (*device, 0.3);

    auto clipOf = [&] (RegionId id) { return dynamic_cast<te::WaveAudioClip*> (detail::findClip (TestAudioDevice::getEditOf (engine), id)); };

    // A click every 0.4 s of input (not on the beats: any timing will do).
    const InputMaker clicks = [&] (juce::int64 start, int num)
    {
        juce::AudioBuffer<float> b (settings.numInputs, num);
        b.clear();
        const auto every = (juce::int64) (0.4 * rate), width = (juce::int64) (0.001 * rate);

        for (int i = 0; i < num; ++i)
            if ((start + i) % every < width)
                for (int ch = 0; ch < b.getNumChannels(); ++ch)
                    b.setSample (ch, i, 0.5f);

        return b;
    };

    // One pass of a cycle recording over beats `from` to `to`, at `bpm`.
    auto recordPass = [&] (double from, double to, double bpm)
    {
        engine.setLooping (true);
        engine.setLoopRangeBeats (from, to);
        engine.setPositionBeats (from);
        engine.toggleRecord();
        const bool started = engine.getTransportState().recording;
        runAudio (*device, (to - from) * 60.0 / bpm + 0.05, clicks);
        engine.toggleRecord();
        pumpMessages();
        engine.setLooping (false);
        return started;
    };

    // Where the clicks of the region's current take are, in beats, as it
    // plays at `bpm` (the whole song's tempo) with its recording as it is.
    // (Not the ones right at its edges, where its fades are.)
    auto clickBeats = [&] (RegionId id, double bpm)
    {
        std::vector<double> beats;
        auto* clip = clipOf (id);

        if (clip == nullptr)
            return beats;

        const auto found = findClicks (clip->getOriginalFile());
        const auto pos = clip->getPosition();
        const double contentStart = pos.getStartOfSource().inSeconds();

        for (auto s : found.starts)
            if (const double t = contentStart + (double) s / found.sampleRate; t > pos.getStart().inSeconds() + 0.1 && t < pos.getEnd().inSeconds() - 0.1)
                beats.push_back (t * bpm / 60.0);

        return beats;
    };

    int exportNumber = 0;
    auto hitsOnBeats = [&] (const char* what, const std::vector<double>& beats, double bpm)
    {
        std::vector<double> expected;

        for (auto b : beats)
            expected.push_back (b * 60.0 / bpm);

        const auto worst = worstHitErrorMs (exportForHits (engine, dir.getChildFile ("Follow Export"), ++exportNumber), expected);
        std::cout << "  (" << what << ": " << beats.size() << " hits within " << worst << " ms)\n";
        return ! beats.empty() && worst < 5.0;
    };

    check (recordPass (8.0, 16.0, 84.0), "(recording)");
    auto list = regions.getRegions (rec);
    check (list.size() == 1, "(the recording makes a region)");

    if (list.size() != 1)
        return;

    const auto region = list[0].id;
    const auto recorded = clickBeats (region, 84.0);
    check (recorded.size() >= 10, "(with the clicks in it)");
    check (detail::isRecordingFile (clipOf (region)->getOriginalFile()) && detail::followsTempo (*clipOf (region)),
           "a recording follows the tempo, with Flex off");
    check (! clipOf (region)->getWarpTime() && ! clipOf (region)->state.getChildWithName (te::IDs::WARPTIME).isValid(),
           "at the tempo it was recorded at, it plays its recording as it is (nothing stretched or analysed)");
    check (hitsOnBeats ("at 84, as recorded", recorded, 84.0), "(its hits, where they were recorded)");

    // (Not checkUndoRoundTrip: the loop range used for recording is part of
    // the transport, which a tempo change moves without an undo step.)
    engine.setBpm (100.0);
    pumpMessages();
    check (std::abs (regions.getRegion (region)->lengthBeats - list[0].lengthBeats) < 1.0e-6 && clipOf (region)->getWarpTime(),
           "at a new tempo, the recording keeps its length in beats, stretched to fit");
    check (hitsOnBeats ("at 100, Flex off", recorded, 100.0), "...and every hit stays on its beat");

    engine.undo();
    pumpMessages();
    check (! clipOf (region)->getWarpTime() && hitsOnBeats ("undone, at 84", recorded, 84.0), "undoing the tempo change plays the recording as it is again");
    engine.redo();
    pumpMessages();

    //==============================================================================
    std::cout << "Recordings follow the tempo: Flex\n";

    checkUndoRoundTrip (engine, "Flex On", [&] { flex.setFlexOn (rec, true); });
    check (hitsOnBeats ("at 100, Flex on", recorded, 100.0), "turning Flex on leaves the hits on their beats");

    FlexInfo info = flex.getFlex (region);

    for (int i = 0; i < 400 && info.analysing; ++i)
    {
        juce::MessageManager::getInstance()->runDispatchLoopUntil (25);
        info = flex.getFlex (region);
    }

    check (! info.hasEdits, "following the tempo isn't a Flex edit (Reset Flex has nothing to do)");

    // The hit nearest the region's middle, a fifth of a beat later.
    auto edited = recorded;
    const auto middle = std::min_element (edited.begin(), edited.end(), [] (double a, double b) { return std::abs (a - 12.0) < std::abs (b - 12.0); });
    const auto hit = std::min_element (info.transients.begin(), info.transients.end(),
                                       [&] (const FlexMarker& a, const FlexMarker& b) { return std::abs (a.beat - *middle) < std::abs (b.beat - *middle); });
    check (hit != info.transients.end() && std::abs (hit->beat - *middle) < 0.01, "the hits are found where they play now");

    if (hit == info.transients.end())
        return;

    *middle += 0.2;
    check (flex.moveMarker (region, hit->sourceSeconds, *middle), "(moving a hit)");
    pumpMessages();
    check (hitsOnBeats ("a hit moved", edited, 100.0), "a hit in a stretched recording can be moved with Flex");

    checkUndoRoundTrip (engine, "Flex Off", [&] { flex.setFlexOn (rec, false); });
    check (! flex.getFlex (region).hasEdits && clipOf (region)->getWarpTime(), "Flex off sets the moved hit aside, and the recording still follows the tempo");
    check (hitsOnBeats ("Flex off again", recorded, 100.0), "...with every hit back on its recorded beat");

    engine.setBpm (90.0);
    pumpMessages();
    check (hitsOnBeats ("at 90, Flex off", recorded, 90.0), "a tempo change with Flex off keeps them there");

    checkUndoRoundTrip (engine, "Flex On", [&] { flex.setFlexOn (rec, true); });
    check (flex.getFlex (region).hasEdits && hitsOnBeats ("at 90, Flex back on", edited, 90.0),
           "Flex back on brings the moved hit back, on its beat at the new tempo");

    checkUndoRoundTrip (engine, "Reset Flex", [&] { flex.resetFlex (region); });
    check (! flex.getFlex (region).hasEdits && clipOf (region)->getWarpTime() && hitsOnBeats ("reset at 90", recorded, 90.0),
           "Reset Flex goes back to the recording's own beats, still following the tempo");
    flex.setFlexOn (rec, false);
    pumpMessages();

    //==============================================================================
    std::cout << "Recordings follow the tempo: imported files don't\n";
    {
        const auto other = tracks.addAudioTrack();
        const auto file = dir.getChildFile ("Imported Clicks.wav");
        writeClickWav (file, rate, 4.0, { 0.5, 1.0, 1.5 }, 0.5f);
        const auto imported = regions.importAudioFiles (other, { file.getFullPathName() }, 40.0).front();
        pumpMessages();
        const double length = regions.getRegion (imported)->lengthBeats;
        engine.setBpm (120.0);
        pumpMessages();
        check (! detail::followsTempo (*clipOf (imported)) && ! clipOf (imported)->getWarpTime()
                 && std::abs (regions.getRegion (imported)->lengthBeats - length * 120.0 / 90.0) < 1.0e-3,
               "an imported audio file keeps its own speed (its length in seconds)");
        engine.undo();
        pumpMessages();
        tracks.deleteTrack (other);
        pumpMessages();
    }

    //==============================================================================
    std::cout << "Recordings follow the tempo: takes\n";

    // The same beats again, at 90.
    check (recordPass (8.0, 16.0, 90.0), "(recording)");
    list = regions.getRegions (rec);
    check (list.size() == 1 && list[0].id == region && list[0].takeCount == 2 && list[0].currentTake == 1,
           "recording the same beats again at another tempo adds a take (the first one was stretched)");

    if (list.size() != 1 || list[0].takeCount != 2)
    {
        std::cout << "         " << list.size() << " regions, " << (list.empty() ? 0 : list[0].takeCount) << " takes\n";
        return;
    }

    const auto second = clickBeats (region, 90.0);
    check (! clipOf (region)->getWarpTime() && hitsOnBeats ("take 2, at 90", second, 90.0), "the new take plays as it was recorded");

    checkUndoRoundTrip (engine, "Choose Take", [&] { takes.setCurrentTake (region, 0); });
    check (clipOf (region)->getWarpTime() && hitsOnBeats ("take 1, at 90", recorded, 90.0), "the first take still follows the tempo");

    engine.setBpm (110.0);
    pumpMessages();
    check (hitsOnBeats ("take 1, at 110", recorded, 110.0), "...through another tempo change");
    takes.setCurrentTake (region, 1);
    pumpMessages();
    check (hitsOnBeats ("take 2, at 110", second, 110.0), "and so does the take that wasn't playing, from the tempo it was recorded at");

    // Recording over part of the stretched region: what it played there
    // becomes the new region's first take.
    check (recordPass (12.0, 16.0, 110.0), "(recording)");
    list = regions.getRegions (rec);
    check (list.size() == 2, "recording over part of a region cuts it back");


    if (list.size() != 2)
        for (auto& r : list)
            std::cout << "         (region at " << r.startBeat << ", " << r.lengthBeats << " beats, " << r.takeCount << " takes)\n";

    if (list.size() == 2)
    {
        const auto& added = list[0].startBeat > list[1].startBeat ? list[0] : list[1];
        check (added.takeCount == 2, "...and what it played there becomes a take of the new region");
        takes.setCurrentTake (added.id, 0);
        pumpMessages();

        std::vector<double> covered;

        for (auto b : second)
            if (b > 12.1 && b < 15.9)
                covered.push_back (b);

        check (hitsOnBeats ("covered take, at 110", covered, 110.0), "...playing the stretched audio it played");
        engine.undo();
        pumpMessages();
    }

    engine.undo();   // the recording
    pumpMessages();
    checkSaveAndReopen (engine, dir.getChildFile ("Follow Tempo"));
    check (hitsOnBeats ("reopened", second, 110.0), "a reopened session plays the takes the same");
}

//==============================================================================
// The tuner: the pitch detector on generated guitar notes, the steadying,
// and the engine side (which input it listens to, Mute while tuning).
static void runTunerSection()
{
    using juce::MathConstants;

    // A note as a frequency, `cents` away from in tune.
    auto noteHz = [] (int midiNote, double cents = 0.0) { return 440.0 * std::pow (2.0, (midiNote - 69 + cents / 100.0) / 12.0); };

    // A sine, or a sawtooth (every harmonic, like a bright pickup), starting
    // at sample `start`. The sawtooth is band-limited (harmonics summed up
    // to 10 kHz) so it doesn't alias, as a real one through a sound card wouldn't.
    auto toneSample = [] (double hz, double rate, juce::int64 i, bool saw) -> float
    {
        const auto phase = 2.0 * MathConstants<double>::pi * hz * (double) i / rate;

        if (! saw)
            return 0.4f * (float) std::sin (phase);

        double v = 0.0;

        for (int h = 1; h * hz < 10000.0; ++h)
            v += std::sin (h * phase) / h;

        return (float) (0.25 * v);
    };

    // Plays `seconds` of a tone into a tuner (as the audio thread would, in
    // 256-sample blocks), analysing every 50 ms (as its thread would), and
    // returns every reading.
    auto listen = [&] (Tuner& tuner, double rate, double seconds, const std::function<float (juce::int64)>& source)
    {
        std::vector<TunerReading> readings;
        std::vector<float> block (256);
        const auto total = (juce::int64) (seconds * rate);
        const auto analyseEvery = (juce::int64) (rate * 0.05);
        juce::int64 sinceAnalysis = 0;

        for (juce::int64 done = 0; done < total; done += (juce::int64) block.size())
        {
            for (size_t i = 0; i < block.size(); ++i)
                block[i] = source (done + (juce::int64) i);

            const float* channels[] = { block.data() };
            tuner.pushInput (channels, 1, (int) block.size());
            sinceAnalysis += (juce::int64) block.size();

            if (sinceAnalysis >= analyseEvery)
            {
                sinceAnalysis -= analyseEvery;
                tuner.analyse();
                readings.push_back (tuner.getReading());
            }
        }

        return readings;
    };

    juce::AudioDeviceManager noDevice;     // never opened: the test feeds the tuner itself

    std::cout << "Tuner: the pitch detector\n";
    {
        // Open strings in standard tuning, drop D and drop C, a 7-string's
        // low B, and some fretted notes up to the high E string's 24th fret.
        const int notes[] = { 40, 45, 50, 55, 59, 64,    // E2 A2 D3 G3 B3 E4
                              38, 36, 35,                // D2 (drop D), C2 (drop C), B1
                              52, 69, 76, 88 };          // E3, A4, E5, E6 (1319 Hz)
        const double detunings[] = { 0.0, -18.0, 9.0, -45.0, 33.0 };

        for (const double rate : { 44100.0, 48000.0, 96000.0 })
        {
            PitchDetector detector;
            detector.prepare (rate);
            std::vector<float> samples ((size_t) detector.getWindowSize());
            double worstCents = 0.0;
            bool allFound = true;

            for (bool saw : { false, true })
                for (int note : notes)
                    for (double detune : detunings)
                    {
                        const auto hz = noteHz (note, detune);

                        for (size_t i = 0; i < samples.size(); ++i)
                            samples[i] = toneSample (hz, rate, (juce::int64) i + 1000, saw);

                        auto found = detector.detect (samples.data(), (int) samples.size());
                        allFound = allFound && found.has_value();

                        if (found)
                        {
                            const auto p = NotePitch::fromFrequency (found->frequency);
                            const auto centsOff = (p.midiNote - note) * 100.0 + p.cents - detune;
                            worstCents = juce::jmax (worstCents, std::abs (centsOff));
                        }
                    }

            std::cout << "    (" << rate << " Hz: worst error " << juce::String (worstCents, 2) << " cents)\n";
            check (allFound, "every guitar note is found, sine and sawtooth, in tune and detuned");
            check (worstCents < 1.0, "...to within a cent, with no octave errors");
        }

        PitchDetector detector;
        detector.prepare (48000.0);
        std::vector<float> samples ((size_t) detector.getWindowSize(), 0.0f);
        check (! detector.detect (samples.data(), (int) samples.size()), "silence has no pitch");

        juce::Random random (1);

        for (auto& s : samples)
            s = random.nextFloat() * 0.5f - 0.25f;

        check (! detector.detect (samples.data(), (int) samples.size()), "nor does noise");

        // An E minor chord (E2, B2, E3, G3, B3, E4) is several notes at once.
        const int chord[] = { 40, 47, 52, 55, 59, 64 };

        for (size_t i = 0; i < samples.size(); ++i)
        {
            float v = 0.0f;

            for (int n : chord)
                v += toneSample (noteHz (n, n == 55 ? 14.0 : -7.0), 48000.0, (juce::int64) i, true) / 4.0f;

            samples[i] = v;
        }

        auto chordPitch = detector.detect (samples.data(), (int) samples.size());
        check (! chordPitch || NotePitch::fromFrequency (chordPitch->frequency).getNoteName() == "E",
               "a strummed chord shows nothing, or its root");

        auto e2 = NotePitch::fromFrequency (82.4069);
        check (e2.getNoteName() == "E" && e2.getOctave() == 2 && std::abs (e2.cents) < 0.1f, "82.4 Hz is E2");
        auto sharpA = NotePitch::fromFrequency (noteHz (45, 30.0));
        check (sharpA.getNoteName() == "A" && sharpA.getOctave() == 2 && std::abs (sharpA.cents - 30.0f) < 0.01f, "...and A2 30 cents sharp is A2 +30");
        check (NotePitch::fromFrequency (noteHz (61)).getNoteName() == "C#", "sharps are named with #");
    }

    std::cout << "Tuner: steady readings\n";
    {
        const double rate = 48000.0;

        // A plucked string: a sawtooth that fades away over two seconds,
        // then silence.
        auto pluck = [&] (int note, double cents)
        {
            const auto hz = noteHz (note, cents);
            return [=] (juce::int64 i) -> float
            {
                const auto t = (double) i / rate;
                return t < 2.0 ? (float) std::exp (-1.5 * t) * toneSample (hz, rate, i, true) : 0.0f;
            };
        };

        struct Case { int note; double cents; const char* what; };

        for (auto c : { Case { 40, 0.0, "low E" }, Case { 40, -18.0, "low E, 18 cents flat" },
                        Case { 38, -5.0, "drop D, 5 cents flat" }, Case { 36, 2.0, "drop C" },
                        Case { 55, 9.0, "G string, 9 cents sharp" } })
        {
            Tuner tuner (noDevice);
            tuner.prepare (rate);
            tuner.setInputChannels (0, -1);
            auto readings = listen (tuner, rate, 2.6, pluck (c.note, c.cents));

            // While the note sounds (after the first 150 ms): the right note,
            // the right cents, and a needle that holds still.
            bool rightNote = true;
            float lowest = 100.0f, highest = -100.0f;

            for (size_t i = 3; i < 40; ++i)
            {
                auto& r = readings[i];
                rightNote = rightNote && r.hasPitch && r.note.midiNote == c.note;
                lowest = juce::jmin (lowest, r.note.cents);
                highest = juce::jmax (highest, r.note.cents);
            }

            const auto label = juce::String (c.what);
            check (rightNote, (label + ": the right note the whole time").toRawUTF8());
            check (std::abs (lowest - c.cents) < 1.5 && std::abs (highest - c.cents) < 1.5,
                   (label + ": cents right to within 1.5, needle steady").toRawUTF8());
            check (readings[38].isInTune() == (std::abs (c.cents) <= TunerReading::inTuneCents),
                   (label + ": shows in tune only when within 3 cents").toRawUTF8());
            check (! readings.back().hasPitch, (label + ": blank once the string stops").toRawUTF8());
        }

        // A single wrong reading (a pick scratch) doesn't move the display.
        {
            Tuner tuner (noDevice);
            tuner.prepare (rate);
            tuner.setInputChannels (0, -1);
            const auto a = noteHz (45), e = noteHz (64);

            // A2, with 45 ms of high E in the middle (less than one reading apart).
            auto readings = listen (tuner, rate, 1.0, [&] (juce::int64 i)
            {
                const bool glitch = i >= (juce::int64) (0.5 * rate) && i < (juce::int64) (0.545 * rate);
                return toneSample (glitch ? e : a, rate, i, true);
            });

            bool held = true;

            for (size_t i = 3; i < readings.size(); ++i)
                held = held && readings[i].hasPitch && readings[i].note.midiNote == 45;

            check (held, "a moment of another pitch doesn't flicker the note");
        }

        // A string that's a quarter-tone out doesn't flicker between names.
        {
            Tuner tuner (noDevice);
            tuner.prepare (rate);
            tuner.setInputChannels (0, -1);
            const auto hz = noteHz (50, 50.0);    // halfway between D3 and D#3
            auto readings = listen (tuner, rate, 1.0, [&] (juce::int64 i)
            {
                // ...and wavering 3 cents either side, 3 times a second.
                // (The phase is the frequency added up over time.)
                const auto t = (double) i / rate, depth = 0.0017, wobbleHz = 3.0;
                const auto phase = 2.0 * MathConstants<double>::pi * hz * t
                                     - hz * depth / wobbleHz * std::cos (2.0 * MathConstants<double>::pi * wobbleHz * t);
                return 0.4f * (float) std::sin (phase);
            });

            std::set<int> namesShown;

            for (size_t i = 3; i < readings.size(); ++i)
                if (readings[i].hasPitch)
                    namesShown.insert (readings[i].note.midiNote);

            check (namesShown.size() == 1, "a note between two others keeps one name");
        }

        // Noise shows nothing at all.
        {
            Tuner tuner (noDevice);
            tuner.prepare (rate);
            tuner.setInputChannels (0, -1);
            juce::Random random (7);
            auto readings = listen (tuner, rate, 1.0, [&] (juce::int64) { return random.nextFloat() * 0.4f - 0.2f; });
            check (std::none_of (readings.begin(), readings.end(), [] (auto& r) { return r.hasPitch; }), "noise shows nothing");
        }

        // A stereo input: both sides are heard (here the note is only on the right).
        {
            Tuner tuner (noDevice);
            tuner.prepare (rate);
            tuner.setInputChannels (0, 1);
            std::vector<float> left (256, 0.0f), right (256);

            for (juce::int64 done = 0; done < (juce::int64) rate / 2; done += 256)
            {
                for (size_t i = 0; i < right.size(); ++i)
                    right[i] = toneSample (noteHz (45), rate, done + (juce::int64) i, false);

                const float* channels[] = { left.data(), right.data() };
                tuner.pushInput (channels, 2, 256);

                if (done % 2304 == 0)
                    tuner.analyse();
            }

            check (tuner.getReading().hasPitch && tuner.getReading().note.midiNote == 45, "a stereo input is heard on either side");

            tuner.setInputChannels (-1, -1);
            check (! tuner.hasInput(), "no input channels: nothing to listen to");
        }
    }

    std::cout << "Tuner: in the engine\n";
    {
        AudioEngine engine { testSettings, false };
        TestAudioDevice::Settings settings;
        auto device = std::make_unique<TestAudioDevice> (engine, settings);
        auto& tracks = engine.getTracks();
        auto& tuner = engine.getTuner();
        juce::MessageManager::getInstance()->runDispatchLoopUntil (2500);
        runAudio (*device, 0.2);

        const auto midi = tracks.getTracks().front().id;
        auto inputs = tracks.inputs().getAudioInputs();
        check (inputs.size() == 3, "(the device's inputs: 1, 2, 1+2)");

        if (inputs.size() < 3)
            return;

        auto guitar = tracks.addAudioTrack();
        tracks.inputs().setTrackInput (guitar, inputs[0].id);
        tracks.inputs().setMonitoring (guitar, true);
        tracks.setSelectedTrack (guitar);
        runAudio (*device, 0.2);
        check (tuner.hasInput(), "it listens to the selected audio track's input");

        tracks.setSelectedTrack (midi);
        check (! tuner.hasInput(), "an instrument track has no input for it");
        tracks.setSelectedTrack (guitar);

        auto heard = [&] { runAudio (*device, 0.2); return peakOf (device->process (tone (*device, 0.5f) (0, (int) settings.sampleRate / 2))); };
        check (heard() > 0.1f, "(the monitored input is heard)");

        // Turning the tuner on is a listening change, not an edit: nothing to undo or save.
        const auto before = engine.getStateSnapshot();
        const auto undoBefore = engine.getUndoDescription();
        tuner.setActive (true);
        check (tuner.isActive() && heard() < 0.001f, "while tuning, the input is muted (the default)");
        check (engine.getStateSnapshot() == before && engine.getUndoDescription() == undoBefore,
               "turning the tuner on doesn't change the session or add an undo step");

        engine.getPreferences().set (settings::muteWhileTuning, false);
        check (heard() > 0.1f, "with Mute while tuning off, it's heard while tuning");
        engine.getPreferences().set (settings::muteWhileTuning, true);
        check (heard() < 0.001f, "(muted again)");

        // Its own thread reads the notes (fed here straight into the tuner:
        // the test device shares its input and output buffers, so a callback
        // after Tracktion's sees the output, see AudioEngine::reattachAudioCallbacks).
        std::vector<float> block (256);
        juce::int64 done = 0;
        const auto until = juce::Time::getMillisecondCounter() + 2000;

        while (! tuner.getReading().hasPitch && juce::Time::getMillisecondCounter() < until)
        {
            for (size_t i = 0; i < block.size(); ++i)
                block[i] = toneSample (noteHz (50), settings.sampleRate, done + (juce::int64) i, true);

            done += (juce::int64) block.size();
            const float* channels[] = { block.data() };
            tuner.pushInput (channels, 1, (int) block.size());
            juce::Thread::sleep (5);   // about real time
        }

        check (tuner.getReading().hasPitch && tuner.getReading().note.midiNote == 50, "its thread finds the note (D3)");

        tuner.setActive (false);
        check (! tuner.getReading().hasPitch, "off, it shows nothing");
        check (heard() > 0.1f, "and the input is heard again");
        check (engine.getStateSnapshot() == before, "the session is as it was");
    }
}

//==============================================================================
// The pedalboard: the pedal style table, pedals kept at the start of an
// audio track's chain, room for 12 effects, the Echo's Time knob, undo and
// saving, and older sessions keeping their order.
static void runPedalsSection (const juce::File& dir)
{
    AudioEngine engine { testSettings, false };
    auto device = std::make_unique<TestAudioDevice> (engine, TestAudioDevice::Settings {});
    const auto rate = device->getSettings().sampleRate;
    auto& tracks = engine.getTracks();
    auto& effects = tracks.effects();
    juce::MessageManager::getInstance()->runDispatchLoopUntil (2500);
    runAudio (*device, 0.2);

    auto t = tracks.addAudioTrack();
    pumpMessages();

    // The chain's plugin types, e.g. "chorus,delay,reverb".
    auto typesOn = [&] (TrackId track)
    {
        juce::StringArray types;

        for (auto& slot : effects.getEffectChain (track))
            types.add (slot.type);

        return types.joinIntoString (",");
    };

    auto removeAll = [&] (TrackId track)
    {
        for (auto& slot : effects.getEffectChain (track))
            effects.removeEffect (track, slot.id);
    };

    auto indexOfParameter = [&] (TrackId track, PluginId plugin, const juce::String& id)
    {
        auto params = effects.getEffectParameters (track, plugin);

        for (int i = 0; i < (int) params.size(); ++i)
            if (params[(size_t) i].id == id)
                return i;

        return -1;
    };

    std::cout << "Pedal styles\n";
    {
        auto available = effects.getAvailableEffects();
        bool offered = true, knobCounts = true, knobsFound = true;
        juce::StringArray names;

        for (auto& style : getPedalStyles())
        {
            offered = offered && std::any_of (available.begin(), available.end(), [&] (const EffectType& e)
                                              { return e.type == style.pluginType && ! e.isThirdParty; });
            knobCounts = knobCounts && style.knobs.size() >= 2 && style.knobs.size() <= 3;
            names.addIfNotAlreadyThere (style.name);

            const auto plugin = effects.addEffect (t, style.pluginType);

            for (auto& knob : style.knobs)
            {
                if (indexOfParameter (t, plugin, knob.parameterId) < 0)
                {
                    knobsFound = false;
                    std::cout << "         " << style.name << " has no \"" << knob.parameterId << "\" setting\n";
                }
            }

            effects.removeEffect (t, plugin);
        }

        check (getPedalStyles().size() == 20 && names.size() == 20, "there are twenty pedals, each with its own name");
        check (offered, "every pedal skins an effect from the effects menu");
        check (knobCounts, "each has 2 or 3 knobs");
        check (knobsFound, "and every knob turns a setting its effect has");
        check (findPedalStyle ("reverb") == nullptr && findPedalStyle ("ikarosLimiter") == nullptr,
               "Reverb and Limiter aren't pedals");
    }

    std::cout << "Which tracks have a pedalboard\n";
    {
        TrackId guitar = 0;
        checkUndoRoundTrip (engine, "Add Guitar Track", [&] { guitar = tracks.addGuitarTrack(); });
        const auto info = tracks.getTrack (guitar);
        check (info.has_value() && info->kind == TrackKind::audio && info->name == "Guitar 1" && info->icon == "guitar",
               "Guitar or Bass makes an audio track called Guitar 1, with the guitar icon");
        check (effects.hasPedalboard (guitar), "which has a pedalboard");

        check (! effects.hasPedalboard (t), "a plain audio track (a microphone) doesn't");
        tracks.setTrackIcon (t, "bass");
        check (effects.hasPedalboard (t), "one with the bass icon does");
        tracks.setTrackIcon (t, "mic");
        check (! effects.hasPedalboard (t), "and with the microphone icon it doesn't again");

        const auto chorus = effects.addEffect (t, "chorus");
        check (effects.hasPedalboard (t), "an audio track with a pedal keeps its pedalboard, whatever its icon");
        effects.removeEffect (t, chorus);
        effects.addEffect (t, "reverb");
        check (! effects.hasPedalboard (t), "(other effects don't count)");
        removeAll (t);
        tracks.setTrackIcon (t, {});

        const auto instrument = tracks.addInstrumentTrack();
        tracks.setTrackIcon (instrument, "guitar");
        effects.addEffect (instrument, "chorus");
        check (! effects.hasPedalboard (instrument) && ! effects.hasPedalboard (masterTrackId),
               "instrument tracks and the master never do");
        tracks.deleteTrack (instrument);
        tracks.deleteTrack (guitar);
        pumpMessages();
    }

    std::cout << "Showing and hiding the pedalboard\n";
    {
        check (! effects.hasPedalboard (t), "(a vocal track has none to start with)");
        checkUndoRoundTrip (engine, "Show Pedalboard", [&] { effects.setPedalboardShown (t, true); });
        check (effects.hasPedalboard (t), "any audio track can show one");

        const auto guitar = tracks.addGuitarTrack();
        const auto chorus = effects.addEffect (guitar, "chorus");
        checkUndoRoundTrip (engine, "Hide Pedalboard", [&] { effects.setPedalboardShown (guitar, false); });
        check (! effects.hasPedalboard (guitar) && effects.getEffectChain (guitar).size() == 2,
               "a guitar track can hide its pedalboard (and keeps its pedal and amp)");

        const auto instrument = tracks.addInstrumentTrack();
        effects.setPedalboardShown (instrument, true);
        check (! effects.hasPedalboard (instrument), "an instrument track can't have one");

        const auto session = dir.getChildFile ("Pedalboards");
        check (engine.saveSessionAs (session), "(save)");
        check (engine.openSession (AudioEngine::getSessionFileFor (session)), "(reopen)");
        pumpMessages();
        auto list = tracks.getTracks();
        TrackId vocal = 0, guitarAgain = 0;

        for (auto& info : list)
        {
            if (info.kind == TrackKind::audio && info.icon == "guitar")     guitarAgain = info.id;
            else if (info.kind == TrackKind::audio)                         vocal = info.id;
        }

        check (vocal != 0 && guitarAgain != 0 && effects.hasPedalboard (vocal) && ! effects.hasPedalboard (guitarAgain),
               "shown and hidden pedalboards survive save and reopen");

        // Back to just the vocal track for the checks below (they find it as
        // the last track).
        juce::ignoreUnused (chorus);
        // Every ID in the session as it opened (the highest is the guitar's
        // or the instrument track's, about to be deleted).
        std::uint64_t highestId = 0;

        for (auto& itemId : tracktion::EditItemID::findAllIDs (TestAudioDevice::getEditOf (engine)->state))
            highestId = std::max<std::uint64_t> (highestId, itemId.getRawID());

        tracks.deleteTrack (guitarAgain);
        tracks.deleteTrack (list.back().id);   // the instrument track added above

        // A new effect never reuses a deleted track's IDs: its plug-ins live
        // on (in the undo history, and for a while in Tracktion's cache).
        const auto added = effects.addEffect (vocal, "reverb");
        check (added > highestId, "an effect added after deleting tracks gets a new ID, not a deleted plug-in's");
        effects.removeEffect (vocal, added);
        t = vocal;
        effects.setPedalboardShown (t, false);
        removeAll (t);
        pumpMessages();
    }

    std::cout << "Pedals come first\n";
    {
        const auto reverb = effects.addEffect (t, "reverb");
        const auto chorus = effects.addEffect (t, "chorus");
        const auto echo = effects.addEffect (t, "delay");
        check (typesOn (t) == "chorus,delay,reverb", "a pedal added at the end goes after the last pedal, before other effects");

        effects.moveEffect (t, chorus, 2);
        check (typesOn (t) == "delay,chorus,reverb", "a pedal moved past the other effects stops after the last pedal");
        effects.moveEffect (t, reverb, 0);
        check (typesOn (t) == "delay,chorus,reverb", "another effect can't go in front of the pedals");
        effects.addEffect (t, "pitchShifter", 0);
        check (typesOn (t) == "delay,chorus,pitchShifter,reverb", "nor be added there");

        effects.replaceEffect (t, reverb, "phaser");
        check (typesOn (t) == "delay,chorus,phaser,pitchShifter", "an effect replaced by a pedal joins the pedals");
        effects.replaceEffect (t, echo, "reverb");
        check (typesOn (t) == "chorus,phaser,reverb,pitchShifter", "a pedal replaced by another effect leaves them");

        // Instrument tracks have no pedalboard: their effects go where they're put.
        auto instrument = tracks.addInstrumentTrack();
        effects.addEffect (instrument, "reverb");
        effects.addEffect (instrument, "chorus");
        check (typesOn (instrument) == "reverb,chorus", "an instrument track's effects stay in any order");
        tracks.deleteTrack (instrument);
        removeAll (t);
    }

    std::cout << "Up to 12 effects\n";
    {
        check (EffectsController::maxEffectsPerTrack == 12, "a track has room for 12 effects");
        int added = 0;

        for (int i = 0; i < 12; ++i)
            added += effects.addEffect (t, i % 2 == 0 ? "chorus" : "reverb") != 0 ? 1 : 0;

        check (added == 12, "12 can be added");
        check (effects.addEffect (t, "phaser") == 0 && effects.getEffectChain (t).size() == 12, "a 13th can't");
        removeAll (t);
    }

    std::cout << "Tape Echo's time\n";
    {
        auto inputs = tracks.inputs().getAudioInputs();
        check (! inputs.empty(), "(the pretend device has inputs)");

        if (inputs.empty())
            return;

        tracks.inputs().setTrackInput (t, inputs[0].id);
        tracks.setSelectedTrack (t);
        tracks.inputs().setArmed (t, true);
        tracks.inputs().setMonitoring (t, true);

        const auto echo = effects.addEffect (t, "delay");
        const auto time = indexOfParameter (t, echo, "length");
        check (time >= 0 && effects.getEffectParameters (t, echo)[(size_t) time].valueText == "150 ms",
               "the Echo has a Time setting, 150 ms to start with");

        if (time < 0)
            return;

        effects.setEffectParameter (t, echo, indexOfParameter (t, echo, "feedback"), 0.0f);   // one repeat
        effects.setEffectParameter (t, echo, indexOfParameter (t, echo, "mix proportion"), 0.5f);
        runAudio (*device, 0.5);

        engine.beginUndoStep ("Tape Echo Time");
        effects.setEffectParameter (t, echo, time, (500.0f - 20.0f) / (2000.0f - 20.0f));
        check (effects.getEffectParameters (t, echo)[(size_t) time].valueText == "500 ms", "it can be set to 500 ms");

        // A click goes in; the first sound out is the click, the next its echo.
        juce::AudioBuffer<float> click (device->getSettings().numInputs, (int) (rate * 0.8));
        click.clear();

        for (int ch = 0; ch < click.getNumChannels(); ++ch)
            for (int i = 0; i < 32; ++i)
                click.setSample (ch, i, 0.5f);

        runAudio (*device, 0.6);   // (let the old echoes die away)
        const auto heard = device->process (click);
        int first = -1, second = -1;

        for (int i = 0; i < heard.getNumSamples(); ++i)
        {
            if (std::abs (heard.getSample (0, i)) < 0.05f)
                continue;

            if (first < 0)
                first = i;
            else if (i > first + (int) (rate * 0.1))
            {
                second = i;
                break;
            }
        }

        const auto gapMs = first >= 0 && second >= 0 ? (second - first) * 1000.0 / rate : 0.0;
        std::cout << "    (the echo comes " << gapMs << " ms after the click)\n";
        check (std::abs (gapMs - 500.0) < 2.0, "and the echo comes back 500 ms later");

        engine.undo();
        check (effects.getEffectParameters (t, echo)[(size_t) time].valueText == "150 ms", "undo puts it back");
        removeAll (t);
        tracks.inputs().setMonitoring (t, false);
    }

    std::cout << "Undo and saving\n";
    {
        PluginId chorus = 0, echo = 0, phaser = 0;
        checkUndoRoundTrip (engine, "Add Effect", [&] { chorus = effects.addEffect (t, "chorus"); });
        echo = effects.addEffect (t, "delay");
        effects.addEffect (t, "reverb");
        pumpMessages();

        checkUndoRoundTrip (engine, "Turn Effect Off", [&] { effects.setEffectEnabled (t, echo, false); });
        checkUndoRoundTrip (engine, "Move Effect", [&] { effects.moveEffect (t, echo, 0); });
        checkUndoRoundTrip (engine, "Replace Effect", [&] { phaser = effects.replaceEffect (t, chorus, "phaser"); });
        checkUndoRoundTrip (engine, "Orbit Phaser Rate", [&]
        {
            engine.beginUndoStep ("Orbit Phaser Rate");
            effects.setEffectParameter (t, phaser, indexOfParameter (t, phaser, "rate"), 0.8f);
        });
        checkUndoRoundTrip (engine, "Tape Echo Time", [&]
        {
            engine.beginUndoStep ("Tape Echo Time");
            effects.setEffectParameter (t, echo, indexOfParameter (t, echo, "length"), 0.25f);
        });

        checkSaveAndReopen (engine, dir.getChildFile ("Pedals"));
        t = tracks.getTracks().back().id;
        auto chain = effects.getEffectChain (t);
        check (typesOn (t) == "delay,phaser,reverb" && chain.size() == 3 && ! chain[0].enabled && chain[1].enabled,
               "the pedals, their order and their on/off state survive save and reopen");

        if (chain.size() == 3)
            check (effects.getEffectParameters (t, chain[0].id)[(size_t) indexOfParameter (t, chain[0].id, "length")].valueText == "515 ms",
                   "and so do their knobs");
    }

    std::cout << "Older sessions keep their order\n";
    {
        // A session from before pedals came first: a chorus after a reverb
        // (put there behind the controller's back).
        removeAll (t);
        effects.addEffect (t, "reverb");
        {
            // (The Plugin::Ptr must be let go before the Edit goes, below.)
            auto* edit = TestAudioDevice::getEditOf (engine);
            auto* track = detail::findTrack (edit, t);
            auto chorus = edit->getPluginCache().createNewPlugin (tracktion::ChorusPlugin::xmlTypeName, {});
            auto* reverb = track->pluginList.findFirstPluginOfType<tracktion::ReverbPlugin>();
            track->pluginList.insertPlugin (chorus, track->pluginList.indexOf (reverb) + 1, nullptr);
        }

        check (typesOn (t) == "reverb,chorus", "(made one)");

        const auto session = dir.getChildFile ("Old Order");
        check (engine.saveSessionAs (session), "(save)");
        check (engine.openSession (AudioEngine::getSessionFileFor (session)), "(reopen)");
        pumpMessages();
        t = tracks.getTracks().back().id;
        check (typesOn (t) == "reverb,chorus", "it opens with its effects in the order they were saved in");

        // Moving its chorus on the pedalboard brings it to the front.
        effects.moveEffect (t, effects.getEffectChain (t)[1].id, 0);
        check (typesOn (t) == "chorus,reverb", "moving its pedal puts it with the pedals");
        removeAll (t);
    }

    std::cout << "Ported pedals sound like the originals\n";
    {
        // Each port plays tests/fixtures/pedals/di_guitar.wav at the settings
        // tools/pedals/render_references.sh rendered the original LV2 plugin
        // at, and the difference from that render must be near silence: at
        // least `belowDb` quieter than the render itself.
        //
        // How near depends on the port:
        //   - It runs the original DSP unchanged: it matches to rounding (it
        //     differs only in compiler flags), so 80 dB leaves plenty of room.
        //   - We oversample a stage with juce::dsp::Oversampling where the
        //     original used zita-resampler (Mellow Drive, Vintage Fuzz, Super
        //     Fuzz): the two resamplers' filters differ in delay (by a
        //     fraction of a sample, too) and near the top of the hearing
        //     range, and the clipping after them exaggerates that. The best
        //     whole-sample lag (within 256) is used. Measured: the two fuzzes
        //     about 38 dB (only their clipping is oversampled), so 30 dB.
        //     Mellow Drive about 22 dB, whatever filters we use: the
        //     original's 2x resampler primes its down-sampler with one
        //     sample fewer than its up-sampler, which seems to leave it half
        //     a sample late, and no whole-sample lag lines that up. So 18
        //     dB. These ports sound alike, not identical.
        //   - The Golden Boost: its original is a JUCE plugin, built with an
        //     older JUCE, so the same code rounds a little differently
        //     (measured about 76 dB): 60 dB.
        // The first setting listed is also turned a little, to check the
        // test would notice a difference: then it must null at least 6 dB
        // worse. So that setting must not be a switch.
        struct Reference
        {
            juce::String type, file;
            std::vector<std::pair<juce::String, float>> settings;   // parameter ID, value
            double belowDb;
            bool align = false;      // look for the best lag
            bool stereo = false;     // a stereo DSP: its right channel isn't silent
        };

        const std::vector<Reference> references {
            { YellowDrivePlugin::xmlTypeName, "yellow_drive.wav", { { "drive", 0.7f }, { "level", -6.0f }, { "tone", 650.0f } }, 80.0 },
            { StackDrivePlugin::xmlTypeName, "stack_drive.wav",
              { { "gain", 0.7f }, { "bass", 0.5f }, { "level", 0.5f }, { "mid", 0.6f }, { "treble", 0.5f } }, 80.0 },
            { HotPlatePlugin::xmlTypeName, "hot_plate.wav", { { "gain", 0.6f }, { "bass", 0.5f }, { "treble", 0.6f }, { "volume", 0.3f } }, 80.0 },
            { TubeDrivePlugin::xmlTypeName, "tube_drive.wav", { { "gain", 0.6f }, { "bass", 0.5f }, { "treble", 0.6f }, { "volume", 0.5f } }, 80.0 },
            { MellowDrivePlugin::xmlTypeName, "mellow_drive.wav", { { "drive", 0.6f }, { "glass", 0.5f }, { "level", 0.5f } }, 18.0, true },
            { SunFuzzPlugin::xmlTypeName, "sun_fuzz.wav", { { "drive", 0.7f }, { "input", 0.5f }, { "volume", 0.5f } }, 80.0 },
            { VintageFuzzPlugin::xmlTypeName, "vintage_fuzz.wav", { { "fuzz", 0.7f }, { "tone", 0.5f }, { "volume", 0.5f } }, 30.0, true },
            { SuperFuzzPlugin::xmlTypeName, "super_fuzz.wav",
              { { "expander", 0.6f }, { "balance", 0.5f }, { "tone", 0.0f }, { "trim", 0.25f } }, 30.0, true },
            { EnvelopeWahPlugin::xmlTypeName, "envelope_wah.wav",
              { { "depth", 0.8f }, { "drive", 0.0f }, { "gain", 0.0f }, { "mode", 2.0f }, { "peak", 8.0f }, { "range", 1.0f }, { "tone", 0.0f } }, 80.0 },
            { SlowSwellPlugin::xmlTypeName, "slow_swell.wav", { { "uptime", 200.0f }, { "downtime", 5.0f }, { "threshold", 1.0f } }, 80.0 },
            { JetFlangerPlugin::xmlTypeName, "jet_flanger.wav",
              { { "depth", 1.0f }, { "width", 5.0f }, { "freq", 0.5f }, { "feedback", -0.707f }, { "wet", 100.0f }, { "mix", 0.0f } }, 80.0 },
            { SpringTankPlugin::xmlTypeName, "spring_tank.wav",
              { { "dry_wet", 50.0f }, { "lfo", 0.2f }, { "roomsize", 0.6f }, { "damp", 0.4f }, { "mode", 0.0f } }, 80.0, false, true },
            { GoldenBoostPlugin::xmlTypeName, "golden_boost.wav", { { "gain", 0.6f }, { "treble", 0.6f }, { "level", 0.5f } }, 60.0, true },
        };

        // render_references.sh plays this much silence first and leaves it
        // out (the originals fade in, and their knobs glide to the settings).
        constexpr int preroll = 48000, blockSize = 512;
        const auto folder = juce::File (IKAROS_TEST_FIXTURES).getChildFile ("pedals");

        auto readWav = [] (const juce::File& file)
        {
            juce::WavAudioFormat wav;
            std::unique_ptr<juce::AudioFormatReader> reader (wav.createReaderFor (file.createInputStream().release(), true));
            juce::AudioBuffer<float> samples (1, reader != nullptr ? (int) reader->lengthInSamples : 0);

            if (reader != nullptr)
                reader->read (&samples, 0, samples.getNumSamples(), 0, true, false);

            return samples;
        };

        auto rms = [] (const juce::AudioBuffer<float>& b, int ch)
        {
            return b.getNumSamples() > 0 ? (double) b.getRMSLevel (ch, 0, b.getNumSamples()) : 0.0;
        };

        // Plays `guitar` through the plugin's left channel, and silence
        // through its right, in blocks the way Tracktion does.
        auto render = [&] (tracktion::Plugin& plugin, double sampleRate, const juce::AudioBuffer<float>& guitar)
        {
            plugin.initialise ({ tracktion::TimePosition(), sampleRate, blockSize });
            plugin.reset();

            juce::AudioBuffer<float> buffer (2, preroll + guitar.getNumSamples());
            buffer.clear();
            buffer.copyFrom (0, preroll, guitar, 0, 0, guitar.getNumSamples());

            for (int start = 0; start < buffer.getNumSamples(); start += blockSize)
            {
                tracktion::PluginRenderContext context (&buffer, start, std::min (blockSize, buffer.getNumSamples() - start),
                                                        nullptr, 0.0, {}, true, false, true, false);
                plugin.applyToBuffer (context);
            }

            juce::AudioBuffer<float> played (2, guitar.getNumSamples());

            for (int ch = 0; ch < 2; ++ch)
                played.copyFrom (ch, 0, buffer, ch, preroll, guitar.getNumSamples());

            return played;
        };

        // How far (dB) the difference between the left channel of `played`
        // and `original` is below `original`. With `align`, `played` may be
        // a little late or early (the oversampling filters' delay), so the
        // best lag within 256 samples is used.
        auto nullDepth = [] (const juce::AudioBuffer<float>& played, const juce::AudioBuffer<float>& original, bool align)
        {
            const int n = original.getNumSamples(), margin = align ? 256 : 0;
            const float* a = played.getReadPointer (0);
            const float* b = original.getReadPointer (0);
            double reference = 0.0, best = std::numeric_limits<double>::max();

            for (int i = margin; i < n - margin; ++i)
                reference += (double) b[i] * b[i];

            for (int lag = -margin; lag <= margin; ++lag)
            {
                double error = 0.0;

                for (int i = margin; i < n - margin; ++i)
                {
                    const double d = (double) a[i + lag] - b[i];
                    error += d * d;
                }

                best = std::min (best, error);
            }

            return 10.0 * std::log10 (reference / std::max (best, 1.0e-30));
        };

        const auto guitar = readWav (folder.getChildFile ("di_guitar.wav"));
        check (guitar.getNumSamples() == 96000, "(the DI guitar is two seconds long)");

        for (auto& reference : references)
        {
            const auto original = readWav (folder.getChildFile (reference.file));
            auto* edit = TestAudioDevice::getEditOf (engine);
            // (The Plugin::Ptr must be let go before the Edit goes.)
            auto plugin = edit->getPluginCache().createNewPlugin (reference.type, {});
            const auto name = plugin != nullptr ? plugin->getName() : reference.type;

            if (plugin == nullptr || original.getNumSamples() != guitar.getNumSamples())
            {
                check (false, (name + " can be made, and has a reference render").toRawUTF8());
                continue;
            }

            for (auto& [id, value] : reference.settings)
                if (auto param = plugin->getAutomatableParameterByID (id))
                    param->setParameter (value, juce::sendNotificationSync);

            const auto played = render (*plugin, 48000.0, guitar);
            const auto belowDb = nullDepth (played, original, reference.align);
            const auto latencyMs = plugin->getLatencySeconds() * 1000.0;
            std::cout << "    (" << name << ": the difference is " << juce::String (belowDb, 1) << " dB below the original"
                      << (latencyMs > 0.0 ? ", latency " + juce::String (latencyMs, 2) + " ms" : juce::String()) << ")\n";
            check (belowDb >= reference.belowDb, (name + " sounds like the original (the difference is near silence)").toRawUTF8());

            if (! reference.stereo)
                check (rms (played, 1) < 1.0e-9, (name + ": each channel has its own copy (the silent right stays silent)").toRawUTF8());

            check ((latencyMs == 0.0) != reference.align, (name + (reference.align ? ": its oversampling's latency is reported"
                                                                                   : ": no latency")).toRawUTF8());

            // A null test that can't fail proves nothing: with its first
            // knob turned a little, the difference must be clearly audible.
            if (auto param = plugin->getAutomatableParameterByID (reference.settings.front().first))
            {
                const auto range = param->getValueRange();
                param->setParameter (reference.settings.front().second + 0.1f * (range.getEnd() - range.getStart()), juce::sendNotificationSync);
                const auto nudgedDepth = nullDepth (render (*plugin, 48000.0, guitar), original, reference.align);
                std::cout << "    (with " << reference.settings.front().first << " turned a little: " << juce::String (nudgedDepth, 1) << " dB)\n";
                check (nudgedDepth < juce::jmin (40.0, belowDb - 6.0),
                       (name + ": (and with a knob turned a little, it doesn't null)").toRawUTF8());
                param->setParameter (reference.settings.front().second, juce::sendNotificationSync);
            }

            // At 44.1 kHz there's no reference, but it should sound much the same.
            const auto at44 = render (*plugin, 44100.0, guitar);
            const auto levelDifference = juce::Decibels::gainToDecibels (rms (at44, 0)) - juce::Decibels::gainToDecibels (rms (played, 0));
            check (std::isfinite (rms (at44, 0)) && std::abs (levelDifference) < 2.0f, (name + " works at 44.1 kHz too").toRawUTF8());
        }
    }

    std::cout << "All twenty pedals at once\n";
    {
        // How much of the computer's time the pedals need: two seconds of
        // stereo guitar through all twenty, one after another, played
        // directly (a track holds at most 12 effects). A Debug build is far
        // slower than the app, so the check only applies to Release builds.
        auto* edit = TestAudioDevice::getEditOf (engine);
        juce::AudioBuffer<float> audio (2, 96000);
        audio.clear();
        juce::WavAudioFormat wav;
        const auto fixture = juce::File (IKAROS_TEST_FIXTURES).getChildFile ("pedals/di_guitar.wav");

        if (std::unique_ptr<juce::AudioFormatReader> reader { wav.createReaderFor (fixture.createInputStream().release(), true) })
            reader->read (&audio, 0, 96000, 0, true, true);   // the mono file into both channels
        double totalSeconds = 0.0;
        juce::StringArray slowest;

        for (auto& style : getPedalStyles())
        {
            auto plugin = edit->getPluginCache().createNewPlugin (style.pluginType, {});

            if (plugin == nullptr)
                continue;

            plugin->initialise ({ tracktion::TimePosition(), 48000.0, 512 });
            const auto start = juce::Time::getHighResolutionTicks();

            for (int at = 0; at < audio.getNumSamples(); at += 512)
            {
                tracktion::PluginRenderContext context (&audio, at, std::min (512, audio.getNumSamples() - at),
                                                        nullptr, 0.0, {}, true, false, true, false);
                plugin->applyToBuffer (context);
            }

            const auto seconds = juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - start);
            totalSeconds += seconds;
            slowest.add (juce::String (seconds / 2.0 * 100.0, 1).paddedLeft ('0', 5) + "% " + style.name);
        }

        slowest.sort (false);
        std::reverse (slowest.begin(), slowest.end());   // the most first
        std::cout << "    (all twenty need " << juce::String (totalSeconds / 2.0 * 100.0, 1)
                  << "% of one core at 48 kHz; the most: " << slowest.joinIntoString (", ").substring (0, 140) << "...)\n";
        check (std::isfinite (audio.getRMSLevel (0, 0, 96000)), "(the chain's output is finite)");
       #if defined (NDEBUG)
        check (totalSeconds < 2.0 * 0.5, "all twenty together need less than half of one core");
       #endif
    }

    std::cout << "Yellow Drive\n";
    {
        const auto drive = effects.addEffect (t, YellowDrivePlugin::xmlTypeName);
        auto valueOf = [&] (const juce::String& id)
        {
            const auto i = indexOfParameter (t, drive, id);
            return i >= 0 ? effects.getEffectParameters (t, drive)[(size_t) i].valueText : juce::String();
        };

        check (drive != 0 && effects.getEffectChain (t).front().name == "Yellow Drive", "it can be added, and is called Yellow Drive");
        check (valueOf ("drive") == "50%" && valueOf ("tone") == "400 Hz"
                 && valueOf ("level") == juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x92")) + "16.0 dB",
               "its knobs start where the original's do (Drive 50%, Tone 400 Hz, Level -16 dB)");

        checkUndoRoundTrip (engine, "Yellow Drive Drive", [&]
        {
            engine.beginUndoStep ("Yellow Drive Drive");
            effects.setEffectParameter (t, drive, indexOfParameter (t, drive, "drive"), 0.8f);
        });
        engine.beginUndoStep ("Yellow Drive Tone");
        effects.setEffectParameter (t, drive, indexOfParameter (t, drive, "tone"), 0.5f);   // 550 Hz (linear, as the original)

        checkSaveAndReopen (engine, dir.getChildFile ("Yellow Drive"));
        t = tracks.getTracks().back().id;
        const auto chain = effects.getEffectChain (t);
        check (chain.size() == 1 && chain[0].type == YellowDrivePlugin::xmlTypeName, "it survives save and reopen");

        if (chain.size() == 1)
        {
            const auto i = indexOfParameter (t, chain[0].id, "tone");
            check (i >= 0 && effects.getEffectParameters (t, chain[0].id)[(size_t) i].valueText == "550 Hz", "and so do its knobs");
        }
    }

    std::cout << "Every ported pedal survives saving\n";
    {
        // All the ported pedals, as many as fit on a track, each with its
        // first knob turned to 0.8.
        removeAll (t);
        juce::StringArray types;

        for (auto& style : getPedalStyles())
            if (style.pluginType.startsWith ("ikaros") && types.size() < EffectsController::maxEffectsPerTrack)
                types.add (style.pluginType);

        for (auto& type : types)
        {
            const auto id = effects.addEffect (t, type);
            engine.beginUndoStep ("Pedal Knob");
            effects.setEffectParameter (t, id, 0, 0.8f);
        }

        const auto before = typesOn (t);
        auto knobs = [&]
        {
            juce::StringArray texts;

            for (auto& slot : effects.getEffectChain (t))
                texts.add (effects.getEffectParameters (t, slot.id)[0].valueText);

            return texts.joinIntoString ("|");
        };

        const auto knobsBefore = knobs();
        checkSaveAndReopen (engine, dir.getChildFile ("Ported Pedals"));
        t = tracks.getTracks().back().id;
        check (types.size() == 12 && typesOn (t) == before, "twelve of them on a track come back, in order");
        check (knobs() == knobsBefore, "with their knobs");
    }

    device.reset();
}

//==============================================================================
// Reads a whole audio file (every channel), or an empty buffer if it can't.
static juce::AudioBuffer<float> readAudioFile (const juce::File& f, double* sampleRate = nullptr)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (f));

    if (reader == nullptr)
        return {};

    juce::AudioBuffer<float> samples ((int) reader->numChannels, (int) reader->lengthInSamples);
    reader->read (&samples, 0, samples.getNumSamples(), 0, true, true);

    if (sampleRate != nullptr)
        *sampleRate = reader->sampleRate;

    return samples;
}

static void runAmpSection (const juce::File& dir)
{
    AudioEngine engine { testSettings, false };
    auto device = std::make_unique<TestAudioDevice> (engine, TestAudioDevice::Settings {});
    auto& tracks = engine.getTracks();
    auto& effects = tracks.effects();
    juce::MessageManager::getInstance()->runDispatchLoopUntil (2500);
    runAudio (*device, 0.2);

    const auto& voices = getAmpVoices();
    const auto offCab = AmpPlugin::getOffCab();

    auto typesOn = [&] (TrackId track)
    {
        juce::StringArray types;

        for (auto& slot : effects.getEffectChain (track))
            types.add (slot.type);

        return types.joinIntoString (",");
    };

    // The settings match a voice's starting ones.
    auto hasVoiceDefaults = [&] (const EffectsController::AmpSettings& amp, int voice)
    {
        auto& v = voices[(size_t) voice];
        bool same = amp.exists && amp.voice == voice && amp.cab == v.cab && amp.bright == v.bright;

        for (int i = 0; i < ampKnobCount; ++i)
            same = same && std::abs (amp.knobs[(size_t) i] - v.knobDefaults[(size_t) i]) < 1.0e-4f;

        return same;
    };

    // The track's Amp itself (the Plugin::Ptr must be let go before the Edit goes).
    auto ampPluginOn = [&] (TrackId track) -> AmpPlugin*
    {
        auto* t = detail::findTrack (TestAudioDevice::getEditOf (engine), track);
        return t != nullptr ? t->pluginList.findFirstPluginOfType<AmpPlugin>() : nullptr;
    };

    std::cout << "Voices and cabs\n";
    {
        juce::StringArray names, cabNames;

        for (auto& v : voices)
            names.add (v.name);

        for (int i = 0; i <= offCab; ++i)
            cabNames.add (getAmpCabName (i));

        check (names.joinIntoString (",") == "Clean,Crunch,Chime,British,High Gain,Bass", "six voices, in order");
        check (cabNames.size() == 19 && cabNames[offCab] == "Off", "every Guitarix cab (18), then Off");

        // No brand or amp names in the cab list.
        bool plain = true;

        for (auto& name : cabNames)
            for (auto* brand : { "Twin", "Bassman", "Marshall", "AC30", "Mesa", "mesa", "Princeton", "Fender", "Vox", "Ampeg" })
                plain = plain && ! name.containsIgnoreCase (brand);

        check (plain, "the cabs have plain names");
        check (getAmpCabName (voices[0].cab) == juce::String::fromUTF8 ("2\xc3\x97" "12 Open Back")
                 && getAmpCabName (voices[5].cab) == juce::String::fromUTF8 ("1\xc3\x97" "15"),
               "Clean plays the 2x12 Open Back, Bass the 1x15");
    }

    std::cout << "New guitar and bass tracks\n";
    TrackId guitar = 0, bass = 0;
    {
        checkUndoRoundTrip (engine, "Add Guitar Track", [&] { guitar = tracks.addGuitarTrack(); });
        check (typesOn (guitar) == AmpPlugin::xmlTypeName, "a new guitar track has an Amp (and nothing else)");
        check (hasVoiceDefaults (effects.getAmp (guitar), 0) && effects.getAmp (guitar).on, "on the Clean voice, switched on");

        checkUndoRoundTrip (engine, "Add Bass Track", [&] { bass = tracks.addGuitarTrack (true); });
        const auto info = tracks.getTrack (bass);
        check (info.has_value() && info->name == "Bass 1" && info->icon == "bass" && effects.hasPedalboard (bass),
               "a bass track is called Bass 1, with the bass icon and a pedalboard");
        check (hasVoiceDefaults (effects.getAmp (bass), 5), "and its Amp is on the Bass voice");

        const auto vocal = tracks.addAudioTrack();
        check (! effects.getAmp (vocal).exists && ! effects.hasPedalboard (vocal), "a plain audio track gets no Amp");
        check (effects.addAmp (vocal) != 0 && effects.hasPedalboard (vocal) && effects.getAmp (vocal).voice == 0,
               "but one can be added (Clean), and then it has a pedalboard");
        tracks.deleteTrack (vocal);
        pumpMessages();
    }

    std::cout << "Where the Amp sits\n";
    {
        const auto reverb = effects.addEffect (guitar, "reverb");
        effects.addEffect (guitar, "chorus");
        effects.addEffect (guitar, YellowDrivePlugin::xmlTypeName);
        check (typesOn (guitar) == "chorus,ikarosYellowDrive,ikarosAmp,reverb", "after the pedals, before other effects");

        const auto ampId = effects.getAmp (guitar).pluginId;
        effects.moveEffect (guitar, ampId, 0);
        check (typesOn (guitar) == "chorus,ikarosYellowDrive,ikarosAmp,reverb", "it can't be moved in front of the pedals");
        effects.moveEffect (guitar, reverb, 0);
        check (typesOn (guitar) == "chorus,ikarosYellowDrive,ikarosAmp,reverb", "and other effects can't go in front of it");

        check (effects.addEffect (guitar, AmpPlugin::xmlTypeName) == 0 && effects.addAmp (guitar) == 0
                 && effects.replaceEffect (guitar, reverb, AmpPlugin::xmlTypeName) == 0,
               "a track can't have a second Amp");

        checkUndoRoundTrip (engine, "Remove Effect", [&] { effects.removeEffect (guitar, ampId); });
        check (! effects.getAmp (guitar).exists, "it can be removed");
        checkUndoRoundTrip (engine, "Add Amp", [&] { effects.addAmp (guitar); });
        check (typesOn (guitar) == "chorus,ikarosYellowDrive,ikarosAmp,reverb", "and added back after the pedals");

        for (auto& slot : effects.getEffectChain (guitar))
            if (slot.type != AmpPlugin::xmlTypeName)
                effects.removeEffect (guitar, slot.id);
    }

    std::cout << "Each voice sets the model, tone stack, cab and knobs\n";
    {
        const std::array<const char*, 6> expectedDsp {
            "gxamp16 + tonestack_fender_default", "gxamp7 + tonestack_bassman", "gxamp + tonestack_ac30",
            "gxamp5 + tonestack_jcm800", "gxamp9 + tonestack_engl", "gxamp8 + tonestack_ampeg" };

        for (int voice = 0; voice < (int) voices.size(); ++voice)
        {
            // Off the voice's defaults first, so the change touches everything.
            engine.beginUndoStep ("Amp Gain");
            effects.setAmpKnob (guitar, 0, 0.93f);
            effects.setAmpCab (guitar, offCab);
            effects.setAmpVoice (guitar, (voice + 1) % (int) voices.size());
            effects.setAmpBright (guitar, ! voices[(size_t) voice].bright);
            pumpMessages();

            const auto name = voices[(size_t) voice].name;
            checkUndoRoundTrip (engine, "Amp Voice", [&] { effects.setAmpVoice (guitar, voice); });
            check (hasVoiceDefaults (effects.getAmp (guitar), voice), (name + ": its cab, Bright and knobs, in one undo step").toRawUTF8());

            auto* amp = ampPluginOn (guitar);
            check (amp != nullptr && AmpPlugin::getVoiceDspNames (amp->getVoice()) == expectedDsp[(size_t) voice],
                   (name + ": " + expectedDsp[(size_t) voice]).toRawUTF8());
        }

        // The Plug-ins inspector's Voice knob does the same.
        auto* amp = ampPluginOn (guitar);
        const auto ampId = effects.getAmp (guitar).pluginId;
        auto params = effects.getEffectParameters (guitar, ampId);
        const auto voiceIndex = (int) std::distance (params.begin(), std::find_if (params.begin(), params.end(),
                                                     [] (const ParameterInfo& p) { return p.id == "voice"; }));
        engine.beginUndoStep ("Amp Voice");
        effects.setEffectParameter (guitar, ampId, voiceIndex, 1.0f / 5.0f);   // Crunch
        check (amp != nullptr && hasVoiceDefaults (effects.getAmp (guitar), 1), "the Plug-ins inspector's Voice knob sets the rest too");
    }

    std::cout << "The cab on its own\n";
    {
        engine.beginUndoStep ("Amp Treble");
        effects.setAmpKnob (guitar, 3, 0.81f);
        pumpMessages();
        const auto before = effects.getAmp (guitar);
        checkUndoRoundTrip (engine, "Amp Cab", [&] { effects.setAmpCab (guitar, 15); });
        const auto after = effects.getAmp (guitar);
        check (after.cab == 15 && after.voice == before.voice && after.bright == before.bright && after.knobs == before.knobs,
               "changing the cab changes nothing else");
        checkUndoRoundTrip (engine, "Amp Bright On", [&] { effects.setAmpBright (guitar, true); });   // (Crunch starts with it off)
        checkUndoRoundTrip (engine, "Amp Master", [&]
        {
            engine.beginUndoStep ("Amp Master");
            effects.setAmpKnob (guitar, 5, 0.3f);
        });
        checkUndoRoundTrip (engine, "Turn Effect Off", [&] { effects.setEffectEnabled (guitar, after.pluginId, false); });
        effects.setEffectEnabled (guitar, after.pluginId, true);
    }

    std::cout << "Saving and reopening\n";
    {
        // One track per voice (the guitar track is Crunch, the bass track Bass).
        for (int voice : { 0, 2, 3, 4 })
            effects.setAmpVoice (tracks.addGuitarTrack(), voice);

        effects.setAmpVoice (bass, 5);
        pumpMessages();

        // In track order, as they'll be read back.
        std::vector<EffectsController::AmpSettings> saved;
        juce::Array<int> savedVoices;

        for (auto& info : tracks.getTracks())
        {
            if (effects.getAmp (info.id).exists)
            {
                saved.push_back (effects.getAmp (info.id));
                savedVoices.addIfNotAlreadyThere (saved.back().voice);
            }
        }

        checkSaveAndReopen (engine, dir.getChildFile ("Amps"));

        // The tracks come back in the same order.
        auto list = tracks.getTracks();
        std::vector<EffectsController::AmpSettings> reopened;

        for (auto& info : list)
            if (effects.getAmp (info.id).exists)
                reopened.push_back (effects.getAmp (info.id));

        bool same = reopened.size() == saved.size();

        for (size_t i = 0; same && i < saved.size(); ++i)
        {
            // (The guitar track's are its own, not a voice's defaults: compare as saved.)
            auto& a = saved[i];
            auto& b = reopened[i];
            same = a.voice == b.voice && a.cab == b.cab && a.bright == b.bright && a.on == b.on && a.knobs == b.knobs;
        }

        check (same && saved.size() == 6 && savedVoices.size() == 6, "every voice, and each Amp's cab, Bright, knobs and on/off, survive save and reopen");
    }

    std::cout << "Sound\n";
    {
        const auto di = readAudioFile (juce::File (IKAROS_TEST_FIXTURES).getChildFile ("pedals/di_guitar.wav"));
        check (di.getNumSamples() == 96000, "(the DI guitar is two seconds long)");
        auto* edit = TestAudioDevice::getEditOf (engine);
        constexpr int blockSize = 512;

        // Plays the DI guitar into both channels, in blocks the way Tracktion does.
        auto render = [&] (tracktion::Plugin& plugin, double sampleRate, const std::function<void (int block)>& beforeBlock = {})
        {
            plugin.initialise ({ tracktion::TimePosition(), sampleRate, blockSize });
            plugin.reset();
            juce::AudioBuffer<float> buffer (2, di.getNumSamples());

            for (int ch = 0; ch < 2; ++ch)
                buffer.copyFrom (ch, 0, di, 0, 0, di.getNumSamples());

            for (int start = 0, block = 0; start < buffer.getNumSamples(); start += blockSize, ++block)
            {
                if (beforeBlock)
                    beforeBlock (block);

                tracktion::PluginRenderContext context (&buffer, start, std::min (blockSize, buffer.getNumSamples() - start),
                                                        nullptr, 0.0, {}, true, false, true, false);
                plugin.applyToBuffer (context);
            }

            return buffer;
        };

        auto allFinite = [] (const juce::AudioBuffer<float>& b)
        {
            for (int ch = 0; ch < b.getNumChannels(); ++ch)
                for (int i = 0; i < b.getNumSamples(); ++i)
                    if (! std::isfinite (b.getSample (ch, i)))
                        return false;

            return true;
        };

        auto dB = [] (float gain)    { return juce::Decibels::gainToDecibels (gain, -200.0f); };

        bool finite = true, sensible = true, sameOnBoth = true, latencyOk = true;

        for (int voice = 0; voice < (int) voices.size(); ++voice)
        {
            auto plugin = edit->getPluginCache().createNewPlugin (AmpPlugin::createState (voice));

            for (double rate : { 44100.0, 48000.0, 96000.0 })
            {
                const auto out = render (*plugin, rate);
                const auto rms = dB (out.getRMSLevel (0, 0, out.getNumSamples()));
                const auto peak = dB (out.getMagnitude (0, 0, out.getNumSamples()));
                const auto latency = plugin->getLatencySeconds();
                std::cout << "    (" << voices[(size_t) voice].name << " at " << rate / 1000.0 << " kHz: "
                          << juce::String (rms, 1) << " dBFS RMS, peak " << juce::String (peak, 1) << " dBFS, latency "
                          << juce::String (latency * 1000.0, 2) << " ms)\n";

                finite = finite && allFinite (out);
                sensible = sensible && rms > -30.0f && rms < -12.0f && peak < -0.5f;
                latencyOk = latencyOk && std::isfinite (latency) && latency >= 0.0 && latency < 0.01;

                for (int i = 0; i < out.getNumSamples(); ++i)
                    sameOnBoth = sameOnBoth && out.getSample (0, i) == out.getSample (1, i);
            }
        }

        check (finite, "every voice's output is finite (no NaN or inf) at 44.1, 48 and 96 kHz");
        check (sensible, "at a sensible level (-30 to -12 dBFS RMS, peaks under full scale)");
        check (sameOnBoth, "the same sound on both channels");
        check (latencyOk, "and its latency is reported (none: zero-latency convolution)");

        {
            // Switching voice and cab every few blocks, as automation might.
            auto plugin = edit->getPluginCache().createNewPlugin (AmpPlugin::createState (0));
            auto* amp = dynamic_cast<AmpPlugin*> (plugin.get());
            const auto out = render (*plugin, 48000.0, [&] (int block)
            {
                if (block % 8 == 0)
                {
                    amp->getAutomatableParameterByID ("voice")->setParameter ((float) ((block / 8) % 6), juce::sendNotificationSync);
                    amp->getAutomatableParameterByID ("cab")->setParameter ((float) ((block / 8) % (offCab + 1)), juce::sendNotificationSync);
                }
            });
            check (allFinite (out) && out.getMagnitude (0, 0, out.getNumSamples()) < 1.0f,
                   "switching voice and cab while playing stays finite and under full scale");
        }

        {
            // Cab Off is the amp without a cabinet: convolving its output with
            // a cab's IR by hand gives what the Amp plays with that cab.
            auto plugin = edit->getPluginCache().createNewPlugin (AmpPlugin::createState (1));
            auto cabParam = plugin->getAutomatableParameterByID ("cab");
            const int cab = 9;   // 2x12 Open Back (a 48 kHz IR, like the test's rate)
            cabParam->setParameter ((float) offCab, juce::sendNotificationSync);
            const auto dry = render (*plugin, 48000.0);
            cabParam->setParameter ((float) cab, juce::sendNotificationSync);
            const auto withCab = render (*plugin, 48000.0);

            double irRate = 0.0;
            const auto ir = AmpPlugin::getCabinetIR (cab, irRate);
            double error = 0.0, reference = 0.0;

            for (int i = 0; i < dry.getNumSamples(); ++i)
            {
                double y = 0.0;

                for (int k = 0; k < ir.getNumSamples() && k <= i; ++k)
                    y += (double) ir.getSample (0, k) * dry.getSample (0, i - k);

                error += (y - withCab.getSample (0, i)) * (y - withCab.getSample (0, i));
                reference += y * y;
            }

            const auto belowDb = 10.0 * std::log10 (reference / std::max (error, 1.0e-30));
            std::cout << "    (cab by hand vs the Amp's cab: the difference is " << juce::String (belowDb, 1) << " dB down)\n";
            check (irRate == 48000.0 && belowDb > 80.0, "cab Off passes the amp's output through untouched");
        }

        {
            // How much of one core the Amp needs at 48 kHz: each voice alone,
            // then behind a full pedalboard (11 pedals, the most that fit
            // with it). A Debug build is much slower than the app.
            auto timeIt = [&] (std::vector<tracktion::Plugin::Ptr>& chain)
            {
                for (auto& p : chain)
                {
                    p->initialise ({ tracktion::TimePosition(), 48000.0, blockSize });
                    p->reset();
                }

                juce::AudioBuffer<float> buffer (2, di.getNumSamples());

                for (int ch = 0; ch < 2; ++ch)
                    buffer.copyFrom (ch, 0, di, 0, 0, di.getNumSamples());

                const auto start = juce::Time::getHighResolutionTicks();

                for (int at = 0; at < buffer.getNumSamples(); at += blockSize)
                {
                    tracktion::PluginRenderContext context (&buffer, at, std::min (blockSize, buffer.getNumSamples() - at),
                                                            nullptr, 0.0, {}, true, false, true, false);

                    for (auto& p : chain)
                        p->applyToBuffer (context);
                }

                const auto seconds = juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - start);
                return seconds / (buffer.getNumSamples() / 48000.0) * 100.0;   // % of one core
            };

            juce::StringArray perVoice;
            double worst = 0.0;

            for (int voice = 0; voice < (int) voices.size(); ++voice)
            {
                std::vector<tracktion::Plugin::Ptr> chain { edit->getPluginCache().createNewPlugin (AmpPlugin::createState (voice)) };
                const auto percent = timeIt (chain);
                worst = std::max (worst, percent);
                perVoice.add (voices[(size_t) voice].name + " " + juce::String (percent, 1) + "%");
            }

            std::vector<tracktion::Plugin::Ptr> board;

            for (auto& style : getPedalStyles())
                if ((int) board.size() < EffectsController::maxEffectsPerTrack - 1)
                    board.push_back (edit->getPluginCache().createNewPlugin (style.pluginType, {}));

            board.push_back (edit->getPluginCache().createNewPlugin (AmpPlugin::createState (4)));
            const auto boardPercent = timeIt (board);

            std::cout << "    (CPU, one core at 48 kHz, "
                     #if defined (NDEBUG)
                      << "Release"
                     #else
                      << "Debug"
                     #endif
                      << " build: " << perVoice.joinIntoString (", ") << "; 11 pedals + the Amp "
                      << juce::String (boardPercent, 1) << "%)\n";
           #if defined (NDEBUG)
            check (worst < 10.0, "one Amp needs less than a tenth of one core");
           #else
            juce::ignoreUnused (worst);
           #endif
        }
    }

    std::cout << "Sessions from before the Amp\n";
    {
        // Saved and exported by the app before this milestone (see
        // tests/fixtures/sessions/README).
        const auto fixtures = juce::File (IKAROS_TEST_FIXTURES).getChildFile ("sessions");
        const auto session = dir.getChildFile ("Before Amp.iksession");
        fixtures.getChildFile ("Before Amp.iksession").copyFileTo (session);
        check (engine.openSession (session), "(open)");
        pumpMessages();

        auto list = tracks.getTracks();
        bool noAmp = ! list.empty(), pedalboards = ! list.empty();
        juce::StringArray chains;

        for (auto& info : list)
        {
            noAmp = noAmp && ! effects.getAmp (info.id).exists;

            // (It also has the instrument track every new session starts with.)
            if (info.kind == TrackKind::audio)
            {
                pedalboards = pedalboards && effects.hasPedalboard (info.id);
                chains.add (typesOn (info.id));
            }
        }

        check (noAmp, "it opens with no Amp added");
        check (pedalboards && chains.joinIntoString ("|") == "ikarosYellowDrive,ikarosStackDrive,delay|ikarosGoldenBoost",
               "its guitar and bass tracks keep their pedals as they were");

        ExportSettings settings;
        settings.quality = 24;
        settings.destination = dir.getChildFile ("Before Amp, now.wav");
        bool done = false;
        check (! engine.getExporter().start (settings, [&] (const ExportResult&) { done = true; }).failed(), "(export)");

        for (int i = 0; i < 1200 && ! done; ++i)
            juce::MessageManager::getInstance()->runDispatchLoopUntil (50);

        const auto now = readAudioFile (settings.destination);
        const auto before = readAudioFile (fixtures.getChildFile ("Before Amp.wav"));
        bool identical = now.getNumChannels() == before.getNumChannels() && now.getNumSamples() == before.getNumSamples()
                           && now.getNumSamples() > 0;

        for (int ch = 0; identical && ch < now.getNumChannels(); ++ch)
            identical = std::memcmp (now.getReadPointer (ch), before.getReadPointer (ch), sizeof (float) * (size_t) now.getNumSamples()) == 0;

        check (identical, "and it renders bit-identical to the render made before the Amp");
    }

    device.reset();
}

int main (int argc, char* argv[])
{
    // Tracktion needs a running MessageManager; this sets one up for a console app.
    juce::ScopedJuceInitialiser_GUI juceInit;

    juce::StringArray args;

    for (int i = 1; i < argc; ++i)
        args.add (argv[i]);

    // The plug-in scan runs plug-ins in a copy of the program that started
    // it, so this test binary is also the scan worker.
    if (AudioEngine::runPluginScanWorkerIfRequested (args.joinIntoString (" ")))
    {
        juce::MessageManager::getInstance()->runDispatchLoop();
        return 0;
    }

    // The recovery test's crashing child (it shares the test's folders, set below).
    for (auto& arg : args)
        if (arg.startsWith ("--recovery-child="))
            return runRecoveryChild (juce::File (arg.fromFirstOccurrenceOf ("=", false, false)));

    // Start from fresh settings, so earlier runs (e.g. remembered plug-in
    // scans) don't change the results. Tracktion keeps them in the user's
    // app-data folder, under the name the engine is given.
    juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile (testSettings).deleteRecursively();

    // Sections: "--only=export" runs just that one, "--skip=export" all the
    // others (ctest runs them as separate tests, see CMakeLists.txt). The
    // "main" section is everything that isn't in a section of its own.
    juce::String only, skip;

    for (int i = args.size(); --i >= 0;)
    {
        if (args[i].startsWith ("--only="))       only = args[i].fromFirstOccurrenceOf ("=", false, false);
        else if (args[i].startsWith ("--skip="))  skip = args[i].fromFirstOccurrenceOf ("=", false, false);
        else continue;

        args.remove (i);
    }

    // "--skip=export,undo" skips several.
    const auto skipped = juce::StringArray::fromTokens (skip, ",", {});

    auto runs = [&] (const juce::String& section)
    {
        return (only.isEmpty() || only == section) && ! skipped.contains (section);
    };

    // The test plug-ins (built with -DIKAROS_BUILD_TEST_PLUGINS=ON), if any.
    const auto testPlugins = args.isEmpty() ? juce::File() : juce::File (args[0]);

    auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("IkarosSmokeTest");
    dir.deleteRecursively();
    dir.createDirectory();

    // Never read or change the user's own sound packs: the library's user
    // Sounds folder is one of the test's (see SoundLibrary::getUserSoundsFolder).
    const auto userSounds = dir.getChildFile ("User Sounds").getFullPathName();
    // Nor the app's open sessions: on Linux, working copies (and crash
    // recovery) go in a local data folder of the test's (see
    // platform::getLocalDataFolder).
   #if JUCE_WINDOWS
    _putenv_s ("IKAROS_USER_SOUNDS_DIR", userSounds.toRawUTF8());
   #else
    const auto localData = dir.getChildFile ("Local Data").getFullPathName();
    setenv ("IKAROS_USER_SOUNDS_DIR", userSounds.toRawUTF8(), 1);
    setenv ("XDG_DATA_HOME", localData.toRawUTF8(), 1);
   #endif
    auto file = AudioEngine::getSessionFileFor (dir.getChildFile ("Smoke"));   // Smoke.iksession

    if (runs ("main"))
    {
        std::cout << "Starting in stages\n";
        {
            AudioEngine staged { testSettings, false, false };
            check (staged.getNextStartupStage().has_value(), "an engine started later has stages waiting");

            auto last = -1.0f;
            auto rising = true, described = true;
            auto count = 0;

            while (auto stage = staged.getNextStartupStage())
            {
                rising = rising && stage->fractionDone > last && stage->fractionDone < 1.0f;
                described = described && stage->description.isNotEmpty();
                last = stage->fractionDone;
                staged.runNextStartupStage();
                ++count;
            }

            check (count > 1 && rising && described, "each stage says what it loads, and the progress rises");
            check (staged.getSessionName().isNotEmpty(), "a session is open after the last stage");
        }
        {
            AudioEngine neverStarted { testSettings, false, false };   // quit before the first stage
        }
        {
            AudioEngine partlyStarted { testSettings, false, false };  // quit in the middle
            partlyStarted.runNextStartupStage();
            partlyStarted.runNextStartupStage();
        }
        check (true, "closing an engine that didn't finish starting is safe");

        AudioEngine engine { testSettings };
        auto& tracks = engine.getTracks();

        // Let startup settle: the engine clears setup noise from the undo
        // history once the session has been quiet for a moment.
        juce::MessageManager::getInstance()->runDispatchLoopUntil (2500);

        std::cout << "New session\n";
        auto list = tracks.getTracks();
        check (list.size() == 1, "starts with exactly one track");
        check (engine.isUntitled(), "is untitled");
        check (! engine.hasUnsavedChanges(), "has no unsaved changes");
        check (! engine.canUndo(), "has no undo history");

        auto t1 = list.front().id;
        check (tracks.getDetails (t1).pluginName.contains ("4OSC"), "first track uses the 4OSC synth");

        pumpMessages();
        std::cout << "Tracks and regions\n";
        auto t2 = tracks.addInstrumentTrack();
        check (tracks.getTracks().size() == 2, "+ New Track adds a track");

        auto r1 = tracks.regions().createChordRegion (t2, 5.0);   // inside bar 2 → snaps to beat 4
        auto region = tracks.regions().getRegion (r1);
        check (region.has_value(), "double-click creates a region");
        check (region && region->startBeat == 4.0 && region->lengthBeats == 16.0, "region is 4 bars, snapped to the bar");
        check (region && region->notes.size() == 4 * 7, "region has a chord pattern (7 notes per bar)");
        check (tracks.regions().createChordRegion (t2, 6.0) == 0, "can't stack a region on top of another");

        pumpMessages();
        std::cout << "Mixer\n";
        tracks.setMute (t2, true);
        tracks.setSolo (t1, true);
        engine.beginUndoStep ("Volume");
        tracks.setVolume (t1, 0.5f);
        auto a = tracks.getTrack (t1), b = tracks.getTrack (t2);
        check (b && b->muted, "mute");
        check (a && a->solo, "solo");
        check (b && b->silencedBySolo, "other tracks are silenced by solo");
        check (a && std::abs (a->volume - 0.5f) < 0.01f, "volume");

        pumpMessages();
        std::cout << "Undo\n";
        check (engine.getUndoDescription() == "Volume", "undo step is named");
        engine.undo();   // volume
        pumpMessages();
        check (std::abs (tracks.getTrack (t1)->volume - 0.5f) > 0.01f, "undo restores the volume");
        engine.redo();
        pumpMessages();
        check (std::abs (tracks.getTrack (t1)->volume - 0.5f) < 0.01f, "redo re-applies it");

        pumpMessages();
        std::cout << "Change notifications\n";
        {
            // Collects what the engine reports, like MainComponent does.
            struct Recorder  : public AudioEngine::Listener
            {
                ModelChanges heard;
                int calls = 0;
                void sessionChanged (const ModelChanges& c) override   { heard.merge (c); ++calls; }
            };

            Recorder recorder;
            engine.addListener (&recorder);

            auto hear = [&] (auto&& doEdit)
            {
                recorder.heard = {};
                recorder.calls = 0;
                doEdit();
                pumpMessages();
                return recorder.heard;
            };

            auto knob = hear ([&] { engine.beginUndoStep ("Pan"); tracks.setPan (t2, 0.3f); });
            check (knob.mixer && ! knob.regions && ! knob.notes && knob.changedRegions.empty() && ! knob.session,
                   "a pan change is reported as a mixer change only");

            auto note = hear ([&] { engine.beginUndoStep ("Add Note"); tracks.notes().addNote (r1, { 72, 0.0, 1.0, 100 }); });
            check (note.notes && note.changedRegions == std::set<RegionId> { r1 } && ! note.regions && ! note.mixer,
                   "adding a note reports that region's notes only");

            auto rename = hear ([&] { tracks.regions().renameRegion (r1, "Verse"); });
            check (rename.changedRegions.count (r1) == 1 && ! rename.regions && ! rename.notes,
                   "renaming a region reports just that region");

            RegionId added = 0;
            auto newRegion = hear ([&] { added = tracks.regions().createChordRegion (t2, 40.0); });
            check (added != 0 && newRegion.regions, "a new region is reported as a region-list change");

            auto newTrack = hear ([&] { tracks.addInstrumentTrack(); });
            check (newTrack.tracks, "a new track is reported as a track change");

            auto playhead = hear ([&] { engine.setPositionBeats (8.0); });
            check (recorder.calls == 0, "moving the playhead isn't reported (the UI polls it)");

            auto undone = hear ([&] { engine.undo(); });   // the new track
            check (undone.tracks, "undo reports what it changed");

            engine.undo();   // the new region
            engine.undo();   // the rename
            engine.undo();   // the note
            engine.undo();   // the pan
            pumpMessages();
            check (tracks.getTrack (t2)->pan == 0.0f && tracks.regions().getRegion (r1)->notes.size() == 4 * 7,
                   "undoing the test edits restores the session");

            juce::ignoreUnused (playhead);
            engine.removeListener (&recorder);
        }

        pumpMessages();
        std::cout << "Transport\n";
        engine.setBpm (96.0);
        engine.setLooping (true);
        auto ts = engine.getTransportState();
        check (std::abs (ts.bpm - 96.0) < 0.001, "BPM change reaches the tempo sequence");
        check (ts.looping && ts.loopEndBeats > ts.loopStartBeats, "loop turns on with a range");

        pumpMessages();
        std::cout << "Sound\n";
        check (tracks.effects().setSound (t2, "sampler:ikaros-studio.basics/Soft Piano"), "a sampled sound loads");
        check (tracks.getTrack (t2)->soundName == "Soft Piano", "the sound is assigned to the track");

        std::cout << "Plug-ins\n";
        check (tracks.effects().getInstrument (t2).has_value(), "track has an instrument");
        check (tracks.effects().getEffectChain (t2).empty(), "new tracks start with no effects");
        auto reverb = tracks.effects().addEffect (t2, "reverb");
        auto comp = tracks.effects().addEffect (t2, "compressor");
        auto eq = tracks.effects().addEffect (t2, "4bandEq", 0);   // insert at the top
        auto chain = tracks.effects().getEffectChain (t2);
        check (reverb != 0 && comp != 0 && eq != 0, "effects can be added");
        check (chain.size() == 3 && chain[0].id == eq && chain[1].id == reverb && chain[2].id == comp, "effects go where asked");
        tracks.effects().moveEffect (t2, comp, 0);
        chain = tracks.effects().getEffectChain (t2);
        check (chain.size() == 3 && chain[0].id == comp && chain[1].id == eq, "effects can be reordered");
        tracks.effects().setEffectEnabled (t2, comp, false);
        check (! tracks.effects().getEffectChain (t2)[0].enabled, "effects can be bypassed");
        auto echo = tracks.effects().replaceEffect (t2, eq, "delay");
        chain = tracks.effects().getEffectChain (t2);
        check (echo != 0 && chain.size() == 3 && chain[1].id == echo && chain[1].name == "Echo", "effects can be replaced in place");
        tracks.effects().removeEffect (t2, reverb);
        check (tracks.effects().getEffectChain (t2).size() == 2, "effects can be removed");
        auto params = tracks.effects().getEffectParameters (t2, echo);
        check (! params.empty(), "effects expose parameters");
        engine.beginUndoStep ("Effect setting");
        tracks.effects().setEffectParameter (t2, echo, 0, 0.25f);
        check (std::abs (tracks.effects().getEffectParameters (t2, echo)[0].value - 0.25f) < 0.02f, "effect parameters can be set");
        pumpMessages();
        engine.undo();   // the effect setting
        pumpMessages();
        engine.undo();   // removing the reverb
        pumpMessages();
        check (tracks.effects().getEffectChain (t2).size() == 3, "undo brings a removed effect back");

        pumpMessages();
        std::cout << "Track operations\n";
        {
            const auto trackCount = tracks.getTracks().size();
            const auto sourceRegions = tracks.regions().getRegions (t2);
            const int sourceIndex = tracks.getTrack (t2)->index;

            auto settingsCopy = tracks.duplicateTrack (t2, false);
            pumpMessages();
            auto copy = tracks.getTrack (settingsCopy);
            check (copy && tracks.getTracks().size() == trackCount + 1 && copy->index == sourceIndex + 1,
                   "duplicate settings adds a track just below");
            check (copy && copy->soundName == "Soft Piano" && copy->category == tracks.getTrack (t2)->category,
                   "with the same sound and category (so the same colour)");
            check (tracks.effects().getEffectChain (settingsCopy).size() == tracks.effects().getEffectChain (t2).size(), "and the same effects");
            check (tracks.regions().getRegions (settingsCopy).empty(), "but no regions");

            auto fullCopy = tracks.duplicateTrack (t2, true);
            pumpMessages();
            auto copiedRegions = tracks.regions().getRegions (fullCopy);
            check (! sourceRegions.empty() && copiedRegions.size() == sourceRegions.size()
                     && copiedRegions.front().id != sourceRegions.front().id, "duplicate track copies its regions");
            check (tracks.regions().getRegions (t2).size() == sourceRegions.size(), "and leaves the original's alone");

            const auto sourceName = tracks.getTrack (t2)->name;
            check (tracks.getTrack (settingsCopy)->name == sourceName + " 2" && tracks.getTrack (fullCopy)->name == sourceName + " 3",
                   "duplicates are numbered (Soft Piano 2, Soft Piano 3)");

            // A new track given a sound that another track is named after.
            auto sameSound = tracks.addInstrumentTrack();
            tracks.effects().setSound (sameSound, tracks.effects().getSoundId (t2), false);
            pumpMessages();
            check (tracks.getTrack (sameSound)->name == sourceName + " 4", "so is a new track with the same sound");
            tracks.effects().setSound (sameSound, "synth:Warm Pad");
            pumpMessages();
            check (tracks.getTrack (sameSound)->name == "Warm Pad", "and it's renamed when its sound changes");
            tracks.deleteTrack (sameSound);
            pumpMessages();

            tracks.renameTrack (fullCopy, "  Lead  ");
            pumpMessages();
            check (tracks.getTrack (fullCopy)->name == "Lead", "tracks can be renamed");
            tracks.renameTrack (fullCopy, "   ");
            check (tracks.getTrack (fullCopy)->name == "Lead", "a blank name is ignored");

            check (tracks.getTrack (fullCopy)->icon.isEmpty(), "track icons start automatic");
            tracks.setTrackIcon (fullCopy, "guitar");
            pumpMessages();
            check (tracks.getTrack (fullCopy)->icon == "guitar", "a track icon can be chosen");
            auto iconCopy = tracks.duplicateTrack (fullCopy, false);
            pumpMessages();
            check (tracks.getTrack (iconCopy)->icon == "guitar", "duplicates keep the icon");
            tracks.deleteTrack (iconCopy);
            pumpMessages();

            // Order is now t2, fullCopy, settingsCopy.
            tracks.moveTrack (fullCopy, 1);
            pumpMessages();
            check (tracks.getTrack (fullCopy)->index == sourceIndex + 2
                     && tracks.getTrack (settingsCopy)->index == sourceIndex + 1, "tracks move down");
            tracks.moveTrack (fullCopy, -1);
            pumpMessages();
            check (tracks.getTrack (fullCopy)->index == sourceIndex + 1, "and up");
            check (! tracks.canMoveTrack (tracks.getTracks().front().id, -1)
                     && ! tracks.canMoveTrack (tracks.getTracks().back().id, 1), "but not past either end");

            engine.undo();   // move up
            pumpMessages();
            check (tracks.getTrack (fullCopy)->index == sourceIndex + 2, "undo restores the order");
            engine.redo();
            pumpMessages();

            tracks.deleteTrack (fullCopy);
            pumpMessages();
            tracks.deleteTrack (settingsCopy);
            pumpMessages();
            check (tracks.getTracks().size() == trackCount, "(cleaned up the copies)");
        }

        pumpMessages();
        std::cout << "Recording\n";
        {
            auto t3 = tracks.addInstrumentTrack();
            tracks.setSelectedTrack (t3);
            check (tracks.inputs().getArmedTrack() == t3, "selecting a track arms it");
            tracks.setSelectedTrack (t2);
            check (tracks.inputs().getArmedTrack() == t2 && ! tracks.getTrack (t3)->armed, "selecting another moves the arm");
            tracks.setSelectedTrack (t3);
            tracks.inputs().setArmed (t3, false);
            tracks.setSelectedTrack (t3);
            check (tracks.inputs().getArmedTrack() == 0, "disarming the selected track sticks");
            engine.goToStart();
            pumpMessages();

            engine.toggleRecord();   // nothing armed: arms the selected track for this take
            pumpMessages();
            check (engine.getTransportState().recording, "Record starts recording");
            check (tracks.inputs().getArmedTrack() == t3, "the selected track is armed for the take");
            {
                const auto lastStep = engine.getUndoDescription();
                const auto numTracks = tracks.getTracks().size();
                check (! engine.canUndo() && ! engine.canRedo(), "Undo and Redo are unavailable while recording");
                engine.undo();
                pumpMessages();
                check (engine.getUndoDescription() == lastStep && tracks.getTracks().size() == numTracks,
                       "(and do nothing if asked anyway)");
            }
            tracks.setSelectedTrack (t2);
            check (tracks.inputs().getArmedTrack() == t3, "selecting another track mid-take doesn't move the arm");
            tracks.setSelectedTrack (t3);

            for (int note : { 60, 64, 67 })
            {
                tracks.inputs().noteOn (note, 0.8f);
                juce::MessageManager::getInstance()->runDispatchLoopUntil (250);
                tracks.inputs().noteOff (note);
                juce::MessageManager::getInstance()->runDispatchLoopUntil (100);
            }

            engine.toggleRecord();   // stop
            pumpMessages();
            pumpMessages();
            check (! engine.getTransportState().recording, "Record again stops");
            check (tracks.inputs().getArmedTrack() == 0, "the take's automatic arm is released");

            auto regions = tracks.regions().getRegions (t3);
            check (regions.size() == 1, "the take becomes a region");
            check (! regions.empty() && regions[0].notes.size() == 3, "the region holds the played notes");
            check (engine.canUndo() && engine.getUndoDescription() == "Record", "after the take, Undo is back (to undo the take)");

            tracks.setSelectedTrack (t2);
            tracks.setSelectedTrack (t3);   // armed by selecting it this time
            engine.toggleRecord();
            pumpMessages();
            engine.toggleRecord();
            pumpMessages();
            check (tracks.inputs().getArmedTrack() == t3, "a track armed by selecting it stays armed after the take");

            tracks.inputs().setArmed (t3, true);
            check (tracks.getTrack (t3)->armed, "a track can be armed explicitly");
            tracks.inputs().setArmed (t3, false);
            tracks.deleteTrack (t3);
            tracks.setSelectedTrack (t2);
        }

        std::cout << "Audio tracks\n";
        {
            auto inputs = tracks.inputs().getAudioInputs();
            std::cout << "  (audio inputs: " << inputs.size() << ")\n";
            auto a1 = tracks.addAudioTrack();
            auto info = tracks.getTrack (a1);
            check (info && info->kind == TrackKind::audio, "audio tracks can be added");
            check (tracks.effects().getSoundControls (a1).empty(), "audio tracks have no synth controls");
            check (! tracks.effects().getInstrument (a1).has_value(), "audio tracks have no instrument");

            if (! inputs.empty())
            {
                check (tracks.inputs().getRecordingSettings (a1).inputId == inputs.front().id, "new audio tracks use the first input");
                tracks.inputs().setTrackInput (a1, inputs.back().id);
                check (tracks.inputs().getRecordingSettings (a1).inputId == inputs.back().id, "the input can be changed");
            }

            check (! tracks.inputs().getRecordingSettings (a1).monitoring, "monitoring starts off (no feedback)");
            tracks.inputs().setMonitoring (a1, true);
            check (tracks.inputs().getRecordingSettings (a1).monitoring, "monitoring can be turned on");

            tracks.inputs().setArmed (a1, true);
            check (tracks.getTrack (a1)->armed, "audio tracks can be armed");
            tracks.inputs().setArmed (a1, false);
            tracks.deleteTrack (a1);
            tracks.setSelectedTrack (t2);
        }

        std::cout << "Save\n";
        check (engine.hasUnsavedChanges(), "edits mark the session as changed");
        check (engine.saveSessionAs (file), "Save As writes the file");
        check (file.existsAsFile(), "file exists on disk");
        check (! engine.hasUnsavedChanges(), "saved state is clean");
        check (engine.getSessionName() == "Smoke", "session takes the file name");
        {
            auto recent = engine.getRecentSessions();
            check (! recent.empty() && recent.front().file == file && recent.front().name == "Smoke",
                   "a saved session goes to the top of the recent list");
            check (file == dir.getChildFile ("Smoke.iksession") && engine.getSessionFile() == file,
                   "a session is saved as one .iksession file");

            const auto mime = SessionBundle::readEntry (file, "mimetype").toString();
            const auto info = juce::JSON::parse (SessionBundle::readEntry (file, SessionBundle::infoEntry).toString());
            check (mime == SessionBundle::mimeType && info["appVersion"].toString() == IKAROS_VERSION
                     && SessionBundle::readEntry (file, SessionBundle::sessionEntry).getSize() > 0,
                   "the bundle holds its mimetype, bundle.json and the session");

            // File managers recognise the type from the first bytes, as with
            // ODF files: a ZIP header (30 bytes), then "mimetype" and its
            // uncompressed content. (See packaging/linux/*.xml.)
            juce::MemoryBlock head;
            juce::FileInputStream (file).readIntoMemoryBlock (head, 80);
            check (head.toString().substring (0, 2) == "PK"
                     && juce::String::fromUTF8 ((const char*) head.getData() + 30, 36) == "mimetypeapplication/x-ikaros-session",
                   "and its type can be recognised from its first bytes");

            // The preview the UI writes before saving goes into the bundle too.
            juce::Image picture (juce::Image::RGB, 16, 10, true);
            juce::FileOutputStream pictureOut (engine.getPreviewFile());
            juce::PNGImageFormat().writeImageToStream (picture, pictureOut);
            pictureOut.flush();
            check (engine.saveSession() && SessionBundle::readEntry (file, SessionBundle::previewEntry).getSize() > 0
                     && engine.getRecentSessions().front().preview.existsAsFile(),
                   "its preview is saved inside it, and the recent list can show it");
        }

        pumpMessages();
        std::cout << "New session again\n";
        engine.newSession();
        check (engine.getTracks().getTracks().size() == 1, "new session replaces the old one");

        std::cout << "Session templates\n";
        engine.newSession ({ SessionTemplate::songwriter, 92.0, 3, 4, 9, true });
        pumpMessages();
        auto songwriter = engine.getTracks().getTracks();
        check (songwriter.size() == 5, "Songwriter has five tracks");

        if (songwriter.size() == 5)
        {
            check (songwriter[0].name == "Lo-fi Kit" && songwriter[3].name == "Soft Piano",
                   "instrument tracks are named after their sounds");
            check (songwriter[4].kind == TrackKind::audio && songwriter[4].name == "Voice", "and there's a vocal track");
        }

        check (std::abs (engine.getTransportState().bpm - 92.0) < 0.01, "the template takes the chosen tempo");
        check (engine.getTransportState().timeSigNumerator == 3, "time signature");
        check (engine.getKeyName() == "A min", "and key");
        // Instruments finish loading in the background; the engine then wipes
        // the undo history once things go quiet (see startFromCleanSlateSoon).
        juce::MessageManager::getInstance()->runDispatchLoopUntil (1500);
        check (! engine.hasUnsavedChanges() && ! engine.canUndo(), "a new session starts clean, with nothing to undo");

        for (auto kind : { SessionTemplate::keyboards, SessionTemplate::voice, SessionTemplate::beats })
        {
            engine.newSession ({ kind });
            pumpMessages();
            check (engine.getTracks().getTracks().size() >= 2, "every template makes its tracks");
        }

        engine.newSession();
    }

    if (runs ("main"))
    {
        std::cout << "Open (fresh engine)\n";
        AudioEngine engine { testSettings };
        check (engine.openSession (file), "open succeeds");
        pumpMessages();

        auto list = engine.getTracks().getTracks();
        check (list.size() == 2, "both tracks come back");

        if (list.size() == 2)
        {
            auto& t2 = list[1];
            auto regions = engine.getTracks().regions().getRegions (t2.id);
            check (t2.soundName == "Soft Piano", "sound name round-trips");
            check (t2.muted, "mute round-trips");
            check (regions.size() == 1 && regions[0].notes.size() == 28, "region and notes round-trip");
            auto chain = engine.getTracks().effects().getEffectChain (t2.id);
            check (chain.size() == 3 && ! chain[0].enabled && chain[0].name == "Compressor", "effect chain round-trips");
            auto instrument = engine.getTracks().effects().getInstrument (t2.id);
            check (instrument && instrument->name == "Sampler", "the sampled instrument round-trips");
        }

        check (std::abs (engine.getTransportState().bpm - 96.0) < 0.001, "tempo round-trips");
    }

    if (runs ("main"))
    {
        AudioEngine engine { testSettings };
        auto& tracks = engine.getTracks();
        pumpMessages();

        std::cout << "Region editing\n";
        // Each edit is followed by pumpMessages(): Tracktion tidies up after
        // some edits on the next message-loop turn (e.g. re-sorting clips),
        // and that belongs to the edit's own undo step.
        auto t1 = tracks.getTracks().front().id;
        auto t2 = tracks.addInstrumentTrack();
        auto r1 = tracks.regions().createChordRegion (t1, 0.0);    // beats 0-16
        auto r2 = tracks.regions().createChordRegion (t1, 16.0);   // beats 16-32
        pumpMessages();

        tracks.regions().moveRegions ({ { r1, t2, 4.0 } });
        pumpMessages();
        auto moved = tracks.regions().getRegion (r1);
        check (moved && moved->trackId == t2 && moved->startBeat == 4.0, "a region moves to another track and beat");
        engine.undo();
        pumpMessages();
        moved = tracks.regions().getRegion (r1);
        check (moved && moved->trackId == t1 && moved->startBeat == 0.0, "undo puts it back");

        tracks.regions().moveRegions ({ { r1, t1, 8.0 } });   // lands on half of r2
        pumpMessages();
        auto under = tracks.regions().getRegion (r2);
        check (under && under->startBeat == 24.0 && under->lengthBeats == 8.0, "a moved region cuts the region it lands on");
        engine.undo();
        pumpMessages();
        under = tracks.regions().getRegion (r2);
        check (under && under->startBeat == 16.0 && under->lengthBeats == 16.0, "undo restores the cut region");

        tracks.regions().trimRegion (r1, 4.0, 12.0);
        pumpMessages();
        auto trimmed = tracks.regions().getRegion (r1);
        check (trimmed && trimmed->startBeat == 4.0 && trimmed->lengthBeats == 8.0, "both edges trim");
        check (trimmed && ! trimmed->notes.empty() && trimmed->notes.front().startBeat == -4.0,
               "trimming the start leaves the notes where they were");
        tracks.regions().trimRegion (r1, 0.0, 12.0);
        pumpMessages();
        trimmed = tracks.regions().getRegion (r1);
        check (trimmed && trimmed->startBeat == 0.0 && trimmed->notes.front().startBeat == 0.0, "trimming back out reveals them");

        tracks.regions().moveRegions ({ { r1, t2, 4.0 } });   // a MIDI region can grow past its first note
        pumpMessages();
        tracks.regions().trimRegion (r1, 0.0, 16.0);
        pumpMessages();
        trimmed = tracks.regions().getRegion (r1);
        check (trimmed && trimmed->startBeat == 0.0 && trimmed->notes.front().startBeat == 4.0,
               "growing a MIDI region to the left keeps the notes in place");
        engine.undo();
        pumpMessages();
        check (engine.canRedo(), "redo is still available after undoing a trim");
        engine.undo();
        pumpMessages();
        moved = tracks.regions().getRegion (r1);
        check (moved && moved->trackId == t1 && moved->startBeat == 0.0 && moved->lengthBeats == 12.0, "two undos, two steps back");

        auto halves = tracks.regions().splitRegions ({ r1, r2 }, 6.0);
        pumpMessages();
        auto left = tracks.regions().getRegion (r1);
        check (halves.size() == 1, "split only cuts regions under the split point");
        check (left && left->lengthBeats == 6.0, "the left half ends at the split");
        auto right = halves.empty() ? std::nullopt : tracks.regions().getRegion (halves.front());
        check (right && right->startBeat == 6.0 && right->lengthBeats == 6.0, "the right half starts there");
        engine.undo();
        pumpMessages();

        auto copies = tracks.regions().duplicateRegions ({ r1, r2 });
        pumpMessages();
        check (copies.size() == 2, "duplicate copies every region");
        auto copy = copies.empty() ? std::nullopt : tracks.regions().getRegion (copies.front());
        check (copy && copy->startBeat == 32.0 && copy->notes.size() == 28, "copies go after the last region, notes and all");
        engine.undo();
        pumpMessages();

        tracks.regions().copyRegions ({ r1 });
        check (tracks.regions().canPaste(), "copy fills the clipboard");
        auto pasted = tracks.regions().pasteRegions (t2, 40.0);
        pumpMessages();
        auto paste = pasted.empty() ? std::nullopt : tracks.regions().getRegion (pasted.front());
        check (paste && paste->trackId == t2 && paste->startBeat == 40.0, "paste goes on the chosen track and beat");
        check (pasted.size() == 1 && tracks.regions().pasteRegions (t2, 60.0).size() == 1, "paste can repeat");
        pumpMessages();

        tracks.regions().renameRegion (r1, "Verse");
        pumpMessages();
        check (tracks.regions().getRegion (r1)->name == "Verse", "rename");
        tracks.regions().deleteRegions ({ r1, r2 });
        pumpMessages();
        check (tracks.regions().getRegions (t1).empty(), "delete removes several regions at once");
        engine.undo();
        pumpMessages();
        check (tracks.regions().getRegions (t1).size() == 2, "...in one undo step");

        std::cout << "Notes\n";
        {
            auto region = tracks.regions().getRegion (r2);
            const auto noteCount = region->notes.size();

            // Finds a note by its ID in a fresh read of the region.
            auto findNote = [&] (NoteId id) -> std::optional<NoteInfo>
            {
                if (auto r = tracks.regions().getRegion (r2))
                    for (auto& n : r->notes)
                        if (n.id == id)
                            return n;

                return std::nullopt;
            };

            std::set<NoteId> ids;

            for (auto& n : region->notes)
                ids.insert (n.id);

            check (ids.size() == noteCount && ids.count (0) == 0, "every note has an ID of its own");

            auto id = tracks.notes().addNote (r2, { 72, 1.1, 0.5, 90 });
            pumpMessages();
            auto note = findNote (id);
            check (id != 0 && ids.count (id) == 0 && tracks.regions().getRegion (r2)->notes.size() == noteCount + 1,
                   "add a note (it gets a new ID)");
            check (note && note->pitch == 72 && std::abs (note->startBeat - 1.1) < 1e-6, "the note is where it was drawn");

            // Moving it past other notes re-orders the list, but the ID stays.
            tracks.notes().changeNotes (r2, { { 74, 2.3, 1.0, 40, id } }, "Move Note");
            pumpMessages();
            note = findNote (id);
            check (note && note->pitch == 74 && std::abs (note->startBeat - 2.3) < 1e-6 && note->lengthBeats == 1.0
                     && note->velocity == 40, "move, resize and velocity keep the note's ID");
            check (engine.getUndoDescription() == "Move Note", "the note edit has its own undo step");

            tracks.notes().quantiseNotes (r2, { id }, QuantizeSettings { 1.0 / 16.0 });   // 1/16 = a quarter beat in 4/4
            pumpMessages();
            note = findNote (id);
            check (note && note->pitch == 74 && std::abs (note->startBeat - 2.25) < 1e-6, "quantize snaps to the grid, same ID");

            engine.undo();   // the quantize
            pumpMessages();
            note = findNote (id);
            check (note && std::abs (note->startBeat - 2.3) < 1e-6, "undo puts the note back, same ID");
            engine.redo();
            pumpMessages();
            note = findNote (id);
            check (note && std::abs (note->startBeat - 2.25) < 1e-6, "and redo, same ID");

            auto notesSession = dir.getChildFile ("Notes");
            check (engine.saveSessionAs (notesSession), "(save)");
            check (engine.openSession (AudioEngine::getSessionFileFor (notesSession)), "(reopen)");
            pumpMessages();
            note = findNote (id);
            check (note && note->pitch == 74 && std::abs (note->startBeat - 2.25) < 1e-6,
                   "the note keeps its ID through save and reopen");

            tracks.notes().deleteNotes (r2, { id });
            pumpMessages();
            check (tracks.regions().getRegion (r2)->notes.size() == noteCount && ! findNote (id), "delete a note by ID");
        }

        std::cout << "Count-in\n";
        engine.setCountInEnabled (true);
        check (engine.getTransportState().countIn && engine.getCountInBars() == 1, "count-in turns on (one bar to begin with)");
        engine.setCountInEnabled (false);
        check (! engine.getTransportState().countIn && engine.getCountInBars() == 0, "and off");
        engine.setCountInBars (2);
        check (engine.getTransportState().countIn && engine.getCountInBars() == 2, "it can be two bars");
        engine.setCountInEnabled (false);
        engine.setCountInEnabled (true);
        check (engine.getCountInBars() == 2, "turning it back on keeps the length last chosen");
        engine.setCountInBars (1);
        engine.setCountInEnabled (false);

        std::cout << "Key, pan and master volume\n";
        engine.setKey (3, true);
        pumpMessages();
        auto ts = engine.getTransportState();
        check (ts.keyRoot == 3 && ts.keyMinor && engine.getKeyName() == "Eb min", "the key can be changed");
        engine.undo();
        pumpMessages();
        check (engine.getKeyName() == "C maj", "undo restores the key");
        engine.redo();
        pumpMessages();

        engine.beginUndoStep ("Pan");
        tracks.setPan (t1, -0.5f);
        pumpMessages();
        check (std::abs (tracks.getTrack (t1)->pan + 0.5f) < 0.01f, "pan");
        engine.undo();
        pumpMessages();
        check (std::abs (tracks.getTrack (t1)->pan) < 0.01f, "undo restores the pan");
        engine.redo();
        pumpMessages();

        const auto masterBefore = tracks.getMasterVolume();
        engine.beginUndoStep ("Master Volume");
        tracks.setMasterVolume (0.3f);
        pumpMessages();
        check (std::abs (tracks.getMasterVolume() - 0.3f) < 0.01f && tracks.getMasterVolumeDb() < 0.0f, "master volume");
        engine.undo();
        pumpMessages();
        check (std::abs (tracks.getMasterVolume() - masterBefore) < 0.01f, "undo restores the master volume");
        engine.redo();
        pumpMessages();

        std::cout << "Sound library\n";
        {
            auto& sounds = engine.getSounds().getSounds();
            auto has = [&sounds] (const juce::String& id)
            {
                return std::any_of (sounds.begin(), sounds.end(), [&id] (const SoundInfo& s) { return s.id == id; });
            };

            check (has (SoundLibrary::defaultSoundId) && has ("synth:Warm Pad"), "the built-in synth patches are there");
            check (has ("sampler:ikaros-studio.basics/Soft Piano") && has ("sampler:ikaros-studio.basics/Electronic Kit"),
                   "the bundled sampled instruments are there");
            check (engine.getSounds().getLoops().size() >= 4, "and the loops");
            check (tracks.getTrack (t1)->soundId == SoundLibrary::defaultSoundId, "new tracks use the default sound");

            auto& effects = tracks.effects();
            const auto oldName = tracks.getTrack (t2)->name;
            check (oldName.startsWith ("Instrument"), "(new tracks are named Instrument N)");
            check (effects.setSound (t2, "sampler:ikaros-studio.basics/Electronic Kit"), "picking a sound swaps the instrument");
            pumpMessages();
            check (tracks.getTrack (t2)->name == "Electronic Kit" && tracks.getTrack (t2)->category == "Drums",
                   "a track with the app's name takes the sound's name");
            const auto kitKnobs = effects.getSoundControls (t2);
            check (effects.getInstrument (t2)->name == "Sampler" && kitKnobs.size() == 4 && kitKnobs[0].name == "Tone"
                     && std::none_of (kitKnobs.begin(), kitKnobs.end(), [] (const SoundControl& k) { return k.name == "Pitch"; }),
                   "a sampled kit gets the Drums knobs: four, without Pitch (it would swap drums)");
            check (engine.getUndoDescription() == "Change Sound", "it's one named undo step");
            engine.undo();
            pumpMessages();
            check (tracks.getTrack (t2)->name == oldName && effects.getInstrument (t2)->name == "4OSC Synth",
                   "undo brings back the old instrument and name");
            check (effects.getSoundControls (t2).size() == 5, "and its knobs");

            tracks.renameTrack (t2, "My Beat");
            pumpMessages();
            effects.setSound (t2, "synth:Warm Pad");
            pumpMessages();
            check (tracks.getTrack (t2)->name == "My Beat" && tracks.getTrack (t2)->soundName == "Warm Pad",
                   "a name the user typed is kept");
            check (! effects.setSound (t2, "sampler:No Such Pack/Nothing"), "an unknown sound changes nothing");
            check (tracks.getTrack (t2)->soundName == "Warm Pad", "(still Warm Pad)");
        }

        std::cout << "Sends\n";
        {
            auto& sends = tracks.sends();
            const auto trackCount = tracks.getTracks().size();
            check (sends.getSendLevel (t1, SendBuses::reverb) == 0.0f, "sends start at zero");
            engine.beginUndoStep ("Reverb Send");
            sends.setSendLevel (t1, SendBuses::reverb, 0.5f);
            pumpMessages();
            check (std::abs (tracks.getTrack (t1)->reverbSend - 0.5f) < 0.01f, "a send can be turned up");
            check (tracks.getTracks().size() == trackCount, "the bus tracks stay hidden");
            check (tracks.effects().getEffectChain (t1).size() == 0, "sends aren't listed as effects");
            engine.undo();
            pumpMessages();
            check (tracks.getTrack (t1)->reverbSend == 0.0f, "undo turns it back down");
            engine.redo();
            pumpMessages();
            engine.beginUndoStep ("Delay Send");
            sends.setSendLevel (t1, SendBuses::delay, 0.25f);
            pumpMessages();
            check (std::abs (tracks.getTrack (t1)->delaySend - 0.25f) < 0.01f && std::abs (tracks.getTrack (t1)->reverbSend - 0.5f) < 0.01f,
                   "each bus has its own send");
        }

        std::cout << "EQ and master effects\n";
        {
            auto& eq = tracks.eq();
            const auto flat = eq.getEq (t1);
            check (flat.enabled && flat.bands[1].name == "Low Mid" && flat.bands[1].gainDb == 0.0f
                     && flat.bands[1].frequencyHz == EqController::getDefaults().bands[1].frequencyHz,
                   "a track's EQ starts flat");
            check (eq.getResponse (t1, { 100.0f, 1000.0f })[1] == 0.0f, "a flat EQ draws a flat line");

            const auto effectCount = tracks.effects().getEffectChain (t1).size();
            engine.beginUndoStep ("EQ");
            eq.setBand (t1, 1, 400.0f, 6.0f, 0.7f);
            pumpMessages();
            auto changed = eq.getEq (t1);
            check (std::abs (changed.bands[1].gainDb - 6.0f) < 0.01f && std::abs (changed.bands[1].frequencyHz - 400.0f) < 0.5f,
                   "a band can be boosted");
            check (tracks.effects().getEffectChain (t1).size() == effectCount, "the EQ isn't listed as an effect");
            auto response = eq.getResponse (t1, { 30.0f, 400.0f, 15000.0f });
            check (std::abs (response[1] - 6.0f) < 0.1f && std::abs (response[0]) < 0.5f && std::abs (response[2]) < 0.5f,
                   "its curve peaks at the band");
            check (engine.getUndoDescription() == "EQ", "the change is one undo step");

            engine.undo();
            pumpMessages();
            check (eq.getEq (t1).bands[1].gainDb == 0.0f, "undo flattens it again");
            engine.redo();
            pumpMessages();
            check (std::abs (eq.getEq (t1).bands[1].gainDb - 6.0f) < 0.01f, "redo boosts it again");

            eq.setEnabled (t1, false);
            pumpMessages();
            check (! eq.getEq (t1).enabled && eq.getResponse (t1, { 400.0f })[0] == 0.0f, "the EQ can be turned off");
            eq.setEnabled (t1, true);
            pumpMessages();

            // The master channel has an EQ and effects of its own.
            engine.beginUndoStep ("EQ");
            eq.setBand (masterTrackId, 3, 8000.0f, -4.0f, 0.7f);
            pumpMessages();
            check (std::abs (eq.getEq (masterTrackId).bands[3].gainDb + 4.0f) < 0.01f, "the master has its own EQ");
            check (eq.getEq (t1).bands[3].gainDb == 0.0f, "separate from the tracks'");
            auto masterReverb = tracks.effects().addEffect (masterTrackId, "reverb");
            pumpMessages();
            // (New sessions start with a limiter on the master.)
            check (masterReverb != 0 && tracks.effects().getEffectChain (masterTrackId).size() == 2,
                   "the master can have effects");
            check (tracks.effects().getEffectChain (0).empty() && eq.getEq (0).bands[3].gainDb == 0.0f,
                   "\"no track\" (0) never touches the master");

            // Chorus and Phaser keep their settings as properties, not
            // parameters; they still get knobs.
            auto chorus = tracks.effects().addEffect (t1, "chorus");
            pumpMessages();
            auto chorusParams = tracks.effects().getEffectParameters (t1, chorus);
            check (chorusParams.size() == 4 && chorusParams[1].name == "Speed", "Chorus has Depth, Speed, Width and Mix knobs");
            engine.beginUndoStep ("Speed");
            tracks.effects().setEffectParameter (t1, chorus, 1, 1.0f);
            pumpMessages();
            check (tracks.effects().getEffectParameters (t1, chorus)[1].valueText == "5.0 Hz", "a Chorus knob changes the effect");
            engine.undo();
            pumpMessages();
            check (tracks.effects().getEffectParameters (t1, chorus)[1].valueText == "1.0 Hz", "and undo puts it back");
            check (tracks.effects().getEffectParameters (t1, tracks.effects().addEffect (t1, "phaser")).size() == 3,
                   "Phaser has Depth, Rate and Feedback knobs");
            pumpMessages();

            // The Delay's mix parameter ("mix proportion") is stored as "mix":
            // its ID must never end up as a property (invalid XML).
            auto echo = tracks.effects().addEffect (t1, "delay");
            pumpMessages();
            const auto echoMix = tracks.effects().getEffectParameters (t1, echo)[1].value;
            engine.beginUndoStep ("Mix");
            tracks.effects().setEffectParameter (t1, echo, 1, 0.9f);
            pumpMessages();
            engine.undo();
            pumpMessages();
            check (std::abs (tracks.effects().getEffectParameters (t1, echo)[1].value - echoMix) < 0.01f,
                   "undoing the first change to an effect knob restores it");
            engine.beginUndoStep ("Mix");
            tracks.effects().setEffectParameter (t1, echo, 1, 0.9f);
            pumpMessages();

            auto eqSession = dir.getChildFile ("EQ");
            check (engine.saveSessionAs (eqSession), "(save)");
            const auto savedEdit = SessionBundle::readEntry (AudioEngine::getSessionFileFor (eqSession),
                                                             SessionBundle::sessionEntry).toString();
            check (juce::parseXML (savedEdit) != nullptr && ! savedEdit.contains ("mix proportion"),
                   "effect settings save as valid XML");
            check (engine.openSession (AudioEngine::getSessionFileFor (eqSession)), "(reopen)");
            pumpMessages();
            check (std::abs (tracks.effects().getEffectParameters (t1, tracks.effects().getEffectChain (t1).back().id)[1].value - 0.9f) < 0.01f,
                   "and survive reopening");

            // Sessions saved before the fix have those invalid attributes;
            // they still open.
            {
                auto broken = dir.getChildFile ("Broken Echo.iksession");
                auto brokenEdit = dir.getChildFile ("Broken Echo.tracktionedit");
                brokenEdit.replaceWithText (savedEdit.replace ("<PLUGIN type=\"delay\"",
                                                               "<PLUGIN type=\"delay\" mix proportion=\"0.3\" 12=\"0.5\""));
                SessionBundle::pack (broken, { { SessionBundle::sessionEntry, brokenEdit, true } }, {});
                check (engine.openSession (broken) && tracks.effects().getEffectChain (t1).back().name == "Echo",
                       "a session saved with invalid effect attributes still opens");
            }
            check (std::abs (eq.getEq (t1).bands[1].gainDb - 6.0f) < 0.01f
                     && std::abs (eq.getEq (masterTrackId).bands[3].gainDb + 4.0f) < 0.01f,
                   "track and master EQ survive save and reopen");
            check (tracks.effects().getEffectChain (masterTrackId).size() == 2, "and so do master effects");
        }

        std::cout << "Audio inputs\n";
        {
            auto inputs = tracks.inputs().getAudioInputs();
            int mono = 0, stereo = 0;

            for (auto& in : inputs)
                (in.name.contains ("stereo") ? stereo : mono)++;

            std::cout << "  (" << mono << " mono, " << stereo << " stereo)\n";
            check (mono < 2 || stereo >= 1, "two inputs also make a stereo pair");
        }

        std::cout << "Audio files and recordings\n";
        auto writeWav = [] (const juce::File& f, double seconds)
        {
            f.deleteFile();
            juce::WavAudioFormat wav;
            std::unique_ptr<juce::OutputStream> stream (f.createOutputStream().release());
            auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (44100.0)
                                                                                        .withNumChannels (1)
                                                                                        .withBitsPerSample (16));
            juce::AudioBuffer<float> buffer (1, (int) (seconds * 44100.0));

            for (int i = 0; i < buffer.getNumSamples(); ++i)
                buffer.setSample (0, i, 0.5f * std::sin ((float) i * 0.05f));

            return writer != nullptr && writer->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples());
        };

        auto imported = dir.getChildFile ("Loop.wav");
        check (writeWav (imported, 2.0), "(wrote a test WAV)");
        check (tracks.regions().isSupportedAudioFile (imported), "WAV files can be imported");
        check (! tracks.regions().isSupportedAudioFile (file), "session files can't");

        const auto trackCount = tracks.getTracks().size();
        auto added = tracks.regions().importAudioFiles (t1, { imported.getFullPathName() }, 8.0);   // t1 is an instrument track
        auto audioRegion = added.empty() ? std::nullopt : tracks.regions().getRegion (added.front());
        auto audioTrack = audioRegion ? tracks.getTrack (audioRegion->trackId) : std::nullopt;
        check (tracks.getTracks().size() == trackCount + 1 && audioTrack && audioTrack->kind == TrackKind::audio,
               "dropping on an instrument track makes an audio track");
        check (audioTrack && audioTrack->name == "Loop", "named after the file");
        check (audioRegion && ! audioRegion->isMidi && audioRegion->startBeat == 8.0
                 && std::abs (audioRegion->lengthBeats - 4.0) < 0.01, "the region is as long as the file (2 s = 4 beats)");
        check (audioRegion && ! audioRegion->peaks.empty(), "the region has a waveform");
        check (audioRegion && std::abs (audioRegion->maxEndBeat - 12.0) < 0.01, "an audio region can't grow past its file");
        pumpMessages();
        engine.undo();
        pumpMessages();
        check (tracks.getTracks().size() == trackCount, "undo removes the import and its new track in one step");
        engine.redo();
        pumpMessages();

        auto audioTrackId = audioRegion ? audioRegion->trackId : TrackId {};
        check (tracks.regions().importAudioFiles (audioTrackId, { imported.getFullPathName() }, 0.0).size() == 1
                 && tracks.getTracks().size() == trackCount + 1, "dropping on an audio track adds to it");
        {
            engine.setBpm (100.0);
            auto loop = engine.getSounds().getLoops().front();
            auto loopRegions = tracks.regions().importAudioFiles (audioTrackId, { loop.file.getFullPathName() }, 64.0);
            auto loopRegion = loopRegions.empty() ? std::nullopt : tracks.regions().getRegion (loopRegions.front());
            check (loopRegion && std::abs (loopRegion->lengthBeats - 8.0) < 0.05,
                   "a library loop fits the session's tempo (2 bars at 120 BPM, played at 100)");
            pumpMessages();
            engine.undo();
            pumpMessages();
            engine.undo();   // the tempo
            pumpMessages();
        }

        tracks.regions().moveRegions ({ { r2, audioTrackId, 0.0 } });
        check (tracks.regions().getRegion (r2)->trackId == t1, "MIDI regions can't move to audio tracks");

        // A take from somewhere outside the session, with a name Windows
        // wouldn't accept (a track called "Verse: 1", say).
        auto takes = dir.getChildFile ("Takes");
        takes.createDirectory();
        auto take = takes.getChildFile ("Verse: 1 Take?.wav");
        check (writeWav (take, 1.0), "(wrote a test take)");
        tracks.regions().importAudioFiles (audioTrackId, { take.getFullPathName() }, 20.0);

        // What's inside a bundle: unpack it and look.
        auto contents = [&dir] (const juce::File& bundle)
        {
            auto folder = dir.getChildFile ("Unpacked");
            folder.deleteRecursively();
            SessionBundle::unpack (bundle, folder);
            return folder.getChildFile (SessionBundle::audioFolder);
        };

        auto songFile = AudioEngine::getSessionFileFor (dir.getChildFile ("Song"));
        check (songFile == dir.getChildFile ("Song.iksession") && AudioEngine::getSessionFileFor (songFile) == songFile,
               "Save As adds the .iksession extension (once)");
        check (engine.saveSessionAs (dir.getChildFile ("Song")), "Save As");
        check (songFile.existsAsFile() && engine.getSessionFile() == songFile, "saved as that file");
        auto songAudio = contents (songFile);
        check (songAudio.getChildFile ("Verse_ 1 Take_.wav").existsAsFile(),
               "takes go into the bundle, with names every system accepts");
        check (songAudio.getChildFile (imported.getFileName()).existsAsFile(), "and imported files");
        check (songAudio.findChildFiles (juce::File::findFiles, false, "Loop*").size() == 1,
               "a file used by two regions is copied once");
        check (engine.saveSession() && engine.saveSession(), "saving again (paths must read back the same every time)");
        take.deleteFile();   // the session must now use its own copies
        imported.deleteFile();

        auto moveDir = dir.getChildFile ("Elsewhere");
        moveDir.createDirectory();
        auto otherFile = AudioEngine::getSessionFileFor (moveDir.getChildFile ("Other"));
        check (engine.saveSessionAs (otherFile), "Save As somewhere else");
        check (engine.saveSession(), "and save again");

        auto late = dir.getChildFile ("Late.wav");
        check (writeWav (late, 1.0), "(wrote another test WAV)");
        tracks.regions().importAudioFiles (audioTrackId, { late.getFullPathName() }, 40.0);
        check (engine.saveSession() && engine.saveSession(), "save twice after adding a file");
        late.deleteFile();
        check (contents (otherFile).getChildFile ("Verse_ 1 Take_.wav").existsAsFile(), "Save As takes the session's audio along");
        songFile.deleteFile();   // the new bundle must stand on its own

        std::cout << "Session bundles\n";
        {
            check (SessionBundle::makePortableFileName ("Verse: 1?.wav") == "Verse_ 1_.wav"
                     && SessionBundle::makePortableFileName ("CON.wav") == "_CON.wav"
                     && SessionBundle::makePortableFileName ("Take. ") == "Take"
                     && SessionBundle::makePortableFileName (juce::String::fromUTF8 ("Caf\xc3\xa9 \xe2\x99\xab.flac"))
                          == juce::String::fromUTF8 ("Caf\xc3\xa9 \xe2\x99\xab.flac"),
                   "file names are made valid on every system (and other languages are kept)");

            // A bundle that tries to write outside its folder ("zip slip").
            auto evil = dir.getChildFile ("Evil.iksession");
            {
                mz_zip_archive zip;
                mz_zip_zero_struct (&zip);
                mz_zip_writer_init_file (&zip, evil.getFullPathName().toRawUTF8(), 0);
                mz_zip_writer_add_mem (&zip, "../Escaped.txt", "x", 1, MZ_NO_COMPRESSION);
                mz_zip_writer_finalize_archive (&zip);
                mz_zip_writer_end (&zip);
            }

            auto into = dir.getChildFile ("EvilUnpacked");
            check (SessionBundle::unpack (evil, into).failed() && ! dir.getChildFile ("Escaped.txt").exists(),
                   "a bundle can't write outside its folder");
            check (! engine.openSession (evil) && engine.getSessionFile() == otherFile, "and such a bundle doesn't open");

            // The working copy goes when the session is replaced.
            auto workFolder = engine.getPreviewFile().getParentDirectory();
            check (workFolder.isDirectory(), "(the session has a working copy)");
            check (engine.openSession (otherFile) && ! workFolder.exists(), "a replaced session's working copy is deleted");
        }

        std::cout << "Sessions from earlier versions\n";
        {
            // A session with a sampled piano playing, and audio regions.
            auto piano = tracks.addInstrumentTrack();
            check (tracks.effects().setSound (piano, "sampler:ikaros-studio.basics/Soft Piano"), "(a sampled piano)");
            tracks.regions().createChordRegion (piano, 0.0);
            pumpMessages();
            auto portable = dir.getChildFile ("Portable.iksession");
            check (engine.saveSessionAs (portable), "(saved)");

            // Turn it into a session folder the way earlier versions saved
            // them, as if made on another computer: old-style sound ids, and
            // the sampler's files at a Windows path that doesn't exist here.
            auto legacyDir = dir.getChildFile ("Legacy");
            legacyDir.deleteRecursively();
            SessionBundle::unpack (portable, legacyDir);
            auto legacyFile = legacyDir.getChildFile ("Legacy.tracktionedit");
            const auto basicsFolder = engine.getSounds().findPack ("ikaros-studio.basics")->folder.getFullPathName();
            legacyFile.replaceWithText (legacyDir.getChildFile (SessionBundle::sessionEntry).loadFileAsString()
                                          .replace ("sampler:ikaros-studio.basics/", "sampler:Ikaros Basics/")
                                          .replace (basicsFolder, "C:\\Users\\someone\\Ikaros Basics")
                                          .replace (basicsFolder.replaceCharacter ('/', '\\'), "C:\\Users\\someone\\Ikaros Basics"));
            legacyDir.getChildFile (SessionBundle::sessionEntry).deleteFile();
            check (legacyFile.loadFileAsString().contains ("C:\\Users\\someone") && ! legacyFile.loadFileAsString().contains (basicsFolder),
                   "(the old session points at another computer's files)");

            auto listFolder = [] (const juce::File& folder)
            {
                juce::StringArray files;

                for (auto& f : folder.findChildFiles (juce::File::findFiles, true))
                    files.add (f.getRelativePathFrom (folder) + " " + juce::String (f.getSize()));

                files.sort (false);
                return files;
            };

            const auto before = listFolder (legacyDir);

            check (engine.openSession (legacyFile), "a session folder from an earlier version opens");
            pumpMessages();
            check (engine.getSessionName() == "Legacy" && engine.getSessionFile() == dir.getChildFile ("Legacy.iksession"),
                   "it will be saved as a bundle next to its folder");

            TrackId legacyPiano = 0;
            int audioWithWaveforms = 0;

            for (auto& t : tracks.getTracks())
            {
                if (t.soundName == "Soft Piano")
                    legacyPiano = t.id;

                for (auto& r : tracks.regions().getRegions (t.id))
                    if (! r.isMidi && ! r.peaks.empty())
                        ++audioWithWaveforms;
            }

            check (legacyPiano != 0 && tracks.effects().getSoundId (legacyPiano) == "sampler:ikaros-studio.basics/Soft Piano",
                   "old sound ids are read, and kept the new way");
            check (engine.renderPeak (0.0, 4.0) > 0.01f, "the piano finds this computer's samples, and plays");
            check (audioWithWaveforms >= 4, "its audio loads from the old folder");

            check (engine.saveSession() && dir.getChildFile ("Legacy.iksession").existsAsFile(), "saving writes the bundle");
            check (listFolder (legacyDir) == before, "and leaves the old folder exactly as it was");

            legacyDir.deleteRecursively();
            check (engine.openSession (dir.getChildFile ("Legacy.iksession")), "the new bundle opens on its own");
            pumpMessages();
            int stillThere = 0;

            for (auto& t : tracks.getTracks())
                for (auto& r : tracks.regions().getRegions (t.id))
                    if (! r.isMidi && ! r.peaks.empty())
                        ++stillThere;

            check (stillThere == audioWithWaveforms, "with all its audio");
        }

        std::cout << "Region looping\n";
        {
            engine.newSession();
            pumpMessages();
            auto lt = tracks.getTracks().front().id;
            auto lr = tracks.regions().createChordRegion (lt, 0.0);   // 4 bars = 16 beats, 28 notes
            pumpMessages();

            check (tracks.regions().loopRegion (lr, 40.0), "dragging the loop corner loops a MIDI region");
            auto looped = tracks.regions().getRegion (lr);
            check (looped && looped->isLooped() && looped->lengthBeats == 40.0, "looping extends the region");
            check (looped && looped->loopLengthBeats == 16.0 && looped->loopOffsetBeats == 0.0,
                   "what the region showed becomes the part that repeats");
            check (looped && looped->notes.size() == 28 && looped->getContentLengthBeats() == 16.0,
                   "the notes are one loop's worth, not copies");
            check (engine.getUndoDescription() == "Loop Region", "one \"Loop Region\" undo step");
            check (! tracks.regions().loopRegion (lr, 40.0), "looping to the same length changes nothing");

            pumpMessages();
            engine.undo();
            pumpMessages();
            auto undone = tracks.regions().getRegion (lr);
            check (undone && ! undone->isLooped() && undone->lengthBeats == 16.0, "undo restores the unlooped region");
            engine.redo();
            pumpMessages();
            check (tracks.regions().getRegion (lr)->lengthBeats == 40.0, "redo loops it again");

            // Trimming the start of a looped region moves where in the loop it starts.
            tracks.regions().trimRegion (lr, 4.0, 40.0);
            pumpMessages();
            auto trimmed = tracks.regions().getRegion (lr);
            check (trimmed && trimmed->startBeat == 4.0 && trimmed->lengthBeats == 36.0
                     && trimmed->isLooped() && std::abs (trimmed->loopOffsetBeats - 4.0) < 1.0e-6,
                   "trimming a looped region's start keeps the loop lined up");

            // Growing it back out to the left wraps round the loop.
            tracks.regions().trimRegion (lr, 0.0, 40.0);
            pumpMessages();
            auto regrown = tracks.regions().getRegion (lr);
            check (regrown && regrown->startBeat == 0.0 && std::abs (regrown->loopOffsetBeats) < 1.0e-6,
                   "trimming it back out to the left lines up again");

            // Split in the middle of the second loop.
            auto halves = tracks.regions().splitRegions ({ lr }, 20.0);
            pumpMessages();
            auto left = tracks.regions().getRegion (lr);
            auto right = halves.empty() ? std::nullopt : tracks.regions().getRegion (halves.front());
            check (left && left->isLooped() && left->lengthBeats == 20.0, "splitting a looped region keeps the left half looped");
            check (right && right->isLooped() && right->startBeat == 20.0 && right->lengthBeats == 20.0
                     && std::abs (right->loopOffsetBeats - 4.0) < 1.0e-6 && right->notes.size() == 28,
                   "and the right half carries on from the same place in the loop");

            // Audio: a plain file (no tempo info) loops too.
            auto audioFile = dir.getChildFile ("LoopMe.wav");
            check (writeWav (audioFile, 2.0), "(wrote a test WAV to loop)");
            auto audioIds = tracks.regions().importAudioFiles (0, { audioFile.getFullPathName() }, 0.0);   // 2 s = 4 beats
            pumpMessages();
            auto ar = audioIds.empty() ? RegionId {} : audioIds.front();
            check (tracks.regions().loopRegion (ar, 16.0), "an audio region loops");
            auto loopedAudio = tracks.regions().getRegion (ar);
            check (loopedAudio && loopedAudio->lengthBeats == 16.0 && std::abs (loopedAudio->loopLengthBeats - 4.0) < 0.01,
                   "repeating its 4 beats");
            check (loopedAudio && ! loopedAudio->peaks.empty() && loopedAudio->maxEndBeat > 1000.0,
                   "it still has a waveform, and can be looped as far as you like");

            auto loopSession = dir.getChildFile ("Looping");
            check (engine.saveSessionAs (loopSession), "save the looped regions");
            check (engine.openSession (AudioEngine::getSessionFileFor (loopSession)), "and reopen");
            pumpMessages();
            auto reopened = tracks.regions().getRegion (halves.empty() ? RegionId {} : halves.front());
            auto reopenedAudio = tracks.regions().getRegion (ar);
            check (reopened && reopened->isLooped() && reopened->loopLengthBeats == 16.0
                     && std::abs (reopened->loopOffsetBeats - 4.0) < 1.0e-6 && reopened->notes.size() == 28,
                   "a looped MIDI region survives save and reopen");
            check (reopenedAudio && reopenedAudio->lengthBeats == 16.0 && std::abs (reopenedAudio->loopLengthBeats - 4.0) < 0.01,
                   "so does a looped audio region");
            audioFile.deleteFile();
        }

        std::cout << "Sound pack manifests\n";
        {
            auto& sounds = engine.getSounds();
            sounds.addPackFolder (juce::File (IKAROS_TEST_FIXTURES));

            auto basics = sounds.findPack ("ikaros-studio.basics");
            check (basics && basics->bundled && basics->format == 2 && basics->name == "Ikaros Basics"
                     && basics->license == "CC0-1.0" && basics->credits.size() == 1,
                   "Ikaros Basics has a format 2 pack.json, comes with the app, and is CC0");

            auto test = sounds.findPack ("ikaros-studio.test");
            check (test && ! test->bundled && test->version == "1.2.3" && test->installedSize == 66282
                     && test->credits.size() == 1 && test->credits.front().attributionRequired,
                   "a format 2 pack: id, version, size and credits");

            auto old = sounds.findPack ("local.old-pack");
            check (old && old->format == 1 && old->version == "0.0.0" && old->name == "Old Pack"
                     && old->credits.size() == 1 && old->credits.front().who == "Made for the engine smoke test.",
                   "an old pack still loads, with an id made from its folder name");

            auto* testKeys = sounds.findSampledInstrument ("sampler:ikaros-studio.test/Test Keys");
            check (testKeys && testKeys->info.packId == "ikaros-studio.test" && testKeys->info.icon == "piano"
                     && testKeys->knobs.size() == 5 && testKeys->knobs[1].control == "cc" && testKeys->knobs[1].cc == 73,
                   "an SFZ instrument's icon and five knobs are read");

            auto* oldKeys = sounds.findSampledInstrument ("sampler:local.old-pack/Old Keys");
            check (oldKeys && oldKeys->info.packId == "local.old-pack" && oldKeys->zones.size() == 1 && oldKeys->knobs.empty(),
                   "an instrument without five valid knobs falls back to the defaults");

            check (SoundLibrary::compareVersions ("1.2.3", "1.10.0") < 0 && SoundLibrary::compareVersions ("2.0", "2.0.0") == 0
                     && SoundLibrary::compareVersions ("1.0.1", "1.0.0") > 0, "versions compare number by number");
            check (SoundLibrary::isValidPackId ("ikaros-studio.basics") && ! SoundLibrary::isValidPackId ("ikarosStudio.basics")
                     && ! SoundLibrary::isValidPackId ("my pack"), "pack ids are lowercase letters, digits, dots and dashes");
        }

        std::cout << "SFZ instruments (sfizz)\n";
        {
            engine.newSession();
            pumpMessages();
            const juce::String sfzSound ("sampler:ikaros-studio.test/Test Keys");
            check (engine.getSounds().findSound (sfzSound).has_value(), "an instrument.json can name an SFZ file");

            auto st = tracks.getTracks().front().id;
            check (tracks.effects().setSound (st, sfzSound), "an SFZ sound loads onto a track");
            check (tracks.effects().getInstrument (st).has_value() && tracks.effects().getInstrument (st)->name == "Sampler",
                   "it shows as the track's Sampler");

            // It loads in the background; wait for it.
            for (int i = 0; i < 100 && ! tracks.effects().isInstrumentReady (st); ++i)
                juce::MessageManager::getInstance()->runDispatchLoopUntil (50);

            check (tracks.effects().isInstrumentReady (st), "the SFZ file finishes loading");

            // A region with three middle Cs: loud, loud again, then soft. The
            // test samples play at 0.8 and 0.6 (the loud layer's two
            // round-robins) and 0.2 (the soft layer), whatever the velocity.
            auto sr = tracks.regions().createChordRegion (st, 0.0);
            pumpMessages();
            std::vector<NoteId> chordNotes;

            for (auto& n : tracks.regions().getRegion (sr)->notes)
                chordNotes.push_back (n.id);

            tracks.notes().deleteNotes (sr, chordNotes);
            tracks.notes().addNote (sr, { 60, 0.0, 1.0, 120 });
            tracks.notes().addNote (sr, { 60, 2.0, 1.0, 120 });
            tracks.notes().addNote (sr, { 60, 4.0, 1.0, 30 });
            pumpMessages();

            // Rendered offline, one note at a time (never through the speakers).
            const auto loudA = engine.renderPeak (0.0, 1.5);
            const auto loudB = engine.renderPeak (2.0, 3.5);
            const auto soft = engine.renderPeak (4.0, 5.5);
            std::cout << "  (peaks: " << loudA << ", " << loudB << ", " << soft << ")\n";

            check (loudA > 0.05f && loudB > 0.05f && soft > 0.01f, "the SFZ instrument plays");
            check (soft < juce::jmin (loudA, loudB) * 0.5f, "a soft note plays the soft velocity layer");
            check (std::abs (loudA - loudB) > juce::jmax (loudA, loudB) * 0.1f,
                   "repeated loud notes take turns between the round-robin samples");

            // Saved with the session: reopening loads the SFZ again.
            auto sfzSession = dir.getChildFile ("SFZ");
            check (engine.saveSessionAs (sfzSession), "(save)");
            check (engine.openSession (AudioEngine::getSessionFileFor (sfzSession)), "(reopen)");
            st = tracks.getTracks().front().id;

            for (int i = 0; i < 100 && ! tracks.effects().isInstrumentReady (st); ++i)
                juce::MessageManager::getInstance()->runDispatchLoopUntil (50);

            check (tracks.effects().getSoundId (st) == sfzSound && engine.renderPeak (0.0, 1.5) > 0.05f,
                   "an SFZ instrument survives save and reopen, and still plays");
        }

        std::cout << "Sound knobs for sampled instruments\n";
        {
            auto& effects = tracks.effects();
            engine.newSession();
            pumpMessages();
            auto kt = tracks.getTracks().front().id;

            // One short middle C (a quarter of a beat), then silence.
            auto oneShortNote = [&] (TrackId track)
            {
                auto region = tracks.regions().createChordRegion (track, 0.0);
                pumpMessages();
                std::vector<NoteId> ids;

                for (auto& n : tracks.regions().getRegion (region)->notes)
                    ids.push_back (n.id);

                tracks.notes().deleteNotes (region, ids);
                tracks.notes().addNote (region, { 60, 0.0, 0.25, 100 });
                pumpMessages();
            };

            check (effects.setSound (kt, "sampler:ikaros-studio.basics/Soft Piano"), "(a sampled piano)");
            pumpMessages();
            auto knobs = effects.getSoundControls (kt);
            check (knobs.size() == 5 && knobs[0].name == "Tone" && knobs[2].name == "Room"
                     && std::abs (knobs[2].value - 0.2f) < 0.001f && knobs[4].name == "Pitch" && knobs[4].valueText == "0",
                   "a sampled instrument without knobs of its own gets its category's five, at their defaults");
            check (effects.getEffectChain (kt).empty(), "the plugins behind the knobs aren't in the effects list");

            oneShortNote (kt);
            const int room = 2, pitch = 4;

            engine.beginUndoStep ("Room");
            effects.setSoundControl (kt, room, 0.0f);
            pumpMessages();
            // (renderPeak renders only the range it's given, after a short
            // pre-roll, so the tail is measured close behind the note.)
            const auto dryTail = engine.renderPeak (0.5, 1.0);
            engine.beginUndoStep ("Room");
            effects.setSoundControl (kt, room, 1.0f);
            pumpMessages();
            const auto wetTail = engine.renderPeak (0.5, 1.0);
            std::cout << "  (tail after the note: dry " << dryTail << ", room " << wetTail << ")\n";
            check (wetTail > juce::jmax (0.01f, dryTail * 4.0f), "Room adds a reverb tail to a sampled instrument");

            engine.undo();
            pumpMessages();
            check (effects.getSoundControls (kt)[room].value == 0.0f, "undo puts a knob back");
            engine.redo();
            pumpMessages();

            engine.beginUndoStep ("Pitch");
            effects.setSoundControl (kt, pitch, 0.74f);
            pumpMessages();
            check (effects.getSoundControls (kt)[pitch].valueText == "+6", "Pitch moves in whole semitones");

            // An SFZ instrument's "cc" knob: Release (cc72) lets a short note ring on.
            auto st = tracks.addInstrumentTrack();
            pumpMessages();
            check (effects.setSound (st, "sampler:ikaros-studio.test/Test Keys"), "(the SFZ test instrument)");

            for (int i = 0; i < 100 && ! effects.isInstrumentReady (st); ++i)
                juce::MessageManager::getInstance()->runDispatchLoopUntil (50);

            auto sfzKnobs = effects.getSoundControls (st);
            check (sfzKnobs.size() == 5 && sfzKnobs[1].name == "Attack" && sfzKnobs[2].name == "Release"
                     && std::abs (sfzKnobs[2].value - 0.3f) < 0.001f,
                   "an instrument's own knobs from instrument.json, at their defaults");

            tracks.setMute (kt, true);    // only the SFZ track sounds from here on
            oneShortNote (st);
            engine.beginUndoStep ("Release");
            effects.setSoundControl (st, 2, 0.0f);
            pumpMessages();
            const auto shortRelease = engine.renderPeak (0.6, 0.85);
            engine.beginUndoStep ("Release");
            effects.setSoundControl (st, 2, 0.9f);
            pumpMessages();
            const auto longRelease = engine.renderPeak (0.6, 0.85);
            std::cout << "  (after the note: release 0 " << shortRelease << ", release 0.9 " << longRelease << ")\n";
            check (longRelease > shortRelease * 3.0f, "a cc knob drives the SFZ file's controller");

            // Everything is saved with the session.
            auto knobSession = dir.getChildFile ("Knobs");
            check (engine.saveSessionAs (knobSession), "(save)");
            check (engine.openSession (AudioEngine::getSessionFileFor (knobSession)), "(reopen)");
            pumpMessages();
            kt = tracks.getTracks()[0].id;
            st = tracks.getTracks()[1].id;
            knobs = effects.getSoundControls (kt);
            sfzKnobs = effects.getSoundControls (st);
            check (knobs.size() == 5 && knobs[room].value == 1.0f && knobs[pitch].valueText == "+6"
                     && sfzKnobs.size() == 5 && std::abs (sfzKnobs[2].value - 0.9f) < 0.001f,
                   "sampled instruments' knobs survive save and reopen");

            for (int i = 0; i < 100 && ! effects.isInstrumentReady (st); ++i)
                juce::MessageManager::getInstance()->runDispatchLoopUntil (50);

            check (engine.renderPeak (0.6, 0.85) > shortRelease * 3.0f, "and still sound the same");

            // A new sound takes its own knobs, and the old hidden plugins go.
            check (effects.setSound (kt, "sampler:ikaros-studio.basics/Electronic Kit"), "(a kit instead)");
            pumpMessages();
            knobs = effects.getSoundControls (kt);
            check (knobs.size() == 4 && knobs[0].name == "Tone" && knobs[3].name == "Room"
                     && std::abs (knobs[3].value - 0.1f) < 0.001f, "a new sound starts from its own knob defaults");

            check (effects.setSound (kt, SoundLibrary::defaultSoundId), "(back to the synth)");
            pumpMessages();
            check (effects.getSoundControls (kt).size() == 5 && effects.getSoundControls (kt)[0].name == "Brightness",
                   "the synth keeps its own knobs");
        }

        // Optional: IKAROS_CHECK_PACKS=<folder of packs> plays every
        // instrument in them (e.g. tools/packs/build_packs.py's build folder).
        if (auto packsToCheck = juce::SystemStats::getEnvironmentVariable ("IKAROS_CHECK_PACKS", {}); packsToCheck.isNotEmpty())
        {
            std::cout << "Checking the packs in " << packsToCheck << "\n";
            auto& sounds = engine.getSounds();
            sounds.addPackFolder (juce::File (packsToCheck));

            for (auto& sound : std::vector<SoundInfo> (sounds.getSounds()))
            {
                auto pack = sounds.findPack (sound.packId);

                if (! pack.has_value() || ! pack->folder.isAChildOf (juce::File (packsToCheck)))
                    continue;

                engine.newSession();
                pumpMessages();
                auto t = tracks.getTracks().front().id;
                const bool loaded = tracks.effects().setSound (t, sound.id);

                for (int i = 0; i < 600 && ! tracks.effects().isInstrumentReady (t); ++i)
                    juce::MessageManager::getInstance()->runDispatchLoopUntil (50);

                // A kick and snare for kits, middle C (and a low and high note) otherwise.
                auto r = tracks.regions().createChordRegion (t, 0.0);
                pumpMessages();
                std::vector<NoteId> chord;

                for (auto& n : tracks.regions().getRegion (r)->notes)
                    chord.push_back (n.id);

                tracks.notes().deleteNotes (r, chord);

                for (int note : sound.category == "Drums" ? std::vector<int> { 36, 38, 42 } : std::vector<int> { 36, 60, 84 })
                    tracks.notes().addNote (r, { note, 0.0, 2.0, 100 });

                pumpMessages();
                const auto peak = engine.renderPeak (0.0, 2.0);
                std::cout << "  " << sound.name << " (" << pack->name << "): peak " << peak
                          << " (" << juce::Decibels::toString (juce::Decibels::gainToDecibels (peak)) << "), "
                          << tracks.effects().getSoundControls (t).size() << " knobs\n";
                check (loaded && peak > 0.01f && peak < 1.5f, "  plays at a sensible level");
            }
        }

        std::cout << "Downloadable sound packs\n";
        {
            auto& store = engine.getPackStore();
            auto& sounds = engine.getSounds();
            auto& effects = tracks.effects();
            using Status = PackStore::IndexStatus;
            using State = StorePack::State;

            // Packs install into a folder of the test's own, never the user's Sounds.
            auto storeFolder = dir.getChildFile ("Store Sounds");
            store.setInstallFolder (storeFolder);
            sounds.addPackFolder (storeFolder);

            const juce::String packId ("ikaros-studio.store-test");
            const juce::String storeSound ("sampler:ikaros-studio.store-test/Store Keys");
            const juce::String storeIndex = juce::String (IKAROS_TEST_STORE) + "/index.json";

            auto waitForStore = [&store]
            {
                for (int i = 0; i < 250 && (store.getIndexStatus() == Status::loading || store.isBusy()); ++i)
                    juce::MessageManager::getInstance()->runDispatchLoopUntil (20);

                pumpMessages();
            };

            auto useIndex = [&] (const juce::String& url)
            {
                store.setIndexUrl (url);
                store.refreshIndex();
                waitForStore();
            };

            auto leftovers = [&storeFolder]
            {
                return storeFolder.findChildFiles (juce::File::findFilesAndDirectories, false, "*").size();
            };

            useIndex (dir.getChildFile ("Nowhere").getChildFile ("index.json").getFullPathName());
            auto basics = store.findPack ("ikaros-studio.basics");
            check (store.getIndexStatus() == Status::unavailable && basics && basics->bundled && basics->state == State::installed,
                   "without the index (offline), the installed packs are still listed");

            useIndex (storeIndex);
            auto offered = store.findPack (packId);
            check (store.getIndexStatus() == Status::loaded && offered && offered->state == State::notInstalled
                     && offered->downloadSize > 0 && offered->category == "Keyboards" && offered->name == "Store Pack",
                   "the index lists a pack to download, with its size");

            useIndex (juce::String (IKAROS_TEST_STORE) + "/index-damaged.json");
            store.install (packId);
            waitForStore();
            check (! sounds.findPack (packId).has_value() && store.findPack (packId)->error.isNotEmpty() && leftovers() == 0,
                   "a download that fails its SHA-256 check is refused and leaves nothing behind");

            useIndex (storeIndex);
            store.install (packId);
            check (store.findPack (packId)->state == State::downloading, "(downloading)");
            waitForStore();
            auto installed = store.findPack (packId);
            check (sounds.findPack (packId).has_value() && installed && installed->state == State::installed
                     && installed->installedVersion == "1.0.0" && installed->error.isEmpty()
                     && sounds.findSound (storeSound).has_value() && leftovers() == 1,
                   "a pack downloads, installs and joins the library");

            // A session that uses it.
            engine.newSession();
            pumpMessages();
            auto pt = tracks.getTracks().front().id;
            check (effects.setSound (pt, storeSound), "(a track plays the downloaded instrument)");
            tracks.regions().createChordRegion (pt, 0.0);
            pumpMessages();
            check (engine.renderPeak (0.0, 1.0) > 0.05f, "a downloaded instrument plays");

            // An update: the index has a newer version than the one installed.
            auto packJson = sounds.findPack (packId)->folder.getChildFile ("pack.json");
            packJson.replaceWithText (packJson.loadFileAsString().replace ("\"1.0.0\"", "\"0.9.0\""));
            sounds.rescan();
            check (store.findPack (packId)->state == State::updateAvailable, "a newer version in the index shows as an update");
            store.install (packId);
            waitForStore();
            check (store.findPack (packId)->state == State::installed && store.findPack (packId)->installedVersion == "1.0.0"
                     && leftovers() == 1 && engine.renderPeak (0.0, 1.0) > 0.05f,
                   "updating replaces the old version, and its tracks keep playing");

            // Removing it: the track waits for it, like a missing plug-in.
            check (! store.remove ("ikaros-studio.basics"), "the pack that comes with the app can't be removed");
            check (store.remove (packId), "(remove the downloaded pack)");
            pumpMessages();
            auto slot = effects.getInstrument (pt);
            check (! sounds.findPack (packId).has_value() && leftovers() == 0 && store.findPack (packId)->state == State::notInstalled
                     && slot && slot->missing && slot->missingPackId == packId && slot->missingPackName == "Store Pack"
                     && effects.getSoundControls (pt).empty(),
                   "removing a pack leaves its tracks showing it as not installed");

            // Saved and reopened without the pack, then saved again: the
            // reference is kept, so it works once the pack is back.
            auto missingSession = dir.getChildFile ("Missing Pack");
            check (engine.saveSessionAs (missingSession), "(save)");
            check (engine.openSession (AudioEngine::getSessionFileFor (missingSession)), "(reopen)");
            pumpMessages();
            check (engine.saveSessionAs (dir.getChildFile ("Missing Pack Again")), "(save again)");
            check (engine.openSession (AudioEngine::getSessionFileFor (dir.getChildFile ("Missing Pack Again"))), "(reopen)");
            pumpMessages();
            pt = tracks.getTracks().front().id;
            slot = effects.getInstrument (pt);
            check (effects.getSoundId (pt) == storeSound && slot && slot->missing && slot->missingPackId == packId,
                   "a session keeps its missing pack's sound through save and reopen");

            store.install (packId);
            waitForStore();
            pumpMessages();
            slot = effects.getInstrument (pt);
            check (slot && ! slot->missing && effects.getSoundControls (pt).size() == 5 && engine.renderPeak (0.0, 1.0) > 0.05f,
                   "installing the pack brings the open session's track back");

            // The starter collection: both test packs, installed with one click.
            auto starter = store.findCollection (PackStore::starterCollectionId);
            check (starter && starter->name == "Starter Sounds" && starter->packIds.size() == 2
                     && starter->packsToInstall == 1 && starter->downloadSize > 0 && ! starter->allInstalled,
                   "the index's starter collection lists its packs, and what's left to download");
            store.installCollection (PackStore::starterCollectionId);
            waitForStore();
            starter = store.findCollection (PackStore::starterCollectionId);
            check (starter && starter->allInstalled && starter->packsToInstall == 0
                     && sounds.findPack ("ikaros-studio.store-bass").has_value(),
                   "installing a collection installs the packs it's missing");

            // A Nota sample registry's index: the author's SFZ library, wrapped in a pack when it's installed.
            useIndex (juce::String (IKAROS_TEST_STORE) + "/nota-index.json");
            const juce::String notaId ("nota.nota-test");
            auto nota = store.findPack (notaId);
            check (nota && nota->category == "Brass" && nota->name == "Nota Test" && nota->state == State::notInstalled
                     && ! store.findPack ("nota.nota-loops").has_value(),
                   "a Nota registry index lists its SFZ packs, filed by their tags, and leaves out loop packs");
            store.install (notaId);
            waitForStore();
            const juce::String notaSound ("sampler:nota.nota-test/Nota Test");
            check (store.findPack (notaId)->state == State::installed && store.findPack (notaId)->error.isEmpty()
                     && sounds.findSound (notaSound).has_value() && sounds.findSound (notaSound)->category == "Brass",
                   "a Nota archive installs as a pack with one instrument for its main SFZ file");
            engine.newSession();
            pumpMessages();
            pt = tracks.getTracks().front().id;
            check (effects.setSound (pt, notaSound), "(a track plays the Nota instrument)");
            tracks.regions().createChordRegion (pt, 0.0);
            pumpMessages();
            check (engine.renderPeak (0.0, 1.0) > 0.05f, "an instrument installed from a Nota registry plays");

            // A pack of loose samples (no SFZ file) becomes a kit, one sample per key from C2.
            const juce::String kitId ("nota.nota-kit"), kitSound ("sampler:nota.nota-kit/Nota Kit");
            store.install (kitId);
            waitForStore();
            check (store.findPack (kitId)->state == State::installed && sounds.findSound (kitSound).has_value(),
                   "a Nota pack with no SFZ file installs as a kit");
            engine.newSession();
            pumpMessages();
            pt = tracks.getTracks().front().id;
            check (effects.setSound (pt, kitSound), "(a track plays the kit)");

            for (int i = 0; i < 100 && ! effects.isInstrumentReady (pt); ++i)
                juce::MessageManager::getInstance()->runDispatchLoopUntil (50);

            auto kitKeys = effects.getInstrumentKeyRange (pt);
            check (kitKeys && kitKeys->first == 36 && kitKeys->second == 37, "an instrument reports the keys it plays");
            auto kitRegion = tracks.regions().createChordRegion (pt, 0.0);
            pumpMessages();
            std::vector<NoteId> kitNotes;

            for (auto& n : tracks.regions().getRegion (kitRegion)->notes)
                kitNotes.push_back (n.id);

            tracks.notes().deleteNotes (kitRegion, kitNotes);
            tracks.notes().addNote (kitRegion, { 37, 0.0, 1.0, 100 });   // the second folder's sample
            pumpMessages();
            check (engine.renderPeak (0.0, 1.0) > 0.05f, "the kit plays its samples on the keys from C2");

            engine.setAppSetting ("smokeTestSetting", true);
            check ((bool) engine.getAppSetting ("smokeTestSetting", false) && ! (bool) engine.getAppSetting ("noSuchSetting", false),
                   "app settings are kept");
        }
    }

    if (runs ("main"))
    {
        // A fresh engine: Tracktion expects only one at a time.
        AudioEngine reopened { testSettings };
        check (reopened.openSession (dir.getChildFile ("Elsewhere").getChildFile ("Other.iksession")), "reopen");
        pumpMessages();
        int withWaveforms = 0;

        for (auto& t : reopened.getTracks().getTracks())
            for (auto& r : reopened.getTracks().regions().getRegions (t.id))
                if (! r.isMidi && ! r.peaks.empty())
                    ++withWaveforms;

        check (withWaveforms == 4, "imported files and recordings load from the session's own Audio folder");
        check (reopened.getKeyName() == "Eb min", "the key round-trips");
        check (std::abs (reopened.getTracks().getTrack (reopened.getTracks().getTracks().front().id)->pan + 0.5f) < 0.01f,
               "pan round-trips");
        check (std::abs (reopened.getTracks().getMasterVolume() - 0.3f) < 0.01f, "master volume round-trips");
        auto first = reopened.getTracks().getTracks().front();
        check (std::abs (first.reverbSend - 0.5f) < 0.01f && std::abs (first.delaySend - 0.25f) < 0.01f, "sends round-trip");
    }

    if (runs ("main"))
    {
        std::cout << "Automation\n";
        auto automationSession = AudioEngine::getSessionFileFor (dir.getChildFile ("Automation"));
        const std::vector<AutomationPoint> ramp { { 4.0, 0.2f }, { 8.0, 0.9f } };

        auto samePoints = [] (const std::vector<AutomationPoint>& a, const std::vector<AutomationPoint>& b)
        {
            if (a.size() != b.size())
                return false;

            for (size_t i = 0; i < a.size(); ++i)
                if (std::abs (a[i].beat - b[i].beat) > 1.0e-6 || std::abs (a[i].value - b[i].value) > 1.0e-3f)
                    return false;

            return true;
        };

        {
            AudioEngine engine { testSettings };
            juce::MessageManager::getInstance()->runDispatchLoopUntil (2500);
            auto& tracks = engine.getTracks();
            auto& automation = tracks.automation();
            auto t = tracks.getTracks().front().id;

            auto targets = automation.getTargets (t);
            auto hasTarget = [&] (const juce::String& id)
            {
                return std::any_of (targets.begin(), targets.end(), [&] (const AutomationTarget& a) { return a.id == id; });
            };
            check (hasTarget (automationTargets::volume) && hasTarget (automationTargets::pan)
                     && hasTarget (automationTargets::send (SendBuses::reverb)) && hasTarget (automationTargets::send (SendBuses::delay))
                     && hasTarget (automationTargets::soundControl (0)) && hasTarget (automationTargets::soundControl (4)),
                   "volume, pan, both sends and the Sound knobs can be automated");

            const bool couldUndo = engine.canUndo();
            automation.setLaneShown (t, true);
            pumpMessages();
            check (tracks.getTrack (t)->automationShown && tracks.getTrack (t)->automationTarget == automationTargets::volume,
                   "a track's lane can be shown, and starts on volume");
            check (engine.canUndo() == couldUndo, "(showing it isn't an undo step)");

            engine.beginUndoStep ("Add Automation Point");
            check (automation.setPoints (t, automationTargets::volume, ramp), "points can be added");
            pumpMessages();
            check (samePoints (automation.getPoints (t, automationTargets::volume), ramp), "and read back in beats");
            check (tracks.getTrack (t)->automated.contains (automationTargets::volume), "the track says its volume is automated");

            // A drag: many small moves, one undo step.
            engine.beginUndoStep ("Move Automation Points");

            for (int i = 1; i <= 10; ++i)
                automation.setPoints (t, automationTargets::volume, { { 4.0 + i * 0.25, 0.2f + i * 0.02f }, { 8.0 + i * 0.25, 0.9f } });

            pumpMessages();
            check (samePoints (automation.getPoints (t, automationTargets::volume), { { 6.5, 0.4f }, { 10.5, 0.9f } }), "points can be moved");
            check (engine.getUndoDescription() == "Move Automation Points", "(the move is a named undo step)");
            engine.undo();
            pumpMessages();
            check (samePoints (automation.getPoints (t, automationTargets::volume), ramp), "undo puts a whole drag back in one step");
            engine.undo();
            pumpMessages();
            check (automation.getPoints (t, automationTargets::volume).empty() && ! tracks.getTrack (t)->automated.contains (automationTargets::volume),
                   "undo removes added points");
            engine.redo();
            pumpMessages();
            check (samePoints (automation.getPoints (t, automationTargets::volume), ramp), "redo adds them back");

            // Points are sorted, and kept within 0..1 and after the start.
            engine.beginUndoStep ("Pan Automation");
            automation.setPoints (t, automationTargets::pan, { { 6.0, 1.5f }, { -2.0, 0.25f } });
            check (samePoints (automation.getPoints (t, automationTargets::pan), { { 0.0, 0.25f }, { 6.0, 1.0f } }), "points are sorted and kept in range");
            check (automation.describeValue (t, automationTargets::pan, 0.25f) == "L 50", "pan values read like the pan knob");

            // A send and a Sound knob.
            const auto reverb = automationTargets::send (SendBuses::reverb);
            automation.setPoints (t, reverb, { { 0.0, 0.5f } });
            automation.setPoints (t, automationTargets::soundControl (0), { { 0.0, 0.3f }, { 4.0, 0.7f } });
            pumpMessages();
            check (automation.getPoints (t, reverb).size() == 1, "a send can be automated (it's made if needed)");
            check (tracks.getTrack (t)->automated.contains (automationTargets::soundControl (0)), "and a Sound knob");

            // Automation belongs to the track, not its regions.
            auto region = tracks.regions().createChordRegion (t, 0.0);
            pumpMessages();
            engine.beginUndoStep ("Move Region");
            tracks.regions().moveRegions ({ { region, t, 8.0 } });
            pumpMessages();
            check (samePoints (automation.getPoints (t, automationTargets::volume), ramp), "moving a region leaves the automation where it is");

            // It plays: silence where the volume curve is at the bottom.
            engine.beginUndoStep ("Volume Automation");
            automation.setPoints (t, automationTargets::volume, { { 0.0, 0.0f }, { 16.0, 0.0f } });
            const float silent = engine.renderPeak (8.0, 12.0);
            automation.setPoints (t, automationTargets::volume, { { 0.0, 0.8f }, { 16.0, 0.8f } });
            const float loud = engine.renderPeak (8.0, 12.0);
            std::cout << "  (peak with volume automation at the bottom " << silent << ", at 0.8 " << loud << ")\n";
            check (silent < 0.001f && loud > 0.01f, "the volume follows its automation when the song plays");
            automation.setPoints (t, automationTargets::volume, ramp);

            // Tempo: Tracktion stores points in seconds and moves them with the tempo.
            engine.setBpm (60.0);
            pumpMessages();
            check (samePoints (automation.getPoints (t, automationTargets::volume), ramp), "a tempo change keeps points on their beats");
            engine.undo();
            pumpMessages();
            check (samePoints (automation.getPoints (t, automationTargets::volume), ramp) && engine.getTransportState().bpm == 120.0,
                   "and so does undoing it");
            engine.setBpm (90.0);

            // Show all: every track's lane opens, on a setting that has automation.
            auto other = tracks.addInstrumentTrack();
            engine.beginUndoStep ("Pan Automation");
            automation.setPoints (other, automationTargets::pan, { { 0.0, 0.2f } });
            automation.setLaneShown (t, false);
            automation.setAllLanesShown (true);
            pumpMessages();
            check (automation.areAllLanesShown() && automation.getLaneTarget (other) == automationTargets::pan
                     && automation.getLaneTarget (t) == automationTargets::volume,
                   "showing all automation opens every lane on what's automated");
            automation.setAllLanesShown (false);
            check (! automation.isLaneShown (t) && ! automation.isLaneShown (other), "and hides them all again");
            automation.setLaneShown (t, true);

            automation.setLaneTarget (t, automationTargets::pan);
            check (engine.saveSessionAs (automationSession), "(save)");
        }

        {
            AudioEngine engine { testSettings };
            check (engine.openSession (automationSession), "(reopen)");
            pumpMessages();
            auto& automation = engine.getTracks().automation();
            auto info = engine.getTracks().getTracks().front();
            check (samePoints (automation.getPoints (info.id, automationTargets::volume), ramp), "automation round-trips (at a new tempo)");
            check (automation.getPoints (info.id, automationTargets::soundControl (0)).size() == 2, "Sound knob automation round-trips");
            check (info.automationShown && info.automationTarget == automationTargets::pan, "so does the lane");
        }
    }

    if (runs ("export"))
    {
        std::cout << "Export\n";
        AudioEngine engine { testSettings };
        juce::MessageManager::getInstance()->runDispatchLoopUntil (2500);
        auto& tracks = engine.getTracks();
        auto& exporter = engine.getExporter();
        auto exportDir = dir.getChildFile ("Export");
        constexpr double rate = SongExporter::sampleRate;

        // Runs an export to the end (pumping the message loop like the app would).
        auto runExport = [&] (ExportSettings s)
        {
            ExportResult result;
            bool done = false;

            if (exporter.start (s, [&] (const ExportResult& r) { result = r; done = true; }).failed())
                return result;

            for (int i = 0; i < 1200 && ! done; ++i)
                juce::MessageManager::getInstance()->runDispatchLoopUntil (50);

            return result;
        };

        juce::AudioFormatManager formats;
        formats.registerBasicFormats();

        auto readAudio = [&] (const juce::File& f)
        {
            juce::AudioBuffer<float> audio;

            if (std::unique_ptr<juce::AudioFormatReader> reader { formats.createReaderFor (f) })
            {
                audio.setSize ((int) reader->numChannels, (int) reader->lengthInSamples);
                reader->read (&audio, 0, audio.getNumSamples(), 0, true, true);
            }

            return audio;
        };

        auto peakIn = [&] (const juce::AudioBuffer<float>& a, double fromSeconds, double toSeconds)
        {
            const auto from = juce::jlimit (0, a.getNumSamples(), (int) (fromSeconds * rate));
            const auto to = juce::jlimit (0, a.getNumSamples(), (int) (toSeconds * rate));
            return to > from ? a.getMagnitude (from, to - from) : 0.0f;
        };

        auto seconds = [&] (const juce::AudioBuffer<float>& a) { return a.getNumSamples() / rate; };

        ExportSettings settings;
        settings.format = ExportFormat::wav;
        settings.quality = 24;
        settings.destination = exportDir.getChildFile ("Song");

        check (exporter.start (settings, {}).failed() && ! exporter.isRunning(), "a song without regions can't be exported");

        // At 120 bpm a beat is half a second: the region plays from 4 s to 12 s.
        auto t = tracks.getTracks().front().id;
        tracks.regions().createChordRegion (t, 8.0);
        pumpMessages();
        check (std::abs (exporter.getSongEndBeats() - 24.0) < 1.0e-6, "the song ends where the last region ends");

        auto result = runExport (settings);
        const auto songFile = exportDir.getChildFile ("Song.wav");
        auto song = readAudio (songFile);
        std::cout << "  (song: " << seconds (song) << " s, peak " << peakIn (song, 0.0, 60.0) << ")\n";
        check (result.ok && result.files.size() == 1 && result.files[0] == songFile && song.getNumChannels() == 2,
               "the song exports to a stereo WAV (the extension is added)");
        check (seconds (song) >= 12.0 && seconds (song) < 12.0 + SongExporter::maxTailSeconds,
               "it lasts to the end of the last region, plus a short tail");
        check (peakIn (song, 0.0, 3.95) < 1.0e-4 && peakIn (song, 4.2, 11.8) > 0.01f, "silence before the region, sound during it");
        check (exportDir.getNumberOfChildFiles (juce::File::findFilesAndDirectories) == 1, "(nothing else is left next to it)");

        // A reverb send makes the tail longer.
        engine.beginUndoStep ("Reverb");
        tracks.sends().setSendLevel (t, SendBuses::reverb, 1.0f);
        pumpMessages();
        settings.destination = exportDir.getChildFile ("Reverb.wav");
        runExport (settings);
        auto reverb = readAudio (settings.destination);
        std::cout << "  (with reverb: " << seconds (reverb) << " s)\n";
        check (seconds (reverb) > seconds (song) + 0.5 && seconds (reverb) <= 12.0 + SongExporter::maxTailSeconds + 0.2,
               "the tail lasts until the reverb dies out");

        // Normalizing: the loudest moment lands just under full scale.
        settings.normalize = true;
        settings.destination = exportDir.getChildFile ("Normalized.wav");
        runExport (settings);
        const auto normalizedPeak = peakIn (readAudio (settings.destination), 0.0, 60.0);
        std::cout << "  (normalized peak " << normalizedPeak << ")\n";
        check (std::abs (normalizedPeak - juce::Decibels::decibelsToGain (SongExporter::normalizeLevelDb)) < 0.01f,
               "normalize brings the peak to -1 dB");
        settings.normalize = false;

        // Automation: the volume drops to nothing for beats 12-16 (6-8 s).
        // (Dry, so the reverb doesn't ring on into the gap.)
        tracks.sends().setSendLevel (t, SendBuses::reverb, 0.0f);
        engine.beginUndoStep ("Volume Automation");
        tracks.automation().setPoints (t, automationTargets::volume,
                                       { { 0.0, 0.8f }, { 11.99, 0.8f }, { 12.0, 0.0f }, { 15.99, 0.0f }, { 16.0, 0.8f } });
        pumpMessages();
        settings.destination = exportDir.getChildFile ("Automation.wav");
        runExport (settings);
        auto automated = readAudio (settings.destination);
        std::cout << "  (automated peaks: " << peakIn (automated, 4.2, 5.9) << ", " << peakIn (automated, 6.1, 7.9)
                  << ", " << peakIn (automated, 8.2, 11.8) << ")\n";
        check (peakIn (automated, 6.1, 7.9) < 1.0e-3f && peakIn (automated, 4.2, 5.9) > 0.01f && peakIn (automated, 8.2, 11.8) > 0.01f,
               "the volume follows its automation in the file");
        tracks.automation().setPoints (t, automationTargets::volume, {});
        pumpMessages();

        // The cycle range, exactly (no tail).
        engine.setLoopRangeBeats (8.0, 12.0);
        settings.cycleOnly = true;
        settings.destination = exportDir.getChildFile ("Cycle.wav");
        runExport (settings);
        auto cycle = readAudio (settings.destination);
        check (std::abs (cycle.getNumSamples() - 2.0 * rate) <= 2.0 && peakIn (cycle, 0.0, 2.0) > 0.01f,
               "just the cycle range exports exactly");
        settings.cycleOnly = false;

        // Stems: a second track plays 0-8 s, without reverb.
        tracks.sends().setSendLevel (t, SendBuses::reverb, 1.0f);
        auto t2 = tracks.addInstrumentTrack();
        tracks.regions().createChordRegion (t2, 0.0);
        pumpMessages();
        settings.destination = exportDir.getChildFile ("Mix.wav");
        runExport (settings);
        auto mix = readAudio (settings.destination);

        settings.stems = true;
        settings.destination = exportDir.getChildFile ("Stems");
        result = runExport (settings);
        check (result.ok && result.files.size() == 2 && exporter.getStemNames().size() == 2, "stems: one file per track");

        if (result.files.size() == 2)
        {
            auto a = readAudio (result.files[0]), b = readAudio (result.files[1]);
            // (The synth isn't sample-for-sample repeatable, so the song's own
            // render may find the end of the reverb tail a little earlier or later.)
            check (result.files[0].getFileName().startsWith ("01 ") && a.getNumSamples() == b.getNumSamples()
                     && std::abs (seconds (a) - seconds (mix)) < 0.5,
                   "all the same length, the song's");
            check (peakIn (a, 0.0, 3.95) < 1.0e-4 && peakIn (b, 0.0, 3.95) > 0.01f, "each holds only its own track");

            // Each stem has its own reverb, and only its own. (The synth isn't
            // sample-for-sample repeatable, so the stems can't be compared
            // with the song directly.)
            std::cout << "  (after 12.5 s: reverb stem " << peakIn (a, 12.5, 60.0) << ", dry stem " << peakIn (b, 9.0, 60.0) << ")\n";
            check (peakIn (a, 12.5, 60.0) > 1.0e-3f && peakIn (b, 9.0, 60.0) < 1.0e-4f, "each stem has its own reverb, and only its own");
        }

        // Mute and solo, like playback.
        tracks.setMute (t2, true);
        settings.stems = false;
        settings.destination = exportDir.getChildFile ("Muted.wav");
        runExport (settings);
        check (peakIn (readAudio (settings.destination), 0.0, 3.95) < 1.0e-4 && exporter.getStemNames().size() == 1,
               "a muted track isn't in the song or the stems");
        tracks.setMute (t2, false);
        tracks.setSolo (t2, true);
        settings.destination = exportDir.getChildFile ("Solo.wav");
        runExport (settings);
        auto solo = readAudio (settings.destination);
        std::cout << "  (solo peaks: " << peakIn (solo, 0.0, 3.95) << ", " << peakIn (solo, 9.0, 60.0) << "; stems: "
                  << exporter.getStemNames().joinIntoString (", ") << ")\n";
        check (peakIn (solo, 0.0, 3.95) > 0.01f && peakIn (solo, 9.0, 60.0) < 1.0e-4f
                 && exporter.getStemNames() == juce::StringArray (tracks.getTrack (t2)->name),
               "solo leaves only the soloed track");
        tracks.setSolo (t2, false);

        // FLAC and OGG.
        settings.format = ExportFormat::flac;
        settings.quality = 16;
        settings.destination = exportDir.getChildFile ("Song.flac");
        runExport (settings);
        auto flac = readAudio (settings.destination);
        std::cout << "  (FLAC " << seconds (flac) << " s, song " << seconds (mix) << " s)\n";
        check (std::abs (seconds (flac) - seconds (mix)) < 0.5 && peakIn (flac, 4.2, 11.8) > 0.01f, "FLAC (16-bit) exports");

        settings.format = ExportFormat::ogg;
        settings.quality = 192;
        settings.destination = exportDir.getChildFile ("Song.ogg");
        runExport (settings);
        auto ogg = readAudio (settings.destination);
        check (std::abs (seconds (ogg) - seconds (mix)) < 0.1 && peakIn (ogg, 4.2, 11.8) > 0.01f, "OGG Vorbis exports");

        settings.format = ExportFormat::mp3;
        settings.quality = 320;
        settings.destination = exportDir.getChildFile ("Song");
        runExport (settings);
        auto mp3 = readAudio (exportDir.getChildFile ("Song.mp3"));
        std::cout << "  (MP3 " << seconds (mp3) << " s, " << exportDir.getChildFile ("Song.mp3").getSize() / 1024 << " KB)\n";
        check (std::abs (seconds (mp3) - seconds (mix)) < 0.5 && peakIn (mp3, 4.2, 11.8) > 0.01f,
               "MP3 exports (with LAME)");

        // Cancel: nothing is left behind, and the song still plays.
        settings.format = ExportFormat::wav;
        settings.destination = exportDir.getChildFile ("Cancelled.wav");
        bool cancelled = false;
        check (exporter.start (settings, [&] (const ExportResult& r) { cancelled = r.cancelled; }).wasOk(), "(start)");
        exporter.cancel();

        for (int i = 0; i < 200 && exporter.isRunning(); ++i)
            juce::MessageManager::getInstance()->runDispatchLoopUntil (50);

        check (cancelled && ! settings.destination.exists() && ! exporter.isRunning(), "cancel stops the export and leaves no file");
        engine.togglePlay();
        juce::MessageManager::getInstance()->runDispatchLoopUntil (300);
        check (engine.getTransportState().playing, "the song still plays after exporting");
        engine.togglePlay();
    }

    if (runs ("main") && testPlugins.getChildFile ("Good").isDirectory())
    {
        std::cout << "Third-party plug-ins\n";
        auto pluginSession = AudioEngine::getSessionFileFor (dir.getChildFile ("Plugins"));
        juce::String gainName ("Ikaros Test Gain");
        juce::String gainTarget;   // the gain parameter's automation target
        const std::vector<AutomationPoint> gainPoints { { 2.0, 0.25f }, { 6.0, 0.75f } };

        auto sameGainPoints = [&] (const std::vector<AutomationPoint>& p)
        {
            return p.size() == 2 && std::abs (p[0].beat - 2.0) < 1.0e-6 && std::abs (p[1].beat - 6.0) < 1.0e-6
                    && std::abs (p[0].value - 0.25f) < 1.0e-3f && std::abs (p[1].value - 0.75f) < 1.0e-3f;
        };

        {
            AudioEngine engine { testSettings };
            auto& plugins = engine.getPlugins();
            auto& tracks = engine.getTracks();
            pumpMessages();

            // Start from nothing known (Tracktion remembers scans in its settings).
            plugins.retrySkippedPlugins();
            plugins.cancelScan();
            waitForPluginScan (engine);

            plugins.scanFolders ({ testPlugins.getChildFile ("Good/VST3").getFullPathName(),
                                   testPlugins.getChildFile ("Good/LV2").getFullPathName(),
                                   testPlugins.getChildFile ("Broken/VST3").getFullPathName() });
            check (plugins.getScanStatus().scanning, "a scan runs in the background");
            waitForPluginScan (engine);
            auto status = plugins.getScanStatus();
            std::cout << "  (checked " << status.done << " of " << status.total << ", found " << status.numFound << ")\n";

            auto findNamed = [] (const std::vector<PluginInfo>& list, const juce::String& name, const juce::String& format)
            {
                for (auto& p : list)
                    if (p.name == name && p.format == format)
                        return std::optional<PluginInfo> (p);

                return std::optional<PluginInfo>();
            };

            auto synth = findNamed (plugins.getInstruments(), "Ikaros Test Synth", "VST3");
            auto gain = findNamed (plugins.getEffects(), gainName, "VST3");
            check (synth.has_value() && gain.has_value(), "VST3 instruments and effects are found");
            check (findNamed (plugins.getInstruments(), "Ikaros Test Synth", "LV2").has_value()
                     && findNamed (plugins.getEffects(), gainName, "LV2").has_value(), "LV2 ones too");
            if (synth)
            {
                plugins.setPluginEnabled (synth->id, false);
                auto installed = plugins.getInstalledPlugins();
                auto off = std::find_if (installed.begin(), installed.end(), [&] (const PluginInfo& p) { return p.id == synth->id; });
                check (off != installed.end() && off->disabled, "a turned-off plug-in stays in the installed list");
                check (! findNamed (plugins.getInstruments(), "Ikaros Test Synth", "VST3").has_value()
                         && ! plugins.findPlugin (synth->id).has_value(),
                       "but is left out of the lists and can't be picked");
                plugins.setPluginEnabled (synth->id, true);
                check (findNamed (plugins.getInstruments(), "Ikaros Test Synth", "VST3").has_value()
                         && plugins.findPlugin (synth->id).has_value(),
                       "and comes back when turned on");
            }

            check (status.failed.contains ("Ikaros Crash Test") && status.failed.contains ("Ikaros Hang Test"),
                   "a plug-in that crashes or hangs fails without taking the app down");
            check (plugins.getSkippedPlugins().size() == 2, "and is skipped from then on");

            plugins.scanFolders ({ testPlugins.getChildFile ("Good/VST3").getFullPathName(),
                                   testPlugins.getChildFile ("Good/LV2").getFullPathName(),
                                   testPlugins.getChildFile ("Broken/VST3").getFullPathName() });
            waitForPluginScan (engine);
            check (plugins.getScanStatus().total == 0, "a second scan skips what it already knows");

            if (synth && gain)
            {
                auto t1 = tracks.getTracks().front().id;
                auto& effects = tracks.effects();
                auto available = effects.getAvailableEffects();
                check (std::any_of (available.begin(), available.end(), [&] (const EffectType& e) { return e.type == "plugin:" + gain->id; }),
                       "plug-in effects are offered with the built-in ones");

                auto added = effects.addEffect (t1, "plugin:" + gain->id);
                pumpMessages();
                auto chain = effects.getEffectChain (t1);
                check (added != 0 && chain.size() == 1 && chain[0].isThirdParty && ! chain[0].missing && chain[0].name == gainName,
                       "a plug-in effect can be added");
                check (effects.getEffectParameters (t1, added).empty(), "(it has its own window instead of knobs)");

                auto gainTargets = tracks.automation().getTargets (t1);
                auto gainParam = std::find_if (gainTargets.begin(), gainTargets.end(),
                                               [&] (const AutomationTarget& a) { return a.group == gainName && a.name == "Gain"; });
                check (gainParam != gainTargets.end(), "a plug-in's parameters can be automated");

                if (gainParam != gainTargets.end())
                {
                    gainTarget = gainParam->id;
                    engine.beginUndoStep ("Gain Automation");
                    tracks.automation().setPoints (t1, gainTarget, { { 2.0, 0.25f }, { 6.0, 0.75f } });
                }

                check (effects.setSound (t1, "plugin:" + synth->id), "a plug-in instrument can be picked");
                pumpMessages();
                auto instrument = effects.getInstrument (t1);
                check (instrument && instrument->isThirdParty && instrument->name == "Ikaros Test Synth", "and becomes the instrument");
                check (tracks.getTrack (t1)->name == "Ikaros Test Synth", "(the track takes its name)");
                check (engine.saveSessionAs (pluginSession), "save with plug-ins");
            }
        }

        {
            AudioEngine engine { testSettings };
            check (engine.openSession (pluginSession), "reopen with plug-ins");
            pumpMessages();
            auto t1 = engine.getTracks().getTracks().front().id;
            auto chain = engine.getTracks().effects().getEffectChain (t1);
            auto instrument = engine.getTracks().effects().getInstrument (t1);
            check (chain.size() == 1 && chain[0].isThirdParty && ! chain[0].missing, "the plug-in effect comes back");
            check (instrument && instrument->isThirdParty && ! instrument->missing, "and the plug-in instrument");
            check (sameGainPoints (engine.getTracks().automation().getPoints (t1, gainTarget)), "and the plug-in's automation");
        }

        {
            // Turning plug-ins off: the open session keeps them until it's opened again.
            AudioEngine engine { testSettings };
            check (engine.openSession (pluginSession), "reopen to turn plug-ins off");
            pumpMessages();
            auto& plugins = engine.getPlugins();
            auto t1 = engine.getTracks().getTracks().front().id;
            auto trackName = engine.getTracks().getTrack (t1)->name;

            juce::String synthId, gainId;

            for (auto& p : plugins.getInstalledPlugins())
            {
                if (p.format != "VST3") continue;
                if (p.name == "Ikaros Test Synth") synthId = p.id;
                if (p.name == gainName) gainId = p.id;
            }

            check (synthId.isNotEmpty() && gainId.isNotEmpty(), "(found the test plug-ins)");
            check (plugins.getTracksUsing (synthId) == juce::StringArray (trackName)
                     && plugins.getTracksUsing (gainId) == juce::StringArray (trackName),
                   "the tracks using a plug-in are listed");

            plugins.setPluginEnabled (synthId, false);
            plugins.setPluginEnabled (gainId, false);
            pumpMessages();
            auto chain = engine.getTracks().effects().getEffectChain (t1);
            auto instrument = engine.getTracks().effects().getInstrument (t1);
            check (chain.size() == 1 && ! chain[0].missing && instrument && ! instrument->missing,
                   "turning plug-ins off leaves the open session as it was");
        }

        {
            AudioEngine engine { testSettings };
            check (engine.openSession (pluginSession), "reopen with plug-ins turned off");
            pumpMessages();
            auto t1 = engine.getTracks().getTracks().front().id;
            auto chain = engine.getTracks().effects().getEffectChain (t1);
            auto instrument = engine.getTracks().effects().getInstrument (t1);
            check (chain.size() == 1 && chain[0].isThirdParty && chain[0].missing && chain[0].disabledByUser
                     && instrument && instrument->missing && instrument->disabledByUser,
                   "the next time, they show as disabled");
            check (sameGainPoints (engine.getTracks().automation().getPoints (t1, gainTarget)),
                   "and the plug-in's automation is kept");
            check (engine.saveSessionAs (pluginSession), "a session with disabled plug-ins can be saved");

            for (auto& p : engine.getPlugins().getInstalledPlugins())
                if (p.disabled)
                    engine.getPlugins().setPluginEnabled (p.id, true);

            pumpMessages();
            chain = engine.getTracks().effects().getEffectChain (t1);
            instrument = engine.getTracks().effects().getInstrument (t1);
            check (chain.size() == 1 && ! chain[0].missing && ! chain[0].disabledByUser
                     && instrument && ! instrument->missing,
                   "turning them back on loads them again at once");
            check (sameGainPoints (engine.getTracks().automation().getPoints (t1, gainTarget)),
                   "with the automation intact");
        }

        // Pretend the gain plug-in was uninstalled: rename it in the saved
        // session (unpacked from its bundle, then packed again).
        auto unpacked = dir.getChildFile ("PluginsUnpacked");
        SessionBundle::unpack (pluginSession, unpacked);
        auto unpackedSession = unpacked.getChildFile (SessionBundle::sessionEntry);

        if (auto xml = juce::parseXML (unpackedSession))
        {
            for (auto* plugin : xml->getChildByName ("TRACK")->getChildIterator())
            {
                if (plugin->hasTagName ("PLUGIN") && plugin->getStringAttribute ("name") == gainName)
                {
                    plugin->setAttribute ("name", "Ikaros Gone Gain");
                    plugin->setAttribute ("filename", "/nowhere/Ikaros Gone Gain.vst3");
                    plugin->setAttribute ("uniqueId", "0");
                    plugin->setAttribute ("uid", "0");
                }
            }

            xml->writeTo (unpackedSession);
            SessionBundle::pack (pluginSession, { { SessionBundle::sessionEntry, unpackedSession, true } }, {});
        }

        {
            AudioEngine engine { testSettings };
            check (engine.openSession (pluginSession), "a session with a missing plug-in still opens");
            pumpMessages();
            auto t1 = engine.getTracks().getTracks().front().id;
            auto chain = engine.getTracks().effects().getEffectChain (t1);
            check (chain.size() == 1 && chain[0].missing && chain[0].name == "Ikaros Gone Gain",
                   "the missing plug-in shows as a placeholder with its name");
            check (engine.getTracks().effects().getInstrument (t1).has_value(), "the rest of the track is fine");
            check (engine.saveSession(), "it can be saved");
            check (SessionBundle::readEntry (pluginSession, SessionBundle::sessionEntry).toString().contains ("Ikaros Gone Gain"),
                   "and keeps the missing plug-in's settings");

            auto& automation = engine.getTracks().automation();
            auto target = automation.getTarget (t1, gainTarget);
            check (target && ! target->editable && target->name == "Gain" && sameGainPoints (automation.getPoints (t1, gainTarget)),
                   "the missing plug-in's automation is kept, read-only");
            check (! automation.setPoints (t1, gainTarget, {}), "(and can't be changed)");
            engine.setBpm (60.0);
            pumpMessages();
            check (sameGainPoints (automation.getPoints (t1, gainTarget)), "a tempo change keeps it on its beats");
            check (engine.saveSession(), "(save again)");
        }

        {
            AudioEngine engine { testSettings };
            check (engine.openSession (pluginSession), "(reopen again)");
            pumpMessages();
            auto t1 = engine.getTracks().getTracks().front().id;
            check (sameGainPoints (engine.getTracks().automation().getPoints (t1, gainTarget)),
                   "the missing plug-in's automation survives saving again");
        }
    }
    else if (runs ("main"))
    {
        std::cout << "(Third-party plug-in checks skipped: build with -DIKAROS_BUILD_TEST_PLUGINS=ON)\n";
    }

    if (runs ("undo"))
    {
        // Every edit the app can make, through the same calls the UI uses:
        // one named undo step each, and undo/redo/save/reopen give back
        // exactly the same session.
        AudioEngine engine { testSettings };
        auto& tracks = engine.getTracks();
        auto& regions = tracks.regions();
        auto& notes = tracks.notes();
        auto& effects = tracks.effects();
        const auto undoSession = dir.getChildFile ("Undo");
        engine.newSession();
        pumpMessages();

        auto t1 = tracks.getTracks().front().id;
        TrackId t2 = 0, audio = 0;

        std::cout << "-- Tracks\n";
        checkUndoRoundTrip (engine, "Add Track", [&] { t2 = tracks.addInstrumentTrack(); });
        checkUndoRoundTrip (engine, "Add Audio Track", [&] { audio = tracks.addAudioTrack(); });
        checkUndoRoundTrip (engine, "Rename Track", [&] { tracks.renameTrack (t1, "Lead"); });
        checkUndoRoundTrip (engine, "Track Icon", [&] { tracks.setTrackIcon (t1, "guitar"); });
        checkUndoRoundTrip (engine, "Move Track Down", [&] { tracks.moveTrack (t1, 1); });
        TrackId copy = 0;
        checkUndoRoundTrip (engine, "New Track with Duplicate Settings", [&] { copy = tracks.duplicateTrack (t2, false); });
        checkUndoRoundTrip (engine, "Delete Track", [&] { tracks.deleteTrack (copy); });
        checkSaveAndReopen (engine, undoSession);

        std::cout << "-- Mixer and inputs\n";
        checkUndoRoundTrip (engine, "Volume", [&] { engine.beginUndoStep ("Volume"); tracks.setVolume (t1, 0.3f); });
        checkUndoRoundTrip (engine, "Pan", [&] { engine.beginUndoStep ("Pan"); tracks.setPan (t1, -0.5f); });
        checkUndoRoundTrip (engine, "Master Volume", [&] { engine.beginUndoStep ("Master Volume"); tracks.setMasterVolume (0.4f); });
        checkUndoRoundTrip (engine, "Reverb Send", [&] { engine.beginUndoStep ("Reverb Send"); tracks.sends().setSendLevel (t1, 0, 0.6f); });
        checkUndoRoundTrip (engine, "Monitoring On", [&] { tracks.inputs().setMonitoring (audio, true); });
        checkSaveAndReopen (engine, undoSession);

        std::cout << "-- Regions\n";
        RegionId r = 0;
        checkUndoRoundTrip (engine, "Add Region", [&] { r = regions.createChordRegion (t1, 0.0); });
        checkUndoRoundTrip (engine, "Move Region", [&] { regions.moveRegions ({ { r, t1, 8.0 } }); });
        checkUndoRoundTrip (engine, "Trim Region", [&] { regions.trimRegion (r, 8.0, 20.0); });
        checkUndoRoundTrip (engine, "Loop Region", [&] { regions.loopRegion (r, 32.0); });
        checkUndoRoundTrip (engine, "Split Region", [&] { regions.splitRegions ({ r }, 12.0); });
        checkUndoRoundTrip (engine, "Duplicate Region", [&] { regions.duplicateRegions ({ r }); });
        checkUndoRoundTrip (engine, "Rename Region", [&] { regions.renameRegion (r, "Verse"); });
        regions.copyRegions ({ r });
        checkUndoRoundTrip (engine, "Paste", [&] { regions.pasteRegions (t2, 64.0); });
        checkUndoRoundTrip (engine, "Delete Region", [&] { regions.deleteRegion (regions.getRegions (t2).front().id); });

        const auto wavFile = dir.getChildFile ("Undo Tone.wav");
        {
            juce::WavAudioFormat wav;
            std::unique_ptr<juce::OutputStream> stream (wavFile.createOutputStream().release());
            auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (44100.0)
                                                                                        .withNumChannels (1)
                                                                                        .withBitsPerSample (16));
            juce::AudioBuffer<float> buffer (1, 44100);

            for (int i = 0; i < buffer.getNumSamples(); ++i)
                buffer.setSample (0, i, 0.5f * std::sin ((float) i * 0.05f));

            check (writer != nullptr && writer->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples()), "(test audio written)");
        }
        checkUndoRoundTrip (engine, "Import Audio File", [&] { regions.importAudioFiles (audio, { wavFile.getFullPathName() }, 0.0); });

        std::cout << "-- Fades\n";
        {
            // At 120 bpm the one-second tone is 2 beats long, and the
            // automatic edge fade (minFadeSeconds, 5 ms) is 0.01 beats.
            const auto a = regions.getRegions (audio).front();
            const double edgeFade = 0.005 * 2.0;
            auto near = [] (double x, double y) { return std::abs (x - y) < 1.0e-4; };
            check (near (a.fadeInBeats, edgeFade) && near (a.fadeOutBeats, edgeFade),
                   "an imported audio region gets very short fades at its edges, so it can't click");

            checkUndoRoundTrip (engine, "Fade In", [&] { regions.setFade (a.id, true, 0.5); });
            checkUndoRoundTrip (engine, "Fade Out", [&] { regions.setFade (a.id, false, 1.0); });
            auto faded = regions.getRegion (a.id);
            check (faded && near (faded->fadeInBeats, 0.5) && near (faded->fadeOutBeats, 1.0), "fades can be made longer");

            pumpMessages();
            regions.setFade (a.id, true, 2.0);
            pumpMessages();
            faded = regions.getRegion (a.id);
            check (faded && near (faded->fadeInBeats, 1.0), "a fade in stops where the fade out starts");
            engine.undo();
            pumpMessages();

            regions.setFade (a.id, true, 0.0);
            pumpMessages();
            faded = regions.getRegion (a.id);
            check (faded && near (faded->fadeInBeats, edgeFade), "a fade can't go below the automatic edge fade");
            engine.undo();
            pumpMessages();

            const auto right = regions.splitRegions ({ a.id }, 0.75);   // leaves room for both fades on each half
            pumpMessages();
            auto leftHalf = regions.getRegion (a.id);
            auto rightHalf = right.empty() ? std::nullopt : regions.getRegion (right.front());
            check (leftHalf && rightHalf && near (leftHalf->fadeOutBeats, edgeFade) && near (rightHalf->fadeInBeats, edgeFade)
                     && near (leftHalf->fadeInBeats, 0.5) && near (rightHalf->fadeOutBeats, 1.0),
                   "splitting keeps the outer fades and gives the cut short fades");
            engine.undo();
            pumpMessages();
        }
        checkSaveAndReopen (engine, undoSession);

        std::cout << "-- Audio region editor\n";
        {
            // A one-second tone that swells up from silence, so its two ends
            // look different. At 120 bpm it's 2 beats long.
            const auto rampFile = dir.getChildFile ("Undo Ramp.wav");
            {
                juce::WavAudioFormat wav;
                std::unique_ptr<juce::OutputStream> stream (rampFile.createOutputStream().release());
                auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (44100.0)
                                                                                            .withNumChannels (1)
                                                                                            .withBitsPerSample (16));
                juce::AudioBuffer<float> buffer (1, 44100);

                for (int i = 0; i < buffer.getNumSamples(); ++i)
                    buffer.setSample (0, i, 0.8f * (float) i / 44100.0f * std::sin ((float) i * 0.05f));

                check (writer != nullptr && writer->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples()), "(test audio written)");
            }

            const auto added = regions.importAudioFiles (audio, { rampFile.getFullPathName() }, 8.0);
            pumpMessages();
            const RegionId ramp = added.empty() ? 0 : added.front();

            // The editor's waveform is read on a background thread: ask until it's there.
            auto waveform = [&] (RegionId id, double from, double to, int columns)
            {
                std::vector<WaveformRange> result;

                for (int tries = 0; tries < 300 && ! regions.getWaveform (id, from, to, columns, result); ++tries)
                    juce::Thread::sleep (10);

                return result;
            };

            auto loudness = [] (const std::vector<WaveformRange>& w, size_t i) { return i < w.size() ? w[i].max - w[i].min : -1.0f; };

            const auto forwards = waveform (ramp, 8.0, 10.0, 8);
            check (forwards.size() == 8 && loudness (forwards, 0) < loudness (forwards, 7) * 0.3f,
                   "the audio editor's waveform shows the recording (here, swelling up)");
            const auto past = waveform (ramp, 10.0, 12.0, 4);
            check (past.size() == 4 && loudness (past, 1) == 0.0f, "past the end of the recording it's silent");

            checkUndoRoundTrip (engine, "Region Gain", [&] { engine.beginUndoStep ("Region Gain"); regions.setRegionGain (ramp, -6.0f); });
            checkUndoRoundTrip (engine, "Reverse Region", [&] { regions.setRegionReversed (ramp, true); });
            checkUndoRoundTrip (engine, "Transpose Region", [&] { regions.setRegionTranspose (ramp, 3); });

            auto info = regions.getRegion (ramp);
            check (info && std::abs (info->gainDb + 6.0f) < 0.01f && info->reversed && info->transposeSemitones == 3,
                   "the region keeps its gain, direction and pitch");
            check (info && info->peaks.size() > 2 && info->peaks.front() > info->peaks.back(),
                   "reversed, its waveform in the arrangement fades away instead");

            const auto backwards = waveform (ramp, 8.0, 10.0, 8);
            check (backwards.size() == 8 && loudness (backwards, 7) < loudness (backwards, 0) * 0.3f,
                   "and so does the editor's");
            check (std::abs (loudness (backwards, 0) - loudness (forwards, 7) * 0.5f) < 0.05f,
                   "the waveform shows the region's gain (-6 dB is about half)");

            regions.setRegionTranspose (ramp, 99);
            pumpMessages();
            check (regions.getRegion (ramp)->transposeSemitones == RegionEditor::maxTransposeSemitones, "transpose stops at two octaves");
            engine.undo();
            pumpMessages();

            const auto halves = regions.splitRegions ({ ramp }, 9.0);
            pumpMessages();
            const auto right = halves.empty() ? std::nullopt : regions.getRegion (halves.front());
            check (right && right->reversed && right->transposeSemitones == 3 && std::abs (right->gainDb + 6.0f) < 0.01f,
                   "both halves of a split keep them");
            engine.undo();
            pumpMessages();

            // A loop that follows the session key keeps doing so, on top of the transpose.
            for (auto& loop : engine.getSounds().getLoops())
            {
                if (loop.name != "Glass Chords")
                    continue;

                const auto loopAdded = regions.importAudioFiles (audio, { loop.file.getFullPathName() }, 16.0);
                pumpMessages();
                const RegionId chords = loopAdded.empty() ? 0 : loopAdded.front();
                check (chords != 0, "(loop placed)");

                if (chords == 0)
                    break;

                const auto shiftBefore = regions.getRegion (chords)->keyShiftSemitones;

                checkUndoRoundTrip (engine, "Transpose Region", [&] { regions.setRegionTranspose (chords, -12); });
                auto loopInfo = regions.getRegion (chords);
                check (loopInfo && loopInfo->followsKey && loopInfo->transposeSemitones == -12 && loopInfo->keyShiftSemitones == shiftBefore,
                       "a transposed loop still follows the key");

                engine.setKey (4, false);   // E major: C major loops go up 4
                pumpMessages();
                loopInfo = regions.getRegion (chords);
                check (loopInfo && loopInfo->transposeSemitones == -12 && loopInfo->keyShiftSemitones == 4,
                       "and changing the key keeps its transpose");
                engine.undo();
                pumpMessages();
            }

            checkSaveAndReopen (engine, undoSession);
            info = regions.getRegion (ramp);
            check (info && std::abs (info->gainDb + 6.0f) < 0.01f && info->reversed && info->transposeSemitones == 3,
                   "the region's gain, direction and pitch are saved");
        }

        std::cout << "-- Notes\n";
        checkUndoRoundTrip (engine, "Add Note", [&] { notes.addNote (r, { 72, 0.5, 0.25, 90 }); });
        checkUndoRoundTrip (engine, "Move Note", [&]
        {
            auto moved = regions.getRegion (r)->notes.front();
            moved.startBeat += 0.5;
            moved.pitch += 2;
            notes.changeNotes (r, { moved }, "Move Note");
        });
        checkUndoRoundTrip (engine, "Quantize", [&]
        {
            std::vector<NoteId> ids;

            for (auto& n : regions.getRegion (r)->notes)
                ids.push_back (n.id);

            notes.quantiseNotes (r, ids, QuantizeSettings { 1.0 / 4.0 });
        });
        checkUndoRoundTrip (engine, "Delete Note", [&] { notes.deleteNotes (r, { regions.getRegion (r)->notes.front().id }); });
        checkSaveAndReopen (engine, undoSession);

        std::cout << "-- Sounds, effects and EQ\n";
        PluginId fx = 0, fx2 = 0;
        checkUndoRoundTrip (engine, "Change Sound", [&] { effects.setSound (t1, "synth:Warm Pad"); });
        checkUndoRoundTrip (engine, "Tone", [&] { engine.beginUndoStep ("Tone"); effects.setSoundControl (t1, 0, 0.8f); });
        checkUndoRoundTrip (engine, "Add Effect", [&] { fx = effects.addEffect (t1, "reverb"); });
        checkUndoRoundTrip (engine, "Add Effect", [&] { fx2 = effects.addEffect (t1, "compressor"); });
        checkUndoRoundTrip (engine, "Replace Effect", [&] { fx2 = effects.replaceEffect (t1, fx2, "delay"); });
        checkUndoRoundTrip (engine, "Move Effect", [&] { effects.moveEffect (t1, fx2, 0); });
        checkUndoRoundTrip (engine, "Turn Effect Off", [&] { effects.setEffectEnabled (t1, fx, false); });
        checkUndoRoundTrip (engine, "Reverb Mix", [&] { engine.beginUndoStep ("Reverb Mix"); effects.setEffectParameter (t1, fx, 0, 0.9f); });
        checkUndoRoundTrip (engine, "Remove Effect", [&] { effects.removeEffect (t1, fx2); });
        checkUndoRoundTrip (engine, "Add Effect", [&] { fx2 = effects.addEffect (t1, "ikarosDistortion"); });
        checkUndoRoundTrip (engine, "Drive", [&] { engine.beginUndoStep ("Drive"); effects.setEffectParameter (t1, fx2, 0, 0.9f); });
        checkUndoRoundTrip (engine, "Add Effect", [&] { effects.addEffect (t1, "ikarosTremolo"); });
        checkUndoRoundTrip (engine, "Add Effect", [&] { effects.addEffect (t1, "ikarosLimiter"); });
        checkUndoRoundTrip (engine, "Turn Noise Gate On", [&] { effects.setNoiseGateOn (t1, true); });
        checkUndoRoundTrip (engine, "Noise Gate Threshold", [&]
        {
            engine.beginUndoStep ("Noise Gate Threshold");
            effects.setNoiseGateThreshold (t1, 0.7f);
        });
        checkUndoRoundTrip (engine, "Turn Noise Gate Off", [&] { effects.setNoiseGateOn (t1, false); });
        checkUndoRoundTrip (engine, "EQ", [&] { engine.beginUndoStep ("EQ"); tracks.eq().setBand (t1, 1, 800.0f, 6.0f, 1.0f); });
        checkUndoRoundTrip (engine, "Turn EQ Off", [&] { tracks.eq().setEnabled (t1, false); });
        checkUndoRoundTrip (engine, "Reset EQ", [&] { tracks.eq().reset (t1); });
        checkSaveAndReopen (engine, undoSession);

        std::cout << "-- Automation, tempo and key\n";
        checkUndoRoundTrip (engine, "Add Automation Point", [&]
        {
            engine.beginUndoStep ("Add Automation Point");
            tracks.automation().setPoints (t1, automationTargets::volume, { { 4.0, 0.2f }, { 8.0, 0.9f } });
        });
        engine.undo();
        pumpMessages();
        check (std::abs (tracks.getTrack (t1)->volume - 0.3f) < 0.01f,
               "undoing the automation puts the fader back where it was (not where the curve left it)");
        engine.redo();
        checkUndoRoundTrip (engine, "Tempo", [&] { engine.setBpm (96.0); });
        checkUndoRoundTrip (engine, "Time Signature", [&] { engine.setTimeSignature (3, 4); });

        std::cout << "-- Loops follow the song key\n";
        {
            int root = -1;
            bool minor = false;
            check (SoundLibrary::parseKey ("F# minor", root, minor) && root == 6 && minor
                     && SoundLibrary::parseKey ("Bbm", root, minor) && root == 10 && minor
                     && SoundLibrary::parseKey ("c", root, minor) && root == 0 && ! minor
                     && SoundLibrary::parseKey ("Eb maj", root, minor) && root == 3 && ! minor
                     && ! SoundLibrary::parseKey ("H", root, minor) && ! SoundLibrary::parseKey ("C dorian", root, minor),
                   "loop keys are read the ways packs and Freesound write them");

            auto findLoop = [&] (const juce::String& name) -> std::optional<LibraryFile>
            {
                for (auto& l : engine.getSounds().getLoops())
                    if (l.name == name)
                        return l;

                return std::nullopt;
            };

            const auto chords = findLoop ("Glass Chords"), bass = findLoop ("Pluck Bass"), beat = findLoop ("Electro Beat");
            check (chords && chords->keyRoot == 0 && ! chords->keyMinor && bass && bass->keyRoot == 0 && bass->keyMinor
                     && beat && beat->unpitched && beat->keyRoot < 0,
                   "the bundled loops know their keys, and the drum loops are unpitched");
            check (SoundLibrary::soundsUnpitched ({ "funk", "drums" }) && ! SoundLibrary::soundsUnpitched ({ "bass" }),
                   "loops tagged as drums count as unpitched");

            // The region on the audio track that starts at this beat.
            auto regionAt = [&] (double startBeat) -> std::optional<RegionInfo>
            {
                for (auto& r : regions.getRegions (audio))
                    if (std::abs (r.startBeat - startBeat) < 0.001)
                        return r;

                return std::nullopt;
            };

            auto shiftAt = [&] (double startBeat)
            {
                auto r = regionAt (startBeat);
                return r && r->followsKey ? r->keyShiftSemitones : 99;
            };

            engine.setKey (2, false);   // D major
            pumpMessages();

            if (chords && bass && beat)
            {
                checkUndoRoundTrip (engine, "Import Audio File", [&] { regions.importAudioFiles (audio, { chords->file.getFullPathName() }, 32.0); });
                regions.importAudioFiles (audio, { bass->file.getFullPathName() }, 48.0);
                regions.importAudioFiles (audio, { beat->file.getFullPathName() }, 64.0);
                pumpMessages();
            }

            check (shiftAt (32.0) == 2, "a C major loop placed in D major goes up 2 semitones");
            check (shiftAt (48.0) == -1, "a C minor loop goes to D major's relative minor, B minor (down 1)");
            check (regionAt (64.0) && ! regionAt (64.0)->followsKey && regionAt (64.0)->keyShiftSemitones == 0,
                   "a drum loop isn't moved");

            checkUndoRoundTrip (engine, "Key", [&] { engine.setKey (7, false); });   // G major
            check (shiftAt (32.0) == -5 && shiftAt (48.0) == 4,
                   "changing the key moves the loops already placed, whichever way is nearer (C to G is down 5)");

            engine.setKey (9, true);   // A minor, C major's relative minor
            pumpMessages();
            check (shiftAt (32.0) == 0 && shiftAt (48.0) == -3, "loops keep their mode: C major stays put in A minor, C minor goes to A minor");
            engine.undo();
            pumpMessages();
            check (shiftAt (32.0) == -5 && shiftAt (48.0) == 4, "undo puts them back");

            if (auto chordsRegion = regionAt (32.0))
            {
                regions.splitRegions ({ chordsRegion->id }, 34.0);
                pumpMessages();
                check (shiftAt (32.0) == -5 && shiftAt (34.0) == -5, "both halves of a split loop follow the key");
                engine.undo();
                pumpMessages();

                regions.copyRegions ({ chordsRegion->id });
                regions.pasteRegions (audio, 96.0);
                pumpMessages();
                check (shiftAt (96.0) == -5, "so does a pasted copy");
                engine.setKey (4, false);   // E major
                pumpMessages();
                check (shiftAt (96.0) == 4 && shiftAt (32.0) == 4, "and it keeps following the key");
                engine.undo();
                pumpMessages();
                engine.undo();
                pumpMessages();
            }
        }

        checkUndoRoundTrip (engine, "Key", [&] { engine.setKey (7, true); });
        checkSaveAndReopen (engine, undoSession);
        {
            auto r = regions.getRegions (audio);
            auto chords = std::find_if (r.begin(), r.end(), [] (const RegionInfo& i) { return std::abs (i.startBeat - 32.0) < 0.001; });
            check (chords != r.end() && chords->followsKey && chords->keyShiftSemitones == -2,
                   "a reopened session's loops still follow the key (C major in G minor = Bb major, down 2)");
        }

        std::cout << "-- Selection after delete, undo and redo\n";
        {
            engine.newSession();
            pumpMessages();
            Selection selection { engine };

            // The same as the Undo and Redo commands do (see MainComponent::perform).
            auto undoOrRedo = [&] (bool undo)
            {
                const auto before = selection.getContents();
                undo ? engine.undo() : engine.redo();
                selection.selectAfterUndoOrRedo (before);
                pumpMessages();
            };

            tracks.addInstrumentTrack();
            pumpMessages();
            const auto lastAdded = tracks.addInstrumentTrack();
            pumpMessages();
            std::vector<TrackId> order;

            for (auto& t : tracks.getTracks())
                order.push_back (t.id);

            selection.selectTrack (order[1]);
            tracks.deleteTrack (order[1]);
            pumpMessages();
            check (selection.getTrack() == order[2], "deleting a track selects the one that takes its place");
            tracks.deleteTrack (order[2]);
            pumpMessages();
            check (selection.getTrack() == order[0], "deleting the bottom track selects the one above");

            undoOrRedo (true);
            check (selection.getTrack() == order[2], "undoing a delete selects the track that comes back");
            undoOrRedo (true);
            check (selection.getTrack() == order[1], "(and again)");

            // Undoing "Add Track" takes the selected track away.
            selection.selectTrack (lastAdded);
            const auto at = std::find (order.begin(), order.end(), lastAdded) - order.begin();
            const auto expected = at + 1 < (long) order.size() ? order[(size_t) at + 1] : order[(size_t) at - 1];
            undoOrRedo (true);
            check (selection.getTrack() == expected, "undoing Add Track selects its neighbour, not the first track");
            undoOrRedo (false);
            check (selection.getTrack() == lastAdded, "redoing it selects the new track again");

            auto& regions = tracks.regions();
            const auto r1 = regions.createChordRegion (order[0], 0.0);
            pumpMessages();
            const auto r2 = regions.createChordRegion (order[0], 32.0);
            pumpMessages();
            selection.selectRegions (order[0], { r1, r2 });
            regions.deleteRegions ({ r1, r2 });
            pumpMessages();
            selection.validate();
            check (! selection.hasRegions(), "deleted regions leave the selection");
            undoOrRedo (true);
            check (selection.isSelected (r1) && selection.isSelected (r2) && selection.getTrack() == order[0],
                   "undoing a delete selects the regions that come back");

            selection.selectTrack (order[1]);
            undoOrRedo (false);   // deletes them again
            undoOrRedo (true);
            check (selection.isSelected (r1) && selection.isSelected (r2), "(also when another track was selected)");
            undoOrRedo (true);    // the second "Add Region"
            check (selection.getRegions() == std::vector<RegionId> { r1 } && selection.getRegion() == r1,
                   "undoing Add Region drops just that region from the selection");
        }
    }

    if (runs ("recovery"))
    {
        std::cout << "Atomic saves\n";
        {
            AudioEngine engine { testSettings };
            juce::MessageManager::getInstance()->runDispatchLoopUntil (2500);

            auto saveDir = dir.getChildFile ("Atomic");
            auto bundle = AudioEngine::getSessionFileFor (saveDir.getChildFile ("Kept"));
            check (engine.saveSessionAs (bundle), "(save)");
            check (engine.saveSession(), "(save again, over the first)");
            check (saveDir.getNumberOfChildFiles (juce::File::findFilesAndDirectories) == 1,
                   "saving leaves no temporary files behind");

            juce::MemoryBlock before, after;
            bundle.loadFileAsData (before);

            // A save that fails halfway (a file it should hold has vanished).
            std::vector<SessionBundle::Entry> entries { { SessionBundle::sessionEntry, engine.getWorkingCopyFolder().getChildFile (SessionBundle::sessionEntry), true },
                                                        { "Audio/Gone.wav", saveDir.getChildFile ("Gone.wav"), false } };
            check (SessionBundle::pack (bundle, entries, {}).failed(), "(a save that fails partway)");
            bundle.loadFileAsData (after);
            check (before == after, "a failed save leaves the saved session exactly as it was");
            check (saveDir.getNumberOfChildFiles (juce::File::findFilesAndDirectories) == 1, "and leaves no half-written file");

            std::cout << "Autosave\n";
            engine.newSession();
            engine.setAutosaveTiming (400, 1500, 5000);
            juce::MessageManager::getInstance()->runDispatchLoopUntil (3500);
            const auto work = engine.getWorkingCopyFolder();
            const auto autosave = work.getChildFile (SessionRecovery::autosaveFile);
            check (work.getChildFile (SessionRecovery::lockFile).existsAsFile(), "an open session's working copy is marked as open");
            check (! autosave.exists(), "a session with no changes isn't autosaved");

            auto& tracks = engine.getTracks();
            auto t = tracks.addInstrumentTrack();
            engine.beginUndoStep ("Rename");
            tracks.renameTrack (t, "Autosaved Track");
            juce::MessageManager::getInstance()->runDispatchLoopUntil (150);
            check (! autosave.exists(), "no autosave while changes are still coming");
            const auto undoName = engine.getUndoDescription();
            juce::MessageManager::getInstance()->runDispatchLoopUntil (700);
            engine.waitForAutosave();
            check (autosave.loadFileAsString().contains ("Autosaved Track"), "a moment after changes stop, the recovery copy is written");
            check (engine.getUndoDescription() == undoName && undoName.isNotEmpty() && ! engine.canRedo(), "autosaving doesn't touch undo");
            check (engine.hasUnsavedChanges(), "the session still has unsaved changes");

            engine.beginUndoStep ("Rename");
            tracks.renameTrack (t, "Second Name");
            juce::MessageManager::getInstance()->runDispatchLoopUntil (700);
            engine.waitForAutosave();
            check (! autosave.loadFileAsString().contains ("Second Name"), "not autosaved again until the interval has passed");
            juce::MessageManager::getInstance()->runDispatchLoopUntil (1200);
            engine.waitForAutosave();
            check (autosave.loadFileAsString().contains ("Second Name"), "then autosaved again");

            check (engine.saveSessionAs (saveDir.getChildFile ("Autosaved")), "(save)");
            check (! autosave.exists(), "saving removes the recovery copy");
            check (engine.findRecoverableSessions().empty(), "sessions open in this copy of the app aren't offered");
        }

        std::cout << "Cut-off recordings\n";
        {
            auto wavFile = dir.getChildFile ("CutOff.wav");
            const int frames = writeCutOffWav (wavFile, 1.0);
            check (frames > 0 && lengthOfWav (wavFile) <= 0, "(a WAV cut off mid-recording reads as empty)");
            check (SessionRecovery::repairWavFile (wavFile), "it can be repaired");
            check (lengthOfWav (wavFile) == frames, "and then holds every whole sample frame");

            juce::MemoryBlock good, again;
            wavFile.loadFileAsData (good);
            SessionRecovery::repairWavFile (wavFile);
            wavFile.loadFileAsData (again);
            check (good == again, "a WAV that's fine is left alone");

            juce::Array<juce::MidiMessage> played { juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100).withTimeStamp (1.25),
                                                    juce::MidiMessage::noteOff (1, 60).withTimeStamp (1.5) };
            auto journal = dir.getChildFile ("Take.txt");
            journal.replaceWithText (SessionRecovery::toJournalLines (played) + "2.0 90 4");   // cut off mid-line
            auto readBack = SessionRecovery::readJournal (journal);
            check (readBack.size() == 2 && readBack[0].isNoteOn() && readBack[0].getNoteNumber() == 60
                     && std::abs (readBack[0].getTimeStamp() - 1.25) < 1e-6 && readBack[1].isNoteOff(),
                   "MIDI written as it's played reads back, without a line cut off by the crash");
        }

        std::cout << "Crash recovery\n";
        {
            auto bundle = AudioEngine::getSessionFileFor (dir.getChildFile ("Crashy"));
            juce::ChildProcess child;
            const auto exe = juce::File::getSpecialLocation (juce::File::currentExecutableFile).getFullPathName();
            check (child.start (juce::StringArray { exe, "--recovery-child=" + bundle.getFullPathName() }),
                   "(started a copy of the app to crash)");

            // Wait for it to be mid-take. readProcessOutput blocks until it has
            // filled the buffer (or the process ends), so it reads a byte at a time.
            juce::String output;
            const auto giveUp = juce::Time::getMillisecondCounter() + 60000;
            char buffer[1];

            while (! output.contains ("RECORDING") && child.isRunning() && juce::Time::getMillisecondCounter() < giveUp)
                if (auto n = child.readProcessOutput (buffer, sizeof (buffer)); n > 0)
                    output += juce::String::fromUTF8 (buffer, n);

            // Read up to the end of the line (the audio track's ID).
            while (output.contains ("RECORDING") && ! output.fromFirstOccurrenceOf ("RECORDING", false, false).containsChar ('\n') && child.isRunning())
                if (auto n = child.readProcessOutput (buffer, sizeof (buffer)); n > 0)
                    output += juce::String::fromUTF8 (buffer, n);

            const auto status = juce::StringArray::fromTokens (output.fromFirstOccurrenceOf ("RECORDING", false, false).upToFirstOccurrenceOf ("\n", false, false).trim(), " ", {});
            check (status.size() == 2 && status[0] == "1" && ! output.contains ("no autosave"),
                   "(the other copy autosaved, and is recording)");
            if (status.size() != 2)
                std::cout << "         the other copy said:\n" << output.getLastCharacters (1500) << "\n";

            const auto audioTrack = (TrackId) status[1].getLargeIntValue();

            AudioEngine engine { testSettings };
            juce::MessageManager::getInstance()->runDispatchLoopUntil (2500);
            check (engine.findRecoverableSessions().empty(), "a session open in another copy of the app isn't offered");

            child.kill();   // SIGKILL: no chance to tidy up, like a crash

            for (int i = 0; i < 100 && child.isRunning(); ++i)
                juce::Thread::sleep (50);


            auto found = engine.findRecoverableSessions();
            check (found.size() == 1, "after the crash, its session is offered");

            if (found.size() == 1)
            {
                const auto session = found.front();
                const auto age = juce::Time::getCurrentTime() - session.savedAt;
                check (session.name == "Crashy" && ! session.untitled, "with its name");
                check (age.inSeconds() >= 0 && age.inSeconds() < 120, "and when its work was autosaved");

                const auto saved = SessionBundle::readEntry (bundle, SessionBundle::sessionEntry).toString();
                check (saved.contains ("TRACK") && ! saved.contains ("After Save"), "autosaving never wrote to the saved session");

                // An audio take cut off by the crash too. (The test can't record
                // audio, so this is what Tracktion would have left: a WAV in the
                // Audio folder, noted in Recovery.json.)
                auto info = SessionRecovery::readInfo (session.folder);
                check (info.has_value() && info->takes.size() == 1 && info->takes[0].midi, "the MIDI take was noted as it started");

                if (info.has_value() && ! info->takes.empty())
                {
                    auto audioTake = info->takes[0];
                    audioTake.midi = false;
                    audioTake.track = audioTrack;
                    audioTake.path = "Audio/Cut Off Take.wav";
                    writeCutOffWav (session.folder.getChildFile (audioTake.path), 2.0);
                    info->takes.push_back (audioTake);
                    SessionRecovery::writeInfo (session.folder, *info);
                }

                check (engine.recoverSession (session), "Recover opens it");
                juce::MessageManager::getInstance()->runDispatchLoopUntil (3500);   // past the clean-slate reset

                auto& tracks = engine.getTracks();
                check (engine.getSessionName() == "Crashy" && ! engine.isUntitled(), "as the saved session it was");
                check (engine.hasUnsavedChanges(), "marked as unsaved");

                std::optional<TrackInfo> afterSave;

                for (auto& t : tracks.getTracks())
                    if (t.name == "After Save")
                        afterSave = t;

                check (afterSave.has_value(), "with the changes made after saving");

                if (afterSave.has_value())
                {
                    auto regions = tracks.regions().getRegions (afterSave->id);
                    check (regions.size() == 2, "the chord region, and the take cut short");

                    if (regions.size() != 2)
                        for (auto& r : regions)
                            std::cout << "         region at " << r.startBeat << ", " << r.notes.size() << " notes\n";

                    if (regions.size() == 2)
                    {
                        const auto& take = regions[0].startBeat < regions[1].startBeat ? regions[0] : regions[1];
                        check (take.notes.size() == 4, "the take holds the notes played before the crash (the held one too)");
                        check (take.startBeat < 0.5 && take.lengthBeats < 16.0, "where it was recorded");
                    }
                }

                auto audioRegions = tracks.regions().getRegions (audioTrack);
                check (audioRegions.size() == 1 && audioRegions[0].lengthBeats > 3.0, "a cut-off audio take goes back on its track too");
                check (engine.getWorkingCopyFolder() == session.folder, "the recordings stay in its working copy");
                check (engine.findRecoverableSessions().empty(), "a recovered session isn't offered again");

                // Discard: a crashed copy of it, then one with nothing unsaved.
                engine.waitForAutosave();
                auto copy = session.folder.getSiblingFile ("Discard Me");
                session.folder.copyDirectoryTo (copy);
                auto offered = engine.findRecoverableSessions();
                check (offered.size() == 1 && offered[0].folder == copy, "(another crashed session)");

                if (! offered.empty())
                    engine.discardRecoverableSession (offered[0]);

                check (! copy.exists(), "Discard deletes it");

                auto empty = session.folder.getSiblingFile ("Nothing Unsaved");
                empty.createDirectory();
                empty.getChildFile (SessionRecovery::lockFile).create();
                SessionRecovery::writeInfo (empty, { "Nothing", {}, {}, {} });
                check (engine.findRecoverableSessions().empty() && ! empty.exists(),
                       "a crashed session with nothing unsaved isn't offered, and is tidied away");
            }
        }

        juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile (testSettings + "Child").deleteRecursively();

        // Closing a session leaves nothing behind to recover.
        auto openSessions = platform::getLocalDataFolder().getChildFile ("Open Sessions");
        check (openSessions.getNumberOfChildFiles (juce::File::findDirectories) == 0, "closed sessions leave no working copies");
    }

    if (runs ("recording"))
        runRecordingSection (dir);

    if (runs ("takes"))
        runTakesSection (dir);

    if (runs ("longrecording"))
        runLongRecordingSection();

    if (runs ("preferences"))
        runPreferencesSection (dir);

    if (runs ("effects"))
        runEffectsSection (dir);

    if (runs ("tempo"))
        runTempoSection (dir);

    if (runs ("sections"))
        runSectionsSection (dir);

    if (runs ("views"))
        runViewsSection (dir);

    if (runs ("stretch"))
        runStretchSection (dir);

    if (runs ("flex"))
        runFlexSection (dir);

    if (runs ("quantize"))
        runQuantizeSection (dir);

    if (runs ("followtempo"))
        runFollowTempoSection (dir);

    if (runs ("tuner"))
        runTunerSection();

    if (runs ("pedals"))
        runPedalsSection (dir);

    if (runs ("amp"))
        runAmpSection (dir);

    dir.deleteRecursively();
    std::cout << (failures == 0 ? "\nAll checks passed\n" : "\nSome checks FAILED\n");
    return failures == 0 ? 0 : 1;
}
