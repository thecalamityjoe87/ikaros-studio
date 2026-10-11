// Engine-layer smoke test: exercises AudioEngine + TrackController without any UI.
// Build with -DIKAROS_BUILD_TESTS=ON, then run `ctest --test-dir build` or the
// IkarosEngineSmokeTest binary directly.

#include "engine/AudioEngine.h"
#include "engine/TrackController.h"
#include <juce_audio_formats/juce_audio_formats.h>   // to write test WAV files
#include <iostream>

using namespace ikaros;

static int failures = 0;

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

int main()
{
    // Tracktion needs a running MessageManager; this sets one up for a console app.
    juce::ScopedJuceInitialiser_GUI juceInit;

    auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("IkarosSmokeTest");
    dir.deleteRecursively();
    dir.createDirectory();
    auto file = dir.getChildFile ("Smoke.tracktionedit");

    {
        AudioEngine engine;
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

        auto r1 = tracks.createChordRegion (t2, 5.0);   // inside bar 2 → snaps to beat 4
        auto region = tracks.getRegion (r1);
        check (region.has_value(), "double-click creates a region");
        check (region && region->startBeat == 4.0 && region->lengthBeats == 16.0, "region is 4 bars, snapped to the bar");
        check (region && region->notes.size() == 4 * 7, "region has a chord pattern (7 notes per bar)");
        check (tracks.createChordRegion (t2, 6.0) == 0, "can't stack a region on top of another");

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
        std::cout << "Transport\n";
        engine.setBpm (96.0);
        engine.setLooping (true);
        auto ts = engine.getTransportState();
        check (std::abs (ts.bpm - 96.0) < 0.001, "BPM change reaches the tempo sequence");
        check (ts.looping && ts.loopEndBeats > ts.loopStartBeats, "loop turns on with a range");

        pumpMessages();
        std::cout << "Sound\n";
        tracks.setSound (t2, "Keyboards", "Grand Piano");
        check (tracks.getTrack (t2)->soundName == "Grand Piano", "preset assigns to the track");

        std::cout << "Plug-ins\n";
        check (tracks.getInstrument (t2).has_value(), "track has an instrument");
        check (tracks.getEffectChain (t2).empty(), "new tracks start with no effects");
        auto reverb = tracks.addEffect (t2, "reverb");
        auto comp = tracks.addEffect (t2, "compressor");
        auto eq = tracks.addEffect (t2, "4bandEq", 0);   // insert at the top
        auto chain = tracks.getEffectChain (t2);
        check (reverb != 0 && comp != 0 && eq != 0, "effects can be added");
        check (chain.size() == 3 && chain[0].id == eq && chain[1].id == reverb && chain[2].id == comp, "effects go where asked");
        tracks.moveEffect (t2, comp, 0);
        chain = tracks.getEffectChain (t2);
        check (chain.size() == 3 && chain[0].id == comp && chain[1].id == eq, "effects can be reordered");
        tracks.setEffectEnabled (t2, comp, false);
        check (! tracks.getEffectChain (t2)[0].enabled, "effects can be bypassed");
        auto echo = tracks.replaceEffect (t2, eq, "delay");
        chain = tracks.getEffectChain (t2);
        check (echo != 0 && chain.size() == 3 && chain[1].id == echo && chain[1].name == "Echo", "effects can be replaced in place");
        tracks.removeEffect (t2, reverb);
        check (tracks.getEffectChain (t2).size() == 2, "effects can be removed");
        auto params = tracks.getEffectParameters (t2, echo);
        check (! params.empty(), "effects expose parameters");
        engine.beginUndoStep ("Effect setting");
        tracks.setEffectParameter (t2, echo, 0, 0.25f);
        check (std::abs (tracks.getEffectParameters (t2, echo)[0].value - 0.25f) < 0.02f, "effect parameters can be set");
        pumpMessages();
        engine.undo();   // the effect setting
        pumpMessages();
        engine.undo();   // removing the reverb
        pumpMessages();
        check (tracks.getEffectChain (t2).size() == 3, "undo brings a removed effect back");

        pumpMessages();
        std::cout << "Recording\n";
        {
            auto t3 = tracks.addInstrumentTrack();
            tracks.setSelectedTrack (t3);
            engine.goToStart();
            pumpMessages();

            engine.toggleRecord();   // nothing armed: arms the selected track for this take
            pumpMessages();
            check (engine.getTransportState().recording, "Record starts recording");
            check (tracks.getArmedTrack() == t3, "the selected track is armed for the take");

            for (int note : { 60, 64, 67 })
            {
                tracks.noteOn (note, 0.8f);
                juce::MessageManager::getInstance()->runDispatchLoopUntil (250);
                tracks.noteOff (note);
                juce::MessageManager::getInstance()->runDispatchLoopUntil (100);
            }

            engine.toggleRecord();   // stop
            pumpMessages();
            pumpMessages();
            check (! engine.getTransportState().recording, "Record again stops");
            check (tracks.getArmedTrack() == 0, "the take's automatic arm is released");

            auto regions = tracks.getRegions (t3);
            check (regions.size() == 1, "the take becomes a region");
            check (! regions.empty() && regions[0].notes.size() == 3, "the region holds the played notes");

            tracks.setArmed (t3, true);
            check (tracks.getTrack (t3)->armed, "a track can be armed explicitly");
            tracks.setArmed (t3, false);
            tracks.deleteTrack (t3);
            tracks.setSelectedTrack (t2);
        }

        std::cout << "Audio tracks\n";
        {
            auto inputs = tracks.getAudioInputs();
            std::cout << "  (audio inputs: " << inputs.size() << ")\n";
            auto a1 = tracks.addAudioTrack();
            auto info = tracks.getTrack (a1);
            check (info && info->kind == TrackKind::audio, "audio tracks can be added");
            check (tracks.getSoundControls (a1).empty(), "audio tracks have no synth controls");
            check (! tracks.getInstrument (a1).has_value(), "audio tracks have no instrument");

            if (! inputs.empty())
            {
                check (tracks.getRecordingSettings (a1).inputId == inputs.front().id, "new audio tracks use the first input");
                tracks.setTrackInput (a1, inputs.back().id);
                check (tracks.getRecordingSettings (a1).inputId == inputs.back().id, "the input can be changed");
            }

            check (! tracks.getRecordingSettings (a1).monitoring, "monitoring starts off (no feedback)");
            tracks.setMonitoring (a1, true);
            check (tracks.getRecordingSettings (a1).monitoring, "monitoring can be turned on");

            tracks.setArmed (a1, true);
            check (tracks.getTrack (a1)->armed, "audio tracks can be armed");
            tracks.setArmed (a1, false);
            tracks.deleteTrack (a1);
            tracks.setSelectedTrack (t2);
        }

        std::cout << "Save\n";
        check (engine.hasUnsavedChanges(), "edits mark the session as changed");
        check (engine.saveSessionAs (file), "Save As writes the file");
        check (file.existsAsFile(), "file exists on disk");
        check (! engine.hasUnsavedChanges(), "saved state is clean");
        check (engine.getSessionName() == "Smoke", "session takes the file name");

        pumpMessages();
        std::cout << "New session again\n";
        engine.newSession();
        check (engine.getTracks().getTracks().size() == 1, "new session replaces the old one");
    }

    {
        std::cout << "Open (fresh engine)\n";
        AudioEngine engine;
        check (engine.openSession (file), "open succeeds");
        pumpMessages();

        auto list = engine.getTracks().getTracks();
        check (list.size() == 2, "both tracks come back");

        if (list.size() == 2)
        {
            auto& t2 = list[1];
            auto regions = engine.getTracks().getRegions (t2.id);
            check (t2.soundName == "Grand Piano", "sound name round-trips");
            check (t2.muted, "mute round-trips");
            check (regions.size() == 1 && regions[0].notes.size() == 28, "region and notes round-trip");
            auto chain = engine.getTracks().getEffectChain (t2.id);
            check (chain.size() == 3 && ! chain[0].enabled && chain[0].name == "Compressor", "effect chain round-trips");
        }

        check (std::abs (engine.getTransportState().bpm - 96.0) < 0.001, "tempo round-trips");
    }

    {
        AudioEngine engine;
        auto& tracks = engine.getTracks();
        pumpMessages();

        std::cout << "Region editing\n";
        // Each edit is followed by pumpMessages(): Tracktion tidies up after
        // some edits on the next message-loop turn (e.g. re-sorting clips),
        // and that belongs to the edit's own undo step.
        auto t1 = tracks.getTracks().front().id;
        auto t2 = tracks.addInstrumentTrack();
        auto r1 = tracks.createChordRegion (t1, 0.0);    // beats 0-16
        auto r2 = tracks.createChordRegion (t1, 16.0);   // beats 16-32
        pumpMessages();

        tracks.moveRegions ({ { r1, t2, 4.0 } });
        pumpMessages();
        auto moved = tracks.getRegion (r1);
        check (moved && moved->trackId == t2 && moved->startBeat == 4.0, "a region moves to another track and beat");
        engine.undo();
        pumpMessages();
        moved = tracks.getRegion (r1);
        check (moved && moved->trackId == t1 && moved->startBeat == 0.0, "undo puts it back");

        tracks.moveRegions ({ { r1, t1, 8.0 } });   // lands on half of r2
        pumpMessages();
        auto under = tracks.getRegion (r2);
        check (under && under->startBeat == 24.0 && under->lengthBeats == 8.0, "a moved region cuts the region it lands on");
        engine.undo();
        pumpMessages();
        under = tracks.getRegion (r2);
        check (under && under->startBeat == 16.0 && under->lengthBeats == 16.0, "undo restores the cut region");

        tracks.trimRegion (r1, 4.0, 12.0);
        pumpMessages();
        auto trimmed = tracks.getRegion (r1);
        check (trimmed && trimmed->startBeat == 4.0 && trimmed->lengthBeats == 8.0, "both edges trim");
        check (trimmed && ! trimmed->notes.empty() && trimmed->notes.front().startBeat == -4.0,
               "trimming the start leaves the notes where they were");
        tracks.trimRegion (r1, 0.0, 12.0);
        pumpMessages();
        trimmed = tracks.getRegion (r1);
        check (trimmed && trimmed->startBeat == 0.0 && trimmed->notes.front().startBeat == 0.0, "trimming back out reveals them");

        tracks.moveRegions ({ { r1, t2, 4.0 } });   // a MIDI region can grow past its first note
        pumpMessages();
        tracks.trimRegion (r1, 0.0, 16.0);
        pumpMessages();
        trimmed = tracks.getRegion (r1);
        check (trimmed && trimmed->startBeat == 0.0 && trimmed->notes.front().startBeat == 4.0,
               "growing a MIDI region to the left keeps the notes in place");
        engine.undo();
        pumpMessages();
        check (engine.canRedo(), "redo is still available after undoing a trim");
        engine.undo();
        pumpMessages();
        moved = tracks.getRegion (r1);
        check (moved && moved->trackId == t1 && moved->startBeat == 0.0 && moved->lengthBeats == 12.0, "two undos, two steps back");

        auto halves = tracks.splitRegions ({ r1, r2 }, 6.0);
        pumpMessages();
        auto left = tracks.getRegion (r1);
        check (halves.size() == 1, "split only cuts regions under the split point");
        check (left && left->lengthBeats == 6.0, "the left half ends at the split");
        auto right = halves.empty() ? std::nullopt : tracks.getRegion (halves.front());
        check (right && right->startBeat == 6.0 && right->lengthBeats == 6.0, "the right half starts there");
        engine.undo();
        pumpMessages();

        auto copies = tracks.duplicateRegions ({ r1, r2 });
        pumpMessages();
        check (copies.size() == 2, "duplicate copies every region");
        auto copy = copies.empty() ? std::nullopt : tracks.getRegion (copies.front());
        check (copy && copy->startBeat == 32.0 && copy->notes.size() == 28, "copies go after the last region, notes and all");
        engine.undo();
        pumpMessages();

        tracks.copyRegions ({ r1 });
        check (tracks.canPaste(), "copy fills the clipboard");
        auto pasted = tracks.pasteRegions (t2, 40.0);
        pumpMessages();
        auto paste = pasted.empty() ? std::nullopt : tracks.getRegion (pasted.front());
        check (paste && paste->trackId == t2 && paste->startBeat == 40.0, "paste goes on the chosen track and beat");
        check (pasted.size() == 1 && tracks.pasteRegions (t2, 60.0).size() == 1, "paste can repeat");
        pumpMessages();

        tracks.renameRegion (r1, "Verse");
        pumpMessages();
        check (tracks.getRegion (r1)->name == "Verse", "rename");
        tracks.deleteRegions ({ r1, r2 });
        pumpMessages();
        check (tracks.getRegions (t1).empty(), "delete removes several regions at once");
        engine.undo();
        pumpMessages();
        check (tracks.getRegions (t1).size() == 2, "...in one undo step");

        std::cout << "Notes\n";
        auto region = tracks.getRegion (r2);
        const auto noteCount = region->notes.size();
        auto index = tracks.addNote (r2, { 72, 1.1, 0.5, 90 });
        pumpMessages();
        region = tracks.getRegion (r2);
        check (index >= 0 && region->notes.size() == noteCount + 1, "add a note");
        check (index >= 0 && region->notes[(size_t) index].pitch == 72 && std::abs (region->notes[(size_t) index].startBeat - 1.1) < 1e-6,
               "the note is where it was drawn");

        auto map = tracks.changeNotes (r2, { { index, { 74, 2.3, 1.0, 40 } } }, "Move Note");
        pumpMessages();
        region = tracks.getRegion (r2);
        check (map.size() == region->notes.size(), "a change reports where every note went");
        index = map.empty() ? -1 : map[(size_t) index];
        auto note = index >= 0 ? region->notes[(size_t) index] : NoteInfo {};
        check (note.pitch == 74 && std::abs (note.startBeat - 2.3) < 1e-6 && note.lengthBeats == 1.0 && note.velocity == 40,
               "move, resize and velocity");
        check (engine.getUndoDescription() == "Move Note", "the note edit has its own undo step");

        map = tracks.quantiseNotes (r2, { index }, 0.25);
        pumpMessages();
        region = tracks.getRegion (r2);
        index = map.empty() ? -1 : map[(size_t) index];
        check (index >= 0 && region->notes[(size_t) index].pitch == 74 && std::abs (region->notes[(size_t) index].startBeat - 2.25) < 1e-6,
               "quantize snaps to the grid");

        tracks.deleteNotes (r2, { index });
        pumpMessages();
        check (tracks.getRegion (r2)->notes.size() == noteCount, "delete a note");

        std::cout << "Count-in\n";
        engine.setCountInEnabled (true);
        check (engine.getTransportState().countIn, "count-in turns on");
        engine.setCountInEnabled (false);
        check (! engine.getTransportState().countIn, "and off");

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
        check (tracks.isSupportedAudioFile (imported), "WAV files can be imported");
        check (! tracks.isSupportedAudioFile (file), "session files can't");

        const auto trackCount = tracks.getTracks().size();
        auto added = tracks.importAudioFiles (t1, { imported.getFullPathName() }, 8.0);   // t1 is an instrument track
        auto audioRegion = added.empty() ? std::nullopt : tracks.getRegion (added.front());
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
        check (tracks.importAudioFiles (audioTrackId, { imported.getFullPathName() }, 0.0).size() == 1
                 && tracks.getTracks().size() == trackCount + 1, "dropping on an audio track adds to it");
        tracks.moveRegions ({ { r2, audioTrackId, 0.0 } });
        check (tracks.getRegion (r2)->trackId == t1, "MIDI regions can't move to audio tracks");

        // Pretend a take was recorded: a file in the recordings folder.
        auto take = AudioEngine::getRecordingsFolder().getNonexistentChildFile ("IkarosSmokeTest_Take", ".wav", false);
        AudioEngine::getRecordingsFolder().createDirectory();
        check (writeWav (take, 1.0), "(wrote a test take)");
        tracks.importAudioFiles (audioTrackId, { take.getFullPathName() }, 20.0);

        auto songFile = dir.getChildFile ("Song.tracktionedit");
        check (engine.saveSessionAs (songFile), "Save As");
        auto songRecordings = AudioEngine::getRecordingsFolderFor (songFile);
        check (songRecordings.getChildFile (take.getFileName()).existsAsFile(), "copies recordings next to the session");
        check (! songRecordings.getChildFile (imported.getFileName()).exists(), "but not imported files");
        take.deleteFile();   // the session must now use its own copy

        auto moveDir = dir.getChildFile ("Elsewhere");
        moveDir.createDirectory();
        auto otherFile = moveDir.getChildFile ("Other.tracktionedit");
        check (engine.saveSessionAs (otherFile), "Save As somewhere else");
        check (AudioEngine::getRecordingsFolderFor (otherFile).getChildFile (take.getFileName()).existsAsFile(),
               "takes the session's recordings along");
        take.deleteFile();
    }

    {
        // A fresh engine: Tracktion expects only one at a time.
        AudioEngine reopened;
        check (reopened.openSession (dir.getChildFile ("Elsewhere").getChildFile ("Other.tracktionedit")), "reopen");
        pumpMessages();
        int withWaveforms = 0;

        for (auto& t : reopened.getTracks().getTracks())
            for (auto& r : reopened.getTracks().getRegions (t.id))
                if (! r.isMidi && ! r.peaks.empty())
                    ++withWaveforms;

        check (withWaveforms == 3, "imported files and recordings still load from the new location");
        check (reopened.getKeyName() == "Eb min", "the key round-trips");
        check (std::abs (reopened.getTracks().getTrack (reopened.getTracks().getTracks().front().id)->pan + 0.5f) < 0.01f,
               "pan round-trips");
        check (std::abs (reopened.getTracks().getMasterVolume() - 0.3f) < 0.01f, "master volume round-trips");
    }

    dir.deleteRecursively();
    std::cout << (failures == 0 ? "\nAll checks passed\n" : "\nSome checks FAILED\n");
    return failures == 0 ? 0 : 1;
}
