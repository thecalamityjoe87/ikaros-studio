// Engine-layer smoke test: exercises AudioEngine + TrackController without any UI.
// Build with -DIKAROS_BUILD_TESTS=ON, then run `ctest --test-dir build` or the
// IkarosEngineSmokeTest binary directly.

#include "engine/AudioEngine.h"
#include "engine/PluginLibrary.h"
#include "engine/SoundLibrary.h"
#include "engine/TrackController.h"
#include <juce_audio_formats/juce_audio_formats.h>   // to write test WAV files
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

    // Start from fresh settings, so earlier runs (e.g. remembered plug-in
    // scans) don't change the results. Tracktion keeps them in the user's
    // app-data folder, under the name the engine is given.
    juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile (testSettings).deleteRecursively();

    // The test plug-ins (built with -DIKAROS_BUILD_TEST_PLUGINS=ON), if any.
    const auto testPlugins = args.isEmpty() ? juce::File() : juce::File (args[0]);

    auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("IkarosSmokeTest");
    dir.deleteRecursively();
    dir.createDirectory();
    auto file = AudioEngine::getSessionFileFor (dir.getChildFile ("Smoke"));   // Smoke/Smoke.tracktionedit

    {
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
        check (tracks.effects().setSound (t2, "sampler:Ikaros Basics/Soft Piano"), "a sampled sound loads");
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
            check (copy && copy->soundName == "Soft Piano" && copy->colour == tracks.getTrack (t2)->colour,
                   "with the same sound and colour");
            check (tracks.effects().getEffectChain (settingsCopy).size() == tracks.effects().getEffectChain (t2).size(), "and the same effects");
            check (tracks.regions().getRegions (settingsCopy).empty(), "but no regions");

            auto fullCopy = tracks.duplicateTrack (t2, true);
            pumpMessages();
            auto copiedRegions = tracks.regions().getRegions (fullCopy);
            check (! sourceRegions.empty() && copiedRegions.size() == sourceRegions.size()
                     && copiedRegions.front().id != sourceRegions.front().id, "duplicate track copies its regions");
            check (tracks.regions().getRegions (t2).size() == sourceRegions.size(), "and leaves the original's alone");

            tracks.renameTrack (fullCopy, "  Lead  ");
            pumpMessages();
            check (tracks.getTrack (fullCopy)->name == "Lead", "tracks can be renamed");
            tracks.renameTrack (fullCopy, "   ");
            check (tracks.getTrack (fullCopy)->name == "Lead", "a blank name is ignored");

            auto purple = TrackController::getTrackColours()[3];
            tracks.setTrackColour (fullCopy, purple);
            pumpMessages();
            check (tracks.getTrack (fullCopy)->colour == purple, "track colour can be changed");

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
            check (! recent.empty() && recent.front().preview == file.getSiblingFile ("Preview.png"),
                   "its preview lives in the session's folder");
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
        auto region = tracks.regions().getRegion (r2);
        const auto noteCount = region->notes.size();
        auto index = tracks.notes().addNote (r2, { 72, 1.1, 0.5, 90 });
        pumpMessages();
        region = tracks.regions().getRegion (r2);
        check (index >= 0 && region->notes.size() == noteCount + 1, "add a note");
        check (index >= 0 && region->notes[(size_t) index].pitch == 72 && std::abs (region->notes[(size_t) index].startBeat - 1.1) < 1e-6,
               "the note is where it was drawn");

        auto map = tracks.notes().changeNotes (r2, { { index, { 74, 2.3, 1.0, 40 } } }, "Move Note");
        pumpMessages();
        region = tracks.regions().getRegion (r2);
        check (map.size() == region->notes.size(), "a change reports where every note went");
        index = map.empty() ? -1 : map[(size_t) index];
        auto note = index >= 0 ? region->notes[(size_t) index] : NoteInfo {};
        check (note.pitch == 74 && std::abs (note.startBeat - 2.3) < 1e-6 && note.lengthBeats == 1.0 && note.velocity == 40,
               "move, resize and velocity");
        check (engine.getUndoDescription() == "Move Note", "the note edit has its own undo step");

        map = tracks.notes().quantiseNotes (r2, { index }, 0.25);
        pumpMessages();
        region = tracks.regions().getRegion (r2);
        index = map.empty() ? -1 : map[(size_t) index];
        check (index >= 0 && region->notes[(size_t) index].pitch == 74 && std::abs (region->notes[(size_t) index].startBeat - 2.25) < 1e-6,
               "quantize snaps to the grid");

        tracks.notes().deleteNotes (r2, { index });
        pumpMessages();
        check (tracks.regions().getRegion (r2)->notes.size() == noteCount, "delete a note");

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

        std::cout << "Sound library\n";
        {
            auto& sounds = engine.getSounds().getSounds();
            auto has = [&sounds] (const juce::String& id)
            {
                return std::any_of (sounds.begin(), sounds.end(), [&id] (const SoundInfo& s) { return s.id == id; });
            };

            check (has (SoundLibrary::defaultSoundId) && has ("synth:Warm Pad"), "the built-in synth patches are there");
            check (has ("sampler:Ikaros Basics/Soft Piano") && has ("sampler:Ikaros Basics/Electronic Kit"),
                   "the bundled sampled instruments are there");
            check (engine.getSounds().getLoops().size() >= 4, "and the loops");
            check (tracks.getTrack (t1)->soundId == SoundLibrary::defaultSoundId, "new tracks use the default sound");

            auto& effects = tracks.effects();
            const auto oldName = tracks.getTrack (t2)->name;
            check (oldName.startsWith ("Instrument"), "(new tracks are named Instrument N)");
            check (effects.setSound (t2, "sampler:Ikaros Basics/Electronic Kit"), "picking a sound swaps the instrument");
            pumpMessages();
            check (tracks.getTrack (t2)->name == "Electronic Kit" && tracks.getTrack (t2)->category == "Drums",
                   "a track with the app's name takes the sound's name");
            check (effects.getInstrument (t2)->name == "Sampler" && effects.getSoundControls (t2).empty(),
                   "sampled instruments have no synth knobs");
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

        // Pretend a take was recorded: a file in the recordings folder.
        auto take = AudioEngine::getRecordingsFolder().getNonexistentChildFile ("IkarosSmokeTest_Take", ".wav", false);
        AudioEngine::getRecordingsFolder().createDirectory();
        check (writeWav (take, 1.0), "(wrote a test take)");
        tracks.regions().importAudioFiles (audioTrackId, { take.getFullPathName() }, 20.0);

        auto songFile = AudioEngine::getSessionFileFor (dir.getChildFile ("Song"));
        check (songFile == dir.getChildFile ("Song").getChildFile ("Song.tracktionedit"), "a session gets a folder of its own");
        check (engine.saveSessionAs (dir.getChildFile ("Song")), "Save As");
        check (songFile.existsAsFile() && engine.getSessionFile() == songFile, "saved in that folder");
        auto songAudio = AudioEngine::getAudioFolderFor (songFile);
        check (songAudio.getChildFile (take.getFileName()).existsAsFile(), "copies recordings into the session");
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

        // A file added to a session that's already saved (Tracktion stores
        // its path differently once the session file exists).
        auto late = dir.getChildFile ("Late.wav");
        check (writeWav (late, 1.0), "(wrote another test WAV)");
        tracks.regions().importAudioFiles (audioTrackId, { late.getFullPathName() }, 40.0);
        check (engine.saveSession() && engine.saveSession(), "save twice after adding a file");
        late.deleteFile();
        check (AudioEngine::getAudioFolderFor (otherFile).getChildFile (take.getFileName()).existsAsFile(),
               "takes the session's audio along");
        check (AudioEngine::getSessionFileFor (otherFile) == otherFile, "a session already in its own folder stays there");
        dir.getChildFile ("Song").deleteRecursively();   // the new session must stand on its own
    }

    {
        // A fresh engine: Tracktion expects only one at a time.
        AudioEngine reopened { testSettings };
        check (reopened.openSession (dir.getChildFile ("Elsewhere").getChildFile ("Other").getChildFile ("Other.tracktionedit")), "reopen");
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

    if (testPlugins.getChildFile ("Good").isDirectory())
    {
        std::cout << "Third-party plug-ins\n";
        auto pluginSession = AudioEngine::getSessionFileFor (dir.getChildFile ("Plugins"));
        juce::String gainName ("Ikaros Test Gain");

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
        }

        // Pretend the gain plug-in was uninstalled: rename it in the saved session.
        if (auto xml = juce::parseXML (pluginSession))
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

            xml->writeTo (pluginSession);
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
            check (pluginSession.loadFileAsString().contains ("Ikaros Gone Gain"), "and keeps the missing plug-in's settings");
        }
    }
    else
    {
        std::cout << "(Third-party plug-in checks skipped: build with -DIKAROS_BUILD_TEST_PLUGINS=ON)\n";
    }

    dir.deleteRecursively();
    std::cout << (failures == 0 ? "\nAll checks passed\n" : "\nSome checks FAILED\n");
    return failures == 0 ? 0 : 1;
}
