#pragma once

#include "engine/AudioEngine.h"
#include "engine/PerfMonitor.h"

namespace ikaros
{

class MainComponent;

/*  Debug-only stress harness (IKAROS_PERF=1, see docs/perf-report.md).

    Started from the command line, e.g.

        IKAROS_PERF=1 "Ikaros Studio" --perf-scenario=mix --perf-tracks=16
                                      --perf-buffer=256 --perf-rate=48000 --perf-seconds=30

    it builds a session from scratch (so runs are the same every time and
    can be repeated before and after a fix), plays it in a loop, optionally
    does things while it plays (--perf-actions=playstop,undo,ui-scroll...),
    then appends one line of results to a summary CSV (--perf-summary=FILE)
    and quits. PerfMonitor writes the per-second CSVs alongside.

    tools/perf/run_matrix.sh runs the whole set of scenarios.
*/
class PerfHarness  : private juce::Timer
{
public:
    /** A harness if the command line asks for one and the monitor is on, otherwise nullptr. */
    static std::unique_ptr<PerfHarness> createIfRequested (const juce::String& commandLine, AudioEngine&,
                                                          MainComponent&, std::function<void()> quitApp);
    ~PerfHarness() override;

    /** The scenario names, for --perf-scenario=help. */
    static juce::StringArray getScenarioNames();

private:
    struct Options
    {
        juce::String scenario;
        int tracks = 16;
        int bufferSize = 0;            // 0 = leave the device alone
        double sampleRate = 0;
        double seconds = 30;           // measured, after the warm-up
        double warmUp = 6;
        juce::StringArray actions;
        juce::File summary;
        juce::String plugin;           // third-party plug-in to use (part of its name)
        double burnRampTo = 0;         // "overload": burn from 0 up to this percent, then back down
    };

    PerfHarness (Options, AudioEngine&, MainComponent&, std::function<void()> quitApp);

    // One measured part of a run. Most scenarios have one; the "-each"
    // scenarios measure every instrument or effect type on its own.
    struct Part
    {
        juce::String label;
        std::function<void()> build;   // fills the (new, empty) session
    };

    enum class Stage { waitForWindow, build, waitReady, warmUp, measure, done };

    void timerCallback() override;
    void startPart();
    void finishPart();
    void doActions();
    void accumulate (const PerfMonitor::Snapshot&);

    // Session building blocks.
    void newEmptySession();
    std::vector<Part> makeParts();
    TrackId addSynth (const juce::String& soundId);
    TrackId addAudioLoop (TrackId track, int loopIndex);
    TrackId addMixTrack (int index);
    void addChords (TrackId, double startBeat);
    void addEffects (TrackId, const juce::StringArray& names);
    void addDenseAutomation (TrackId);
    juce::String findSound (const juce::StringArray& nameParts, const juce::String& fallbackDetail = {}) const;
    juce::String findEffectType (const juce::String& name) const;
    juce::File getLoopFile (int index) const;

    Options options;
    AudioEngine& engine;
    MainComponent& main;
    PerfMonitor& monitor;
    std::function<void()> quitApp;

    std::vector<Part> parts;
    size_t partIndex = 0;
    Stage stage = Stage::waitForWindow;
    double stageStart = 0;            // ms, juce::Time::getMillisecondCounterHiRes
    double lastActionTime = 0;
    int actionStep = 0;
    std::vector<TrackId> addedByActions;

    // Totals over the measured part, from the monitor's once-a-second snapshots.
    struct Totals
    {
        double lastSeconds = -1;
        int seconds = 0, deviceXruns = 0;
        double meter = 0, tracktionMs = 0, plugins = 0, audioCpu = 0, uiCpu = 0, otherCpu = 0, processCpu = 0;
        double paintMs = 0, frames = 0, maxFrameMs = 0, rssStart = 0, rssEnd = 0, rssMax = 0;
        int openFilesStart = 0, openFilesEnd = 0;
        bool alwaysRealtime = true;
    } totals;

    static constexpr double loopBeats = 64.0;   // 16 bars of 4/4: the stretch every scenario loops

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PerfHarness)
};

} // namespace ikaros
