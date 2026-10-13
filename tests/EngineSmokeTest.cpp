// Engine-layer smoke test: exercises AudioEngine + TrackController without any UI.
// Build with -DIKAROS_BUILD_TESTS=ON, then run `ctest --test-dir build` or the
// IkarosEngineSmokeTest binary directly.

#include "engine/AudioEngine.h"
#include "engine/PackStore.h"
#include "engine/PluginLibrary.h"
#include "engine/SoundLibrary.h"
#include "engine/TrackController.h"
#include "engine/SessionBundle.h"
#include "engine/SongExporter.h"
#include "ui/state/Selection.h"                    // plain logic, no components
#include <juce_audio_formats/juce_audio_formats.h>   // to write test WAV files
#include <juce_graphics/juce_graphics.h>             // a test preview picture
#include <miniz.h>                                   // a deliberately broken bundle
#include <cstdlib>                                    // setenv
#include <functional>                                  // std::function
#include <map>
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
   #if JUCE_WINDOWS
    _putenv_s ("IKAROS_USER_SOUNDS_DIR", userSounds.toRawUTF8());
   #else
    setenv ("IKAROS_USER_SOUNDS_DIR", userSounds.toRawUTF8(), 1);
   #endif
    auto file = AudioEngine::getSessionFileFor (dir.getChildFile ("Smoke"));   // Smoke.iksession

    if (runs ("main"))
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

            tracks.notes().quantiseNotes (r2, { id }, 0.25);
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
            check (masterReverb != 0 && tracks.effects().getEffectChain (masterTrackId).size() == 1,
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
            check (tracks.effects().getEffectChain (masterTrackId).size() == 1, "and so do master effects");
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
        checkUndoRoundTrip (engine, "Track Color", [&] { tracks.setTrackColour (t1, juce::Colours::orange); });
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
        checkSaveAndReopen (engine, undoSession);

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

            notes.quantiseNotes (r, ids, 1.0);
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
        checkUndoRoundTrip (engine, "Key", [&] { engine.setKey (7, true); });
        checkSaveAndReopen (engine, undoSession);

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

    dir.deleteRecursively();
    std::cout << (failures == 0 ? "\nAll checks passed\n" : "\nSome checks FAILED\n");
    return failures == 0 ? 0 : 1;
}
