#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <array>
#include <atomic>
#include <map>

namespace tracktion { inline namespace engine { class Engine; class Edit; } }

namespace ikaros
{

/*  Debug-only performance monitoring (see docs/perf-report.md).

    Off unless the app is started with IKAROS_PERF=1 (and compiled out
    entirely when CMake's IKAROS_PERF_TOOLS is OFF). When it's on:

      - every audio callback is timed, start to end, including all of JUCE's
        callbacks (Tracktion's render, the tuner, the live waveform...), the
        same span the toolbar's CPU meter measures;
      - the message thread turns those timings into min / avg / p99 / max per
        second, counts callbacks that took longer than the buffer lasts
        ("over budget", an xrun or near enough), and adds Tracktion's own
        figures, each plug-in's share, each thread's CPU and the memory used;
      - a line per second goes to a CSV file (IKAROS_PERF_CSV, or
        ~/ikaros-perf/perf-<time>.csv), so runs can be compared.

    The audio thread never logs, allocates or locks here: it writes one small
    record per callback into a lock-free FIFO that the message thread empties.

    IKAROS_PERF_BURN=<percent> (or a number in the file IKAROS_PERF_BURN_FILE,
    re-read every second) adds that much busy work to every callback, to
    overload the audio thread on purpose and check the app survives it.
*/
class PerfMonitor  : private juce::Timer
{
public:
    /** True if IKAROS_PERF=1 was set (read once). Always false when compiled out. */
    static bool isEnabled();

    /** Call before Tracktion's DeviceManager::initialise (see the .cpp for why). */
    PerfMonitor (tracktion::Engine&, std::function<tracktion::Edit*()> getEdit);
    ~PerfMonitor() override;

    /** One second's worth of audio callbacks, as percentages of the buffer's length. */
    struct CallbackStats
    {
        int callbacks = 0;
        double minPercent = 0, avgPercent = 0, p50Percent = 0, p99Percent = 0, maxPercent = 0;
        int overBudget = 0;    // callbacks that took longer than the buffer lasts
        int lateStarts = 0;    // callbacks that started over 1.5 buffers after the previous one
        int onSlowCores = 0;   // callbacks that ran on a slower core (see Snapshot::cores)
        double avgPercentFastCores = 0, avgPercentSlowCores = 0;
    };

    /** A plug-in's share of the audio thread (Tracktion's own measurement). */
    struct PluginLoad
    {
        juce::String track, plugin;
        double avgPercent = 0, maxPercent = 0;   // of the buffer's length, over the last window
    };

    /** CPU used by one of the app's threads (like `top -H`), in % of one core. */
    struct ThreadLoad
    {
        int tid = 0;
        juce::String name;
        double percent = 0;
    };

    /** UI figures, filled in by the UI (see setUiSampler). */
    struct UiSample
    {
        double paintMsPerSecond = 0;     // time spent in instrumented paint() calls
        int paintCalls = 0;
        int frames = 0;                  // window repaints that reached the top-level component
        double maxFrameMs = 0;
        juce::String topPainters;        // e.g. "LevelMeter 120/s 4.1ms; Ruler 30/s 1.0ms"
    };

    /** Everything the overlay shows and the CSV logs, for the last second. */
    struct Snapshot
    {
        double seconds = 0;              // since monitoring started
        double sampleRate = 0;
        int bufferSize = 0;
        CallbackStats callback;
        CallbackStats run;               // the whole run (since start or resetRun)
        int deviceXruns = 0;             // JUCE's count for the last second (device + over budget)
        double juceMeterPercent = 0;     // what the toolbar shows (JUCE's smoothed figure)
        double tracktionAvgMs = 0, tracktionMaxMs = 0;   // Tracktion's render only
        double pluginsPercent = 0;       // sum of all plug-ins' shares
        int audioThreadsConfigured = 0;  // Tracktion's render threads (EngineBehaviour)
        bool audioThreadRealtime = false;
        double audioThreadCpu = 0, messageThreadCpu = 0, otherThreadsCpu = 0, processCpu = 0;
        double rssMb = 0;
        int openFiles = 0, threads = 0;
        double burnPercent = 0;
        bool playing = false;
        int droppedRecords = 0;          // FIFO full (the message thread fell behind)
        juce::String cores;              // which CPUs the callbacks ran on, e.g. "0:150 12:38"
        UiSample ui;
        std::vector<PluginLoad> plugins;   // heaviest first
        std::vector<ThreadLoad> busiestThreads;
        juce::String label;              // set by the stress harness
    };

    Snapshot getSnapshot() const        { return latest; }

    /** The UI's figures, asked for once a second on the message thread. */
    void setUiSampler (std::function<UiSample()> f)     { uiSampler = std::move (f); }

    /** Starts the whole-run figures again (the stress harness calls it once a scenario is playing). */
    void resetRun();

    /** Written into every CSV line, so rows from different scenarios can be told apart. */
    void setLabel (const juce::String& l)               { latest.label = label = l; }

