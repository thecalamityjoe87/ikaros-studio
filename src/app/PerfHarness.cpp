#include "app/PerfHarness.h"
#include "app/CommandIDs.h"
#include "app/MainComponent.h"
#include "engine/EffectsController.h"
#include "engine/PluginLibrary.h"
#include "engine/Preferences.h"
#include "engine/SoundLibrary.h"
#include "engine/TrackController.h"

#include <cmath>

namespace ikaros
{

namespace
{
    double nowMs()      { return juce::Time::getMillisecondCounterHiRes(); }

    void log (const juce::String& text)
    {
        juce::Logger::writeToLog ("IKAROS_PERF harness: " + text);
    }

    // "--perf-tracks=16" -> "16" (empty if the option isn't there).
    juce::String getOption (const juce::StringArray& args, const juce::String& name)
    {
        for (auto& a : args)
            if (a.startsWith ("--perf-" + name + "="))
                return a.fromFirstOccurrenceOf ("=", false, false).unquoted();

        return {};
    }
}

juce::StringArray PerfHarness::getScenarioNames()
{
    return { "realistic", "mix", "audio", "synth", "sampled", "drummer", "instrument-each", "effect-each",
             "effects-heavy", "tune", "sends", "automation", "tempo", "plugins3p", "idle-empty", "idle-heavy" };
}

std::unique_ptr<PerfHarness> PerfHarness::createIfRequested (const juce::String& commandLine, AudioEngine& engine,
                                                            MainComponent& main, std::function<void()> quitApp)
{
    const auto args = juce::StringArray::fromTokens (commandLine, true);
    Options o;
    o.scenario = getOption (args, "scenario");

    if (o.scenario.isEmpty() || engine.getPerfMonitor() == nullptr)
        return {};

    if (! getScenarioNames().contains (o.scenario))
    {
        log ("unknown scenario \"" + o.scenario + "\"; scenarios: " + getScenarioNames().joinIntoString (", "));
        quitApp();
        return {};
    }

    auto number = [&] (const char* name, double fallback)
    {
        auto text = getOption (args, name);
        return text.isNotEmpty() ? text.getDoubleValue() : fallback;
    };

    o.tracks = (int) number ("tracks", o.tracks);
    o.bufferSize = (int) number ("buffer", 0);
    o.sampleRate = number ("rate", 0);
    o.seconds = number ("seconds", o.seconds);
    o.warmUp = number ("warmup", o.warmUp);
    o.burnRampTo = number ("burn-ramp", 0);
    o.actions = juce::StringArray::fromTokens (getOption (args, "actions"), ",", {});
    o.actions.removeEmptyStrings();
    o.plugin = getOption (args, "plugin");

    if (auto s = getOption (args, "summary"); s.isNotEmpty())
        o.summary = juce::File::getCurrentWorkingDirectory().getChildFile (s);

    // The first-run welcome would cover the window (the chooser is closed in timerCallback).
    engine.setAppSetting ("starterSoundsOffered", true);

    return std::unique_ptr<PerfHarness> (new PerfHarness (std::move (o), engine, main, std::move (quitApp)));
}

PerfHarness::PerfHarness (Options o, AudioEngine& e, MainComponent& m, std::function<void()> quit)
    : options (std::move (o)), engine (e), main (m), monitor (*e.getPerfMonitor()), quitApp (std::move (quit))
{
    stageStart = nowMs();
    startTimerHz (20);
}

PerfHarness::~PerfHarness()
{
    stopTimer();
}

//==============================================================================
void PerfHarness::timerCallback()
{
    const auto elapsed = (nowMs() - stageStart) / 1000.0;

    switch (stage)
    {
        case Stage::waitForWindow:
            // Long enough for the startup dialogs to have appeared, so they can be closed.
            if (elapsed < 2.0)
                return;

            main.closeSessionChooserForPerf();

            if (options.bufferSize > 0 || options.sampleRate > 0)
                if (auto error = monitor.setDeviceSetup (options.bufferSize, options.sampleRate); error.isNotEmpty())
                    log ("device setup failed: " + error);

            for (auto& s : engine.getSounds().getSounds())
                log ("sound available: " + s.id + " (" + s.detail + ")");

            parts = makeParts();
            startPart();
            return;

        case Stage::build:
            return;

        case Stage::waitReady:
        {
            bool ready = true;

            for (auto& t : engine.getTracks().getTracks())
                if (t.kind == TrackKind::instrument && ! engine.getTracks().effects().isInstrumentReady (t.id))
                    ready = false;

            // A few seconds anyway, for audio files' background work (waveforms, stretched copies).
            if (elapsed < 4.0 || (! ready && elapsed < 120.0))
                return;

            if (! ready)
                log ("instruments still loading after 2 minutes; measuring anyway");

            if (! options.scenario.startsWith ("idle"))
            {
                engine.setLoopRangeBeats (0.0, loopBeats);
                engine.setLooping (true);
                engine.goToStart();
                engine.play();
            }

            stage = Stage::warmUp;
            stageStart = nowMs();
            return;
        }

        case Stage::warmUp:
            if (elapsed < options.warmUp)
                return;

            monitor.setLabel (parts[partIndex].label);
            monitor.resetRun();
            totals = {};
            stage = Stage::measure;
            stageStart = lastActionTime = nowMs();
            actionStep = 0;

            if (options.actions.contains ("record"))
            {
                // Onto an audio track, from the silent test input (see tools/perf/run_matrix.sh).
                for (auto& t : engine.getTracks().getTracks())
                    if (t.kind == TrackKind::audio)
                    {
                        engine.getTracks().setSelectedTrack (t.id);
                        engine.getTracks().inputs().setMonitoring (t.id, options.actions.contains ("monitor"));
                        break;
                    }

                engine.toggleRecord();
            }

            if (options.actions.contains ("ui-mixer"))
                main.invokeCommandForPerf (CommandIDs::showMixerTab);

            return;

        case Stage::measure:
            accumulate (monitor.getSnapshot());

            // A slow ramp up past 100% and back down: the app must survive it
            // and recover (see platform::makeThreadRealtime).
            if (options.burnRampTo > 0)
            {
                const auto half = options.seconds / 2.0;
                const auto t = elapsed < half ? elapsed / half : juce::jmax (0.0, 2.0 - elapsed / half);
                monitor.setBurnPercent (t * options.burnRampTo);
            }

            doActions();

            if (elapsed >= options.seconds)
                finishPart();

            return;

        case Stage::done:
            return;
    }
}

void PerfHarness::accumulate (const PerfMonitor::Snapshot& s)
{
    // The monitor makes a new snapshot once a second; count each one once.
    if (s.seconds == totals.lastSeconds)
        return;

    const bool first = totals.lastSeconds < 0;
    totals.lastSeconds = s.seconds;

    if (first)
    {
        totals.rssStart = s.rssMb;
        totals.openFilesStart = s.openFiles;
        return;   // its second started before the run did
    }

    ++totals.seconds;
    totals.deviceXruns += s.deviceXruns;
    totals.meter += s.juceMeterPercent;
    totals.tracktionMs += s.tracktionAvgMs;
    totals.plugins += s.pluginsPercent;
    totals.audioCpu += s.audioThreadCpu;
    totals.uiCpu += s.messageThreadCpu;
    totals.otherCpu += s.otherThreadsCpu;
    totals.processCpu += s.processCpu;
    totals.paintMs += s.ui.paintMsPerSecond;
    totals.frames += s.ui.frames;
    totals.maxFrameMs = juce::jmax (totals.maxFrameMs, s.ui.maxFrameMs);
    totals.rssEnd = s.rssMb;
    totals.rssMax = juce::jmax (totals.rssMax, s.rssMb);
    totals.openFilesEnd = s.openFiles;
    totals.alwaysRealtime = totals.alwaysRealtime && s.audioThreadRealtime;
}

void PerfHarness::finishPart()
{
    if (engine.getTransportState().recording)
        engine.toggleRecord();

    engine.stop();
    monitor.setBurnPercent (0);

    const auto s = monitor.getSnapshot();
    const auto run = s.run;
    const auto n = juce::jmax (1, totals.seconds);
    auto f = [] (double v) { return juce::String (v, 2); };

    juce::StringArray cols {
        options.scenario, "\"" + parts[partIndex].label + "\"", juce::String (s.sampleRate, 0), juce::String (s.bufferSize),
        juce::String ((int) engine.getTracks().getTracks().size()), juce::String (totals.seconds),
        juce::String (run.callbacks), f (run.avgPercent), f (run.p50Percent), f (run.p99Percent), f (run.maxPercent),
        juce::String (run.overBudget), juce::String (run.lateStarts), juce::String (totals.deviceXruns),
        f (totals.meter / n), juce::String (totals.tracktionMs / n, 3), f (totals.plugins / n),
        f (totals.audioCpu / n), f (totals.uiCpu / n), f (totals.otherCpu / n), f (totals.processCpu / n),
        juce::String (totals.rssStart, 1), juce::String (totals.rssEnd, 1), juce::String (totals.rssMax, 1),
        juce::String (totals.openFilesStart), juce::String (totals.openFilesEnd),
        f (totals.paintMs / n), f (totals.frames / n), f (totals.maxFrameMs),
        totals.alwaysRealtime ? "1" : "0", "\"" + options.actions.joinIntoString (" ") + "\"",
        monitor.getCsvFile().getFileName(),
        f (100.0 * run.onSlowCores / juce::jmax (1, run.callbacks)), f (run.avgPercentFastCores), f (run.avgPercentSlowCores) };

    const auto line = cols.joinIntoString (",");
    log ("result: " + line);

    if (options.summary != juce::File())
    {
        if (! options.summary.existsAsFile())
            options.summary.appendText ("scenario,label,sample_rate,buffer,tracks,seconds,callbacks,cb_avg_pct,cb_p50_pct,"
                                        "cb_p99_pct,cb_max_pct,over_budget,late_starts,device_xruns,juce_meter_avg,"
                                        "te_avg_ms,plugins_pct,audio_cpu,ui_cpu,other_cpu,process_cpu,rss_start_mb,"
                                        "rss_end_mb,rss_max_mb,files_start,files_end,paint_ms_per_s,frames_per_s,"
                                        "max_frame_ms,always_rt,actions,csv,slow_core_pct,avg_pct_fast_cores,avg_pct_slow_cores\n");

        options.summary.appendText (line + "\n");
    }

    if (++partIndex < parts.size())
    {
        startPart();
        return;
    }

    stage = Stage::done;
    log ("finished");
    stopTimer();

    // Not from inside this timer callback: quitting destroys this object.
    juce::MessageManager::callAsync ([quit = quitApp] { quit(); });
}

void PerfHarness::startPart()
{
    stage = Stage::build;
    log ("building \"" + parts[partIndex].label + "\"");

    newEmptySession();
    const auto before = engine.getTracks().getTracks();
    parts[partIndex].build();

    // The empty template's own track goes, unless the part added nothing.
    if (engine.getTracks().getTracks().size() > before.size())
        for (auto& t : before)
            engine.getTracks().deleteTrack (t.id);

    for (auto& t : engine.getTracks().getTracks())
        log ("track: " + t.name + " (" + engine.getTracks().effects().getSoundId (t.id) + ")");

    addedByActions.clear();
    stage = Stage::waitReady;
    stageStart = nowMs();
}

//==============================================================================
// Things to do while the session plays (--perf-actions).
void PerfHarness::doActions()
{
    const auto now = nowMs();
    const auto sinceLast = now - lastActionTime;
    auto has = [this] (const char* name) { return options.actions.contains (name); };
    auto every = [sinceLast] (double ms) { return sinceLast >= ms; };

    // Each action has its own pace; the timer runs at 20 Hz, so the fastest is 50 ms.
    if (! every (50))
        return;

    lastActionTime = now;
    ++actionStep;
    auto& tracks = engine.getTracks();

    if (has ("ui-scroll"))
        main.scrollTimelineForPerf (0.5);

    if (has ("seek") && actionStep % 5 == 0)
        engine.setPositionBeats (juce::Random::getSystemRandom().nextDouble() * loopBeats);

    if (has ("undo") && actionStep % 2 == 0)
    {
        // Ten steps back, then ten forward, over and over.
        if ((actionStep / 20) % 2 == 0 ? engine.canUndo() : ! engine.canRedo())
            engine.undo();
        else
            engine.redo();
    }

    if (has ("playstop") && actionStep % 10 == 0)
        engine.togglePlay();

    if (has ("loop") && actionStep % 20 == 0)
        engine.setLooping (actionStep % 40 != 0);

    if (has ("ui-zoom") && actionStep % 10 == 0)
        main.invokeCommandForPerf ((actionStep / 10) % 2 == 0 ? CommandIDs::zoomIn : CommandIDs::zoomOut);

    if (has ("ui-resize") && actionStep % 10 == 0)
        if (auto* window = main.getTopLevelComponent())
            window->setSize ((actionStep / 10) % 2 == 0 ? 1280 : 1600, (actionStep / 10) % 2 == 0 ? 800 : 1000);

    if (has ("ui-theme") && actionStep % 20 == 0)
        engine.getPreferences().set (settings::themeChoice, (actionStep / 20) % 2 == 0 ? settings::ThemeChoice::light
                                                                                       : settings::ThemeChoice::dark);

    if (has ("ui-library") && actionStep % 20 == 0)
        main.invokeCommandForPerf (CommandIDs::toggleLibrary);

    if (has ("autosave") && actionStep % 40 == 0)
        engine.autosaveNow();

    if (has ("addremove") && actionStep % 40 == 0)
    {
        if (addedByActions.empty())
            addedByActions.push_back (addMixTrack ((int) (actionStep / 40) % 12));
        else
        {
            tracks.deleteTrack (addedByActions.back());
            addedByActions.pop_back();
        }
    }

    if (has ("plugins-addremove") && actionStep % 20 == 0)
    {
        auto all = tracks.getTracks();

        if (! all.empty())
        {
            auto first = all.front().id;
            auto chain = tracks.effects().getEffectChain (first);

            if ((actionStep / 20) % 2 == 0 || chain.empty())
                tracks.effects().addEffect (first, findEffectType ("Reverb"));
            else
                tracks.effects().removeEffect (first, chain.back().id);
        }
    }

    if (has ("tempo-changes") && actionStep % 20 == 0)
        engine.setBpm (100.0 + (actionStep / 20) % 5 * 10.0);
}

//==============================================================================
// Building sessions.

void PerfHarness::newEmptySession()
{
    NewSessionSettings settings;
    settings.kind = SessionTemplate::empty;
    engine.newSession (settings);
}

juce::String PerfHarness::findSound (const juce::StringArray& nameParts, const juce::String& fallbackDetail) const
{
    const auto& sounds = engine.getSounds().getSounds();

    for (auto& part : nameParts)
        for (auto& s : sounds)
            if (s.name.containsIgnoreCase (part))
                return s.id;

    for (auto& s : sounds)
        if (fallbackDetail.isEmpty() || s.detail.containsIgnoreCase (fallbackDetail))
            return s.id;

    return {};
}

juce::String PerfHarness::findEffectType (const juce::String& name) const
{
    for (auto& e : engine.getTracks().effects().getAvailableEffects())
        if (e.name == name)
            return e.type;

    return {};
}

juce::File PerfHarness::getLoopFile (int index) const
{
    const auto& loops = engine.getSounds().getLoops();

    if (loops.empty())
        return {};

    return loops[(size_t) index % loops.size()].file;
}

void PerfHarness::addChords (TrackId track, double startBeat)
{
    auto& t = engine.getTracks();
    const auto region = t.regions().createChordRegion (track, startBeat);

    if (region == 0)
        return;

    // I - V - vi - IV, a bar each: a held chord with the root below, and an
    // eighth-note arpeggio above it. About as busy as a real part.
    const int chords[4][3] { { 60, 64, 67 }, { 55, 59, 62 }, { 57, 60, 64 }, { 53, 57, 60 } };

    for (int bar = 0; bar < 4; ++bar)
    {
        const double barStart = bar * 4.0;
        t.notes().addNote (region, { chords[bar][0] - 12, barStart, 4.0, 90 });

        for (int n : chords[bar])
            t.notes().addNote (region, { n, barStart, 4.0, 80 });

        for (int step = 0; step < 8; ++step)
            t.notes().addNote (region, { chords[bar][step % 3] + 12, barStart + step * 0.5, 0.5, 70 + (step % 4) * 10 });
    }

    t.regions().loopRegion (region, loopBeats);
}

TrackId PerfHarness::addSynth (const juce::String& soundId)
{
    auto& t = engine.getTracks();
    const auto id = t.addInstrumentTrack();

    if (soundId.isNotEmpty())
        t.effects().setSound (id, soundId);

    addChords (id, 0.0);
    return id;
}

TrackId PerfHarness::addAudioLoop (TrackId track, int loopIndex)
{
    auto& t = engine.getTracks();
    const auto file = getLoopFile (loopIndex);

    if (file == juce::File())
        return track;

    for (auto region : t.regions().importAudioFiles (track, { file.getFullPathName() }, 0.0))
        t.regions().loopRegion (region, loopBeats);

    return track;
}

void PerfHarness::addEffects (TrackId track, const juce::StringArray& names)
{
    for (auto& name : names)
        if (auto type = findEffectType (name); type.isNotEmpty())
            engine.getTracks().effects().addEffect (track, type);
}

// One track of a "realistic" song; index picks which kind, so N tracks give a mix.
TrackId PerfHarness::addMixTrack (int index)
{
    auto& t = engine.getTracks();

    switch (index % 12)
    {
        case 0:  return t.addDrummerTrack();
        case 1:  return addSynth (findSound ({ "Bass" }, "Sampled"));
        case 2:  return addSynth (findSound ({ "Grand", "Piano" }, "Sampled"));
        case 3:  return addSynth (findSound ({ "Warm Pad" }));
        case 4:  return addAudioLoop (t.addGuitarTrack(), 1);
        case 5:
        {
            const auto id = addAudioLoop (t.addVocalTrack(), 2);
            addEffects (id, { "Tune", "Compressor", "EQ" });
            return id;
        }
        case 6:  return addAudioLoop (t.addAudioTrack(), 0);
        case 7:  return addSynth (findSound ({ "Strings", "Violin" }, "Sampled"));
        case 8:  return addSynth (findSound ({ "Electric Piano", "Rhodes", "Wurli" }, "Sampled"));
        case 9:  return addSynth (findSound ({ "Bright Lead" }));
        case 10:
        {
            const auto id = addAudioLoop (t.addAudioTrack(), 3);
            addEffects (id, { "Compressor", "Echo" });
            return id;
        }
        default: return addAudioLoop (t.addGuitarTrack (true), 1);
    }
}

void PerfHarness::addDenseAutomation (TrackId track)
{
    auto& automation = engine.getTracks().automation();
    int used = 0;

    // A point every sixteenth note on up to six settings: far busier than anyone would draw.
    for (auto& target : automation.getTargets (track))
    {
        if (! target.editable || used++ >= 6)
            continue;

        std::vector<AutomationPoint> points;

        for (double beat = 0; beat <= loopBeats; beat += 0.25)
            points.push_back ({ beat, 0.5f + 0.4f * (float) std::sin (beat * 0.7 + used) });

        automation.setPoints (track, target.id, std::move (points));
    }
}

std::vector<PerfHarness::Part> PerfHarness::makeParts()
{
    const auto& name = options.scenario;
    const int n = options.tracks;
    const auto label = name + " " + juce::String (n);
    auto& t = engine.getTracks();
    std::vector<Part> list;

    // A 16-track song like the ones the CPU meter reads 50-70% on: drums,
    // bass, keys, strings, synths, guitars through the amp, vocals through
    // Tune, loops, and most tracks sent to the reverb and echo buses.
    auto buildRealistic = [this, &t]
    {
        for (int i = 0; i < 16; ++i)
        {
            const auto id = addMixTrack (i);

            if (i % 4 != 0)
            {
                t.sends().createSend (id, 0);
                t.sends().setSendLevel (id, 0, 0.5f);
            }

            if (i % 3 == 0)
            {
                t.sends().createSend (id, 1);
                t.sends().setSendLevel (id, 1, 0.4f);
            }
        }
    };

    if (name == "realistic" || name == "idle-heavy")
        list.push_back ({ name, buildRealistic });
    else if (name == "tempo")
        list.push_back ({ name, [this, buildRealistic]
        {
            buildRealistic();

            // A tempo change every other bar and a 3/4 section in the middle.
            for (int bar = 2; bar < 16; bar += 2)
                engine.addTempoChange (bar, 90.0 + (bar * 7) % 60);

            engine.addTimeSignatureChange (6, 3, 4);
            engine.addTimeSignatureChange (10, 4, 4);
        } });
    else if (name == "idle-empty")
        list.push_back ({ name, [] {} });
    else if (name == "mix")
        list.push_back ({ label, [this, n] { for (int i = 0; i < n; ++i) addMixTrack (i); } });
    else if (name == "audio")
        list.push_back ({ label, [this, n, &t] { for (int i = 0; i < n; ++i) addAudioLoop (t.addAudioTrack(), i); } });
    else if (name == "synth")
        list.push_back ({ label, [this, n]
        {
            const juce::StringArray patches { "Warm Pad", "Bright Lead", "Basic Synth" };

            for (int i = 0; i < n; ++i)
                addSynth (findSound ({ patches[i % patches.size()] }));
        } });
    else if (name == "sampled")
        list.push_back ({ label, [this, n]
        {
            const juce::StringArray kinds { "Grand", "Bass", "Strings", "Electric Piano", "Soft Piano", "Pizzicato" };

            for (int i = 0; i < n; ++i)
                addSynth (findSound ({ kinds[i % kinds.size()] }, "Sampled"));
        } });
    else if (name == "drummer")
        list.push_back ({ label, [n, &t] { for (int i = 0; i < n; ++i) t.addDrummerTrack(); } });
    else if (name == "tune")
        list.push_back ({ label, [this, n, &t]
        {
            for (int i = 0; i < n; ++i)
                addEffects (addAudioLoop (t.addVocalTrack(), 2), { "Tune" });
        } });
    else if (name == "sends")
        list.push_back ({ label, [this, n, &t]
        {
            for (int i = 0; i < n; ++i)
            {
                const auto id = addMixTrack (i);

                for (int bus = 0; bus < 2; ++bus)
                {
                    t.sends().createSend (id, bus);
                    t.sends().setSendLevel (id, bus, 0.5f);
                }
            }
        } });
    else if (name == "automation")
        list.push_back ({ label, [this, n] { for (int i = 0; i < n; ++i) addDenseAutomation (addMixTrack (i)); } });
    else if (name == "effects-heavy")
        list.push_back ({ label, [this, n, &t]
        {
            for (int i = 0; i < n; ++i)
            {
                const auto id = addAudioLoop (i % 4 == 3 ? t.addGuitarTrack() : t.addVocalTrack(), i);
                addEffects (id, { "Compressor", "EQ", "Reverb", "Echo", "Chorus" });

                if (i % 2 == 0)
                    addEffects (id, { "Tune" });
            }
        } });
    else if (name == "plugins3p")
    {
        juce::String type;

        for (auto& p : engine.getPlugins().getEffects())
            if (type.isEmpty() && (options.plugin.isEmpty() || p.name.containsIgnoreCase (options.plugin)))
            {
                type = "plugin:" + p.id;
                log ("third-party plug-in: " + p.name + " (" + p.format + ")");
            }

        if (type.isEmpty())
            log ("no third-party effect matches \"" + options.plugin + "\"");

        list.push_back ({ label, [this, n, type, &t]
        {
            for (int i = 0; i < n; ++i)
                if (type.isNotEmpty())
                    t.effects().addEffect (addAudioLoop (t.addAudioTrack(), i), type);
        } });
    }
    else if (name == "instrument-each")
    {
        // Every instrument the library has, alone, on one track.
        for (auto& s : engine.getSounds().getSounds())
            list.push_back ({ "instrument: " + s.name + " [" + s.detail + "]", [this, id = s.id] { addSynth (id); } });

        list.push_back ({ "instrument: Drummer", [&t] { t.addDrummerTrack(); } });
    }
    else if (name == "effect-each")
    {
        // Every built-in effect, alone, on one looping audio track (plus the bare track to compare with).
        list.push_back ({ "effect: (none)", [this, &t] { addAudioLoop (t.addAudioTrack(), 0); } });

        for (auto& e : t.effects().getAvailableEffects())
            if (! e.isThirdParty)
                list.push_back ({ "effect: " + e.name, [this, &t, type = e.type]
                {
                    t.effects().addEffect (addAudioLoop (t.addAudioTrack(), 0), type);
                } });
    }

    return list;
}

} // namespace ikaros
