// Engine-layer smoke test: exercises AudioEngine + TrackController without any UI.
// Build with -DIKAROS_BUILD_TESTS=ON, then run `ctest --test-dir build` or the
// IkarosEngineSmokeTest binary directly.

#include "engine/AudioEngine.h"
#include "engine/TrackController.h"
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
        pumpMessages();

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
        tracks.setEffectEnabled (t2, 1, true);
        check (tracks.getTrack (t2)->soundName == "Grand Piano", "preset assigns to the track");
        check (tracks.getEffects (t2)[1].enabled, "compressor can be turned on");

        pumpMessages();
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
            check (engine.getTracks().getEffects (t2.id)[1].enabled, "compressor state round-trips");
        }

        check (std::abs (engine.getTransportState().bpm - 96.0) < 0.001, "tempo round-trips");
    }

    dir.deleteRecursively();
    std::cout << (failures == 0 ? "\nAll checks passed\n" : "\nSome checks FAILED\n");
    return failures == 0 ? 0 : 1;
}