    /** Busy work added to each callback, in percent of the buffer's length. */
    void setBurnPercent (double p)                      { burnFraction = (float) juce::jmax (0.0, p / 100.0); }

    juce::File getCsvFile() const                       { return csvFile; }

    /** Reopens the audio device with this buffer size and sample rate (the
        stress harness's runs). Returns an error message, empty if it worked. */
    juce::String setDeviceSetup (int bufferSize, double sampleRate);

private:
    //==============================================================================
    // Audio thread side.

    // One record per audio callback. Small and trivially copyable, so the
    // FIFO below is just an array.
    struct Record
    {
        juce::int64 startTicks = 0;      // juce::Time::getHighResolutionTicks()
        juce::int64 endTicks = 0;
        int numSamples = 0;
        int cpu = -1;                    // the core it ended on (sched_getcpu)
    };

    // JUCE calls the first callback it was given first, then the others from
    // the last added backwards (see AudioDeviceManager::audioDeviceIOCallbackInt).
    // So `start`, added first, runs before everything, and `end`, added
    // second, runs after everything: together they bracket the whole callback.
    struct StartCallback  : juce::AudioIODeviceCallback
    {
        explicit StartCallback (PerfMonitor& o) : owner (o) {}
        void audioDeviceIOCallbackWithContext (const float* const*, int, float* const*, int, int,
                                               const juce::AudioIODeviceCallbackContext&) override;
        void audioDeviceAboutToStart (juce::AudioIODevice*) override;
        void audioDeviceStopped() override {}
        PerfMonitor& owner;
    };

    struct EndCallback  : juce::AudioIODeviceCallback
    {
        explicit EndCallback (PerfMonitor& o) : owner (o) {}
        void audioDeviceIOCallbackWithContext (const float* const*, int, float* const*, int, int,
                                               const juce::AudioIODeviceCallbackContext&) override;
        void audioDeviceAboutToStart (juce::AudioIODevice*) override {}
        void audioDeviceStopped() override {}
        PerfMonitor& owner;
    };

    // Single producer (audio thread), single consumer (message thread).
    // AbstractFifo only hands out index ranges and keeps its read/write
    // positions in atomics, so neither side ever waits for the other.
    static constexpr int fifoSize = 16384;
    juce::AbstractFifo fifo { fifoSize };
    std::array<Record, fifoSize> records;
    std::atomic<int> dropped { 0 };

    // Only the audio thread touches these (set in StartCallback, read in EndCallback).
    juce::int64 currentStart = 0;

    std::atomic<float> burnFraction { 0.0f };
    std::atomic<double> secondsPerSample { 0.0 };   // set when the device starts
    std::atomic<int> audioThreadTid { 0 };

    //==============================================================================
    // Message thread side.
    void timerCallback() override;
    void drainFifo();
    void finishSecond();
    void readPluginLoads();
    void readProcessStats();
    void writeCsvLine();
    void readBurnFile();

    // Callback percentages are counted in 0.5% bins up to 1000%, so
    // percentiles cost nothing to work out and nothing grows over time.
    struct Histogram
    {
        static constexpr int numBins = 2000;
        std::array<int, numBins> bins {};
        int count = 0, overBudget = 0, lateStarts = 0, onSlowCores = 0;
        double sum = 0, min = 1e9, max = 0, sumFast = 0, sumSlow = 0;

        void add (double percent, bool slowCore);
        void clear()                    { *this = {}; }
        CallbackStats getStats() const;
    };

    tracktion::Engine& engine;
    std::function<tracktion::Edit*()> getEdit;
    StartCallback startCallback { *this };
    EndCallback endCallback { *this };

    Histogram second, run;
    juce::int64 lastStartTicks = 0;
    double startTime = juce::Time::getMillisecondCounterHiRes();
    double lastSecond = startTime;
    int lastDeviceXruns = 0;
    juce::String label;

    // Per-plug-in figures, keyed by the plug-in's item ID, sampled 4 times a second.
    struct PluginSamples { juce::String track, plugin; double sum = 0, max = 0; int count = 0; };
    std::map<juce::uint64, PluginSamples> pluginSamples;

    // Hybrid CPUs (performance and efficiency cores): each core's top speed,
    // to tell which callbacks ran on a slower one, and the count per core
    // for the last second.
    std::vector<int> coreMaxKHz;
    int fastestKHz = 0;
    std::map<int, int> callbacksPerCore;
    bool isSlowCore (int cpu) const;

    // /proc readings from the previous second, for working out CPU use per thread.
    std::map<int, double> lastThreadSeconds;
    double lastProcessSeconds = 0;

    std::function<UiSample()> uiSampler;
    Snapshot latest;

    juce::File csvFile, pluginCsvFile, threadCsvFile, burnFile;
    std::unique_ptr<juce::FileOutputStream> csv, pluginCsv, threadCsv;
    int tick = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PerfMonitor)
};

} // namespace ikaros
