#include "engine/PerfMonitor.h"
#include "platform/AudioHardware.h"

#include <tracktion_engine/tracktion_engine.h>

#if JUCE_LINUX
 #include <dirent.h>
 #include <dlfcn.h>
 #include <fcntl.h>
 #include <sched.h>
 #include <unistd.h>
#endif

// Clang's RealtimeSanitizer (CMake IKAROS_SANITIZE=realtime): between
// __rtsan_realtime_enter and _exit, any allocation, lock or blocking call on
// this thread is reported. GCC doesn't have it, so this is usually off.
#if defined (__has_feature)
 #if __has_feature (realtime_sanitizer)
  #include <sanitizer/rtsan_interface.h>
  #define IKAROS_RTSAN 1
 #endif
#endif

namespace te = tracktion;

namespace ikaros
{

// tools/perf/rtcheck.so, when preloaded (LD_PRELOAD), offers these two
// functions; between them it counts the audio thread's allocations, locks and
// blocking calls. Looked up once, before the audio callbacks are added, and
// only read afterwards, so the audio thread can use them without a lock.
namespace
{
    using RtCheckHook = void (*)();
    RtCheckHook rtCheckEnter = nullptr, rtCheckLeave = nullptr;
}

// On unless CMake's IKAROS_PERF_TOOLS option turns it off (see CMakeLists.txt).
#ifndef IKAROS_PERF_TOOLS
 #define IKAROS_PERF_TOOLS 1
#endif

bool PerfMonitor::isEnabled()
{
   #if IKAROS_PERF_TOOLS
    // A function-local static is initialised once, the first time it's used
    // (and C++ makes that thread-safe), so the environment is read only once.
    static const bool enabled = juce::SystemStats::getEnvironmentVariable ("IKAROS_PERF", {}) == "1";
    return enabled;
   #else
    return false;
   #endif
}

//==============================================================================
PerfMonitor::PerfMonitor (te::Engine& e, std::function<te::Edit*()> editGetter)
    : engine (e), getEdit (std::move (editGetter))
{
   #if JUCE_LINUX
    // dlsym finds a function by name in the libraries loaded so far (here:
    // rtcheck.so, if it was preloaded); nullptr if there isn't one.
    rtCheckEnter = reinterpret_cast<RtCheckHook> (dlsym (RTLD_DEFAULT, "ikaros_rtcheck_enter"));
    rtCheckLeave = reinterpret_cast<RtCheckHook> (dlsym (RTLD_DEFAULT, "ikaros_rtcheck_leave"));
   #endif

    // Must come before Tracktion's own callback (see StartCallback in the
    // header). One side effect: Tracktion is then no longer JUCE's first
    // callback, so JUCE renders it into a spare buffer and adds that to the
    // output, a copy of 2 x buffer-size samples per callback. Negligible, but
    // it's only like that while IKAROS_PERF is on.
    auto& adm = engine.getDeviceManager().deviceManager;
    adm.addAudioCallback (&startCallback);
    adm.addAudioCallback (&endCallback);

    setBurnPercent (juce::SystemStats::getEnvironmentVariable ("IKAROS_PERF_BURN", "0").getDoubleValue());

    if (auto burnPath = juce::SystemStats::getEnvironmentVariable ("IKAROS_PERF_BURN_FILE", {}); burnPath.isNotEmpty())
        burnFile = juce::File (burnPath);

    auto csvPath = juce::SystemStats::getEnvironmentVariable ("IKAROS_PERF_CSV", {});

    csvFile = csvPath.isNotEmpty() ? juce::File (csvPath)
                                   : juce::File::getSpecialLocation (juce::File::userHomeDirectory)
                                         .getChildFile ("ikaros-perf")
                                         .getChildFile ("perf-" + juce::Time::getCurrentTime().formatted ("%Y%m%d-%H%M%S") + ".csv");

   #if JUCE_LINUX
    // Each core's top speed. On a hybrid CPU the efficiency cores' is lower.
    for (int cpu = 0;; ++cpu)
    {
        const juce::File f ("/sys/devices/system/cpu/cpu" + juce::String (cpu) + "/cpufreq/cpuinfo_max_freq");

        if (! f.existsAsFile())
            break;

        coreMaxKHz.push_back (f.loadFileAsString().trim().getIntValue());
        fastestKHz = juce::jmax (fastestKHz, coreMaxKHz.back());
    }
   #endif

    pluginCsvFile = csvFile.withFileExtension ("plugins.csv");
    threadCsvFile = csvFile.withFileExtension ("threads.csv");
    csvFile.getParentDirectory().createDirectory();

    // Replace any old file of the same name, then write the column names.
    auto open = [] (const juce::File& f, const char* header)
    {
        f.deleteFile();
        auto stream = std::make_unique<juce::FileOutputStream> (f);

        if (! stream->openedOk())
            return std::unique_ptr<juce::FileOutputStream>();

        *stream << header << "\n";
        return stream;
    };

    csv = open (csvFile,
                "time_s,label,sample_rate,buffer,callbacks,cb_min_pct,cb_avg_pct,cb_p50_pct,cb_p99_pct,cb_max_pct,"
                "over_budget,late_starts,device_xruns,juce_meter_pct,te_avg_ms,te_max_ms,plugins_pct,audio_threads,rt,"
                "audio_thread_cpu,message_thread_cpu,other_threads_cpu,process_cpu,rss_mb,open_files,threads,burn_pct,"
                "playing,dropped,ui_paint_ms_per_s,ui_paint_calls,ui_frames,ui_max_frame_ms,ui_top_painters,"
                "slow_core_callbacks,avg_pct_fast_cores,avg_pct_slow_cores,cores");
    pluginCsv = open (pluginCsvFile, "time_s,label,track,plugin,avg_pct,max_pct");
    threadCsv = open (threadCsvFile, "time_s,label,tid,name,cpu_pct");

    DBG ("IKAROS_PERF: writing " << csvFile.getFullPathName());
    startTimerHz (4);
}

PerfMonitor::~PerfMonitor()
{
    stopTimer();

    auto& adm = engine.getDeviceManager().deviceManager;
    adm.removeAudioCallback (&endCallback);
    adm.removeAudioCallback (&startCallback);
}

juce::String PerfMonitor::setDeviceSetup (int bufferSize, double sampleRate)
{
    auto& adm = engine.getDeviceManager().deviceManager;
    auto setup = adm.getAudioDeviceSetup();

    if (bufferSize > 0)
        setup.bufferSize = bufferSize;

    if (sampleRate > 0)
        setup.sampleRate = sampleRate;

    return adm.setAudioDeviceSetup (setup, true);
}

void PerfMonitor::resetRun()
{
    drainFifo();   // so earlier callbacks don't count towards the new run
    run.clear();
}

//==============================================================================
// Audio thread. Everything here is a few arithmetic operations and atomic
// loads/stores: no allocation, no locks, no logging, no system calls (apart
// from one gettid when a new device thread first appears).

void PerfMonitor::StartCallback::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    if (device != nullptr && device->getCurrentSampleRate() > 0)
        owner.secondsPerSample = 1.0 / device->getCurrentSampleRate();

    owner.audioThreadTid = 0;   // the restarted device has a new thread
}

void PerfMonitor::StartCallback::audioDeviceIOCallbackWithContext (const float* const*, int,
                                                                  float* const* outputs, int numOutputs, int numSamples,
                                                                  const juce::AudioIODeviceCallbackContext&)
{
    const auto start = juce::Time::getHighResolutionTicks();

    // JUCE adds every later callback's output to this one's, so this is the
    // starting point of the mix: silence.
    for (int i = 0; i < numOutputs; ++i)
        if (outputs[i] != nullptr)
            juce::FloatVectorOperations::clear (outputs[i], numSamples);

    if (owner.audioThreadTid.load (std::memory_order_relaxed) == 0)
        owner.audioThreadTid = (int) platform::currentThreadId();

    owner.currentStart = start;

    // Deliberate overload for testing: spin (never sleep, like a heavy
    // plug-in would) for a share of the buffer's length.
    if (const auto burn = owner.burnFraction.load (std::memory_order_relaxed); burn > 0.0f)
    {
        const auto seconds = burn * numSamples * owner.secondsPerSample.load (std::memory_order_relaxed);
        const auto until = start + (juce::int64) (seconds * (double) juce::Time::getHighResolutionTicksPerSecond());

        while (juce::Time::getHighResolutionTicks() < until)
        {
        }
    }

    // Everything from here to EndCallback is the real-time part.
    if (rtCheckEnter != nullptr)
        rtCheckEnter();

   #if IKAROS_RTSAN
    __rtsan_realtime_enter();
   #endif
}

void PerfMonitor::EndCallback::audioDeviceIOCallbackWithContext (const float* const*, int,
                                                                float* const* outputs, int numOutputs, int numSamples,
                                                                const juce::AudioIODeviceCallbackContext&)
{
   #if IKAROS_RTSAN
    __rtsan_realtime_exit();
   #endif

    if (rtCheckLeave != nullptr)
        rtCheckLeave();

    // This one's output is added to the mix too, so it must be silent.
    for (int i = 0; i < numOutputs; ++i)
        if (outputs[i] != nullptr)
            juce::FloatVectorOperations::clear (outputs[i], numSamples);

    Record record { owner.currentStart, juce::Time::getHighResolutionTicks(), numSamples };

   #if JUCE_LINUX
    record.cpu = sched_getcpu();   // reads a per-thread value the kernel keeps up to date: no system call
   #endif

    // AbstractFifo::write hands out up to two index ranges (free space can
    // wrap around the end of the array); for one item only one is used. The
    // ScopedWrite publishes the item to the reader when it goes out of scope.
    const auto scope = owner.fifo.write (1);

    if (scope.blockSize1 > 0)
        owner.records[(size_t) scope.startIndex1] = record;
    else if (scope.blockSize2 > 0)
        owner.records[(size_t) scope.startIndex2] = record;
    else
        owner.dropped.fetch_add (1, std::memory_order_relaxed);   // full: the message thread is stalled
}

//==============================================================================
// Message thread.

bool PerfMonitor::isSlowCore (int cpu) const
{
    // "Slower" = a top speed under 90% of the fastest core's.
    return cpu >= 0 && cpu < (int) coreMaxKHz.size() && coreMaxKHz[(size_t) cpu] < fastestKHz * 9 / 10;
}

void PerfMonitor::Histogram::add (double percent, bool slowCore)
{
    if (slowCore)
    {
        ++onSlowCores;
        sumSlow += percent;
    }
    else
    {
        sumFast += percent;
    }

    ++bins[(size_t) juce::jlimit (0, numBins - 1, (int) (percent * 2.0))];
    ++count;
    sum += percent;
    min = juce::jmin (min, percent);
    max = juce::jmax (max, percent);

    if (percent > 100.0)
        ++overBudget;
}

PerfMonitor::CallbackStats PerfMonitor::Histogram::getStats() const
{
    CallbackStats s;
    s.callbacks = count;
    s.overBudget = overBudget;
    s.lateStarts = lateStarts;
    s.onSlowCores = onSlowCores;
    s.avgPercentSlowCores = onSlowCores > 0 ? sumSlow / onSlowCores : 0.0;
    s.avgPercentFastCores = count > onSlowCores ? sumFast / (count - onSlowCores) : 0.0;

    if (count == 0)
        return s;

    s.minPercent = min;
    s.maxPercent = max;
    s.avgPercent = sum / count;

    // The top edge of the bin where the running count passes the percentile.
    auto percentile = [this] (double fraction)
    {
        const auto target = (int) std::ceil (fraction * count);
        int seen = 0;

        for (int i = 0; i < numBins; ++i)
            if ((seen += bins[(size_t) i]) >= target)
                return (i + 1) * 0.5;

        return max;
    };

    s.p50Percent = juce::jmin (percentile (0.5), max);
    s.p99Percent = juce::jmin (percentile (0.99), max);
    return s;
}

void PerfMonitor::drainFifo()
{
    const auto ticksPerSecond = (double) juce::Time::getHighResolutionTicksPerSecond();
    const auto secsPerSample = secondsPerSample.load();

    auto process = [&] (const Record& r)
    {
        if (secsPerSample <= 0.0 || r.numSamples <= 0)
            return;

        const auto budget = r.numSamples * secsPerSample;
        const auto percent = (double) (r.endTicks - r.startTicks) / ticksPerSecond / budget * 100.0;

        const bool slow = isSlowCore (r.cpu);
        second.add (percent, slow);
        run.add (percent, slow);
        ++callbacksPerCore[r.cpu];

        // Woken late (or the previous callback ran over): the gap between two
        // starts should be about one buffer.
        if (lastStartTicks != 0 && (double) (r.startTicks - lastStartTicks) / ticksPerSecond > 1.5 * budget)
        {
            ++second.lateStarts;
            ++run.lateStarts;
        }

        lastStartTicks = r.startTicks;
    };

    const auto scope = fifo.read (fifo.getNumReady());

    for (int i = 0; i < scope.blockSize1; ++i)
        process (records[(size_t) (scope.startIndex1 + i)]);

    for (int i = 0; i < scope.blockSize2; ++i)
        process (records[(size_t) (scope.startIndex2 + i)]);
}

void PerfMonitor::timerCallback()
{
    drainFifo();
    readPluginLoads();

    if (++tick % 4 == 0)
        finishSecond();
}

void PerfMonitor::readPluginLoads()
{
    auto* edit = getEdit();

    if (edit == nullptr)
        return;

    // getCpuUsage is Tracktion's smoothed figure for the plug-in's time in
    // each block (see Plugin::applyToBufferWithAutomation); it's an atomic,
    // so reading it here is safe. Sampled 4 times a second.
    auto sample = [this] (te::Plugin& p, const juce::String& trackName)
    {
        if (! p.shouldMeasureCpuUsage())
            return;

        auto& s = pluginSamples[p.itemID.getRawID()];

        if (s.count == 0)
        {
            s.track = trackName;
            s.plugin = p.getName();
        }

        const auto percent = p.getCpuUsage() * 100.0;
        s.sum += percent;
        s.max = juce::jmax (s.max, percent);
        ++s.count;
    };

    for (auto* track : te::getAllTracks (*edit))
        for (auto* p : track->pluginList)
            sample (*p, track->getName());

    for (auto* p : edit->getMasterPluginList())
        sample (*p, "Master");
}

void PerfMonitor::readBurnFile()
{
    if (burnFile != juce::File() && burnFile.existsAsFile())
        setBurnPercent (burnFile.loadFileAsString().trim().getDoubleValue());
}

void PerfMonitor::finishSecond()
{
    const auto now = juce::Time::getMillisecondCounterHiRes();
    auto& adm = engine.getDeviceManager().deviceManager;

    latest.seconds = (now - startTime) / 1000.0;

    if (auto* device = adm.getCurrentAudioDevice())
    {
        latest.sampleRate = device->getCurrentSampleRate();
        latest.bufferSize = device->getCurrentBufferSizeSamples();
    }

    latest.callback = second.getStats();

    juce::StringArray cores;

    for (auto [cpu, count] : callbacksPerCore)
        cores.add (juce::String (cpu) + ":" + juce::String (count));

    latest.cores = cores.joinIntoString (" ");
    callbacksPerCore.clear();
    latest.run = run.getStats();
    second.clear();

    const auto xruns = adm.getXRunCount();
    latest.deviceXruns = juce::jmax (0, xruns - lastDeviceXruns);
    lastDeviceXruns = xruns;

    latest.juceMeterPercent = adm.getCpuUsage() * 100.0;

    // Tracktion's render on its own (it times DeviceManager's callback),
    // since the last reset ("rest" is Tracktion's spelling).
    auto& dm = engine.getDeviceManager();
    const auto stats = dm.getCPUStatistics();
    latest.tracktionAvgMs = stats.meanSeconds * 1000.0;
    latest.tracktionMaxMs = stats.maximumSeconds * 1000.0;
    dm.restCPUStatistics();

    latest.plugins.clear();
    latest.pluginsPercent = 0;

    for (auto& [id, s] : pluginSamples)
    {
        if (s.count == 0)
            continue;

        latest.plugins.push_back ({ s.track, s.plugin, s.sum / s.count, s.max });
        latest.pluginsPercent += s.sum / s.count;
    }

    pluginSamples.clear();
    std::sort (latest.plugins.begin(), latest.plugins.end(),
               [] (const PluginLoad& a, const PluginLoad& b) { return a.avgPercent > b.avgPercent; });

    latest.audioThreadsConfigured = engine.getEngineBehaviour().getNumberOfCPUsToUseForAudio();
    latest.audioThreadRealtime = platform::isThreadRealtime (audioThreadTid.load()).value_or (false);
    latest.burnPercent = burnFraction.load() * 100.0;
    latest.droppedRecords = dropped.exchange (0);

    auto* edit = getEdit();
    latest.playing = edit != nullptr && edit->getTransport().isPlaying();

    readProcessStats();
    latest.ui = uiSampler != nullptr ? uiSampler() : UiSample();

    writeCsvLine();
    readBurnFile();
    lastSecond = now;
}

#if JUCE_LINUX
// Files in /proc say they're empty (size 0) and are made up as they're read,
// so read until the end rather than trusting the size (as juce::File would).
static juce::String readProcFile (const juce::String& path)
{
    juce::String text;
    const int fd = ::open (path.toRawUTF8(), O_RDONLY);

    if (fd < 0)
        return text;

    char buffer[4096];

    for (;;)
    {
        const auto n = ::read (fd, buffer, sizeof (buffer));

        if (n <= 0)
            break;

        text += juce::String (buffer, (size_t) n);
    }

    ::close (fd);
    return text;
}
#endif

void PerfMonitor::readProcessStats()
{
   #if JUCE_LINUX
    const auto elapsed = juce::jmax (0.001, (juce::Time::getMillisecondCounterHiRes() - lastSecond) / 1000.0);
    const auto ticksPerSecond = (double) sysconf (_SC_CLK_TCK);
    const int pid = (int) getpid();
    const int audioTid = audioThreadTid.load();

    std::map<int, double> threadSeconds;
    latest.busiestThreads.clear();
    latest.audioThreadCpu = latest.messageThreadCpu = latest.otherThreadsCpu = latest.processCpu = 0;

    // /proc/self/task has a folder per thread; its "stat" file holds the CPU
    // time used so far (utime + stime, in clock ticks) after the name, which
    // is in brackets and may contain spaces ("Ikaros Studio").
    if (auto* dir = opendir ("/proc/self/task"))
    {
        while (auto* entry = readdir (dir))
        {
            const int tid = atoi (entry->d_name);

            if (tid <= 0)
                continue;

            const auto stat = readProcFile ("/proc/self/task/" + juce::String (tid) + "/stat");
            const auto open = stat.indexOfChar ('('), close = stat.lastIndexOfChar (')');

            if (open < 0 || close < open)
                continue;

            const auto name = stat.substring (open + 1, close);
            const auto fields = juce::StringArray::fromTokens (stat.substring (close + 2), " ", {});

            if (fields.size() < 13)
                continue;

            // After the name: state, ppid, ... utime is the 12th, stime the 13th.
            const auto seconds = (fields[11].getLargeIntValue() + fields[12].getLargeIntValue()) / ticksPerSecond;
            threadSeconds[tid] = seconds;

            const auto it = lastThreadSeconds.find (tid);
            const auto percent = it != lastThreadSeconds.end() ? (seconds - it->second) / elapsed * 100.0 : 0.0;

            latest.processCpu += percent;

            if (tid == audioTid)
                latest.audioThreadCpu = percent;
            else if (tid == pid)
                latest.messageThreadCpu = percent;
            else
                latest.otherThreadsCpu += percent;

            if (percent >= 0.5)
                latest.busiestThreads.push_back ({ tid, name, percent });
        }

        closedir (dir);
    }

    lastThreadSeconds = std::move (threadSeconds);
    std::sort (latest.busiestThreads.begin(), latest.busiestThreads.end(),
               [] (const ThreadLoad& a, const ThreadLoad& b) { return a.percent > b.percent; });

    // Memory and thread count from /proc/self/status ("VmRSS:   123456 kB").
    for (auto line : juce::StringArray::fromLines (readProcFile ("/proc/self/status")))
    {
        if (line.startsWith ("VmRSS:"))
            latest.rssMb = line.fromFirstOccurrenceOf (":", false, false).trim().getDoubleValue() / 1024.0;
        else if (line.startsWith ("Threads:"))
            latest.threads = line.fromFirstOccurrenceOf (":", false, false).trim().getIntValue();
    }

    // Open file handles: one entry each in /proc/self/fd.
    latest.openFiles = 0;

    if (auto* dir = opendir ("/proc/self/fd"))
    {
        while (auto* entry = readdir (dir))
            if (entry->d_name[0] != '.')
                ++latest.openFiles;

        closedir (dir);
    }
   #endif
}

void PerfMonitor::writeCsvLine()
{
    auto quoted = [] (const juce::String& s) { return "\"" + s.replace ("\"", "'") + "\""; };
    auto f = [] (double v) { return juce::String (v, 2); };
    const auto& s = latest;
    const auto t = juce::String (s.seconds, 1);

    if (csv != nullptr)
    {
        juce::StringArray cols {
            t, quoted (label), juce::String (s.sampleRate, 0), juce::String (s.bufferSize),
            juce::String (s.callback.callbacks), f (s.callback.minPercent), f (s.callback.avgPercent),
            f (s.callback.p50Percent), f (s.callback.p99Percent), f (s.callback.maxPercent),
            juce::String (s.callback.overBudget), juce::String (s.callback.lateStarts), juce::String (s.deviceXruns),
            f (s.juceMeterPercent), juce::String (s.tracktionAvgMs, 3), juce::String (s.tracktionMaxMs, 3),
            f (s.pluginsPercent), juce::String (s.audioThreadsConfigured), s.audioThreadRealtime ? "1" : "0",
            f (s.audioThreadCpu), f (s.messageThreadCpu), f (s.otherThreadsCpu), f (s.processCpu),
            juce::String (s.rssMb, 1), juce::String (s.openFiles), juce::String (s.threads), f (s.burnPercent),
            s.playing ? "1" : "0", juce::String (s.droppedRecords),
            f (s.ui.paintMsPerSecond), juce::String (s.ui.paintCalls), juce::String (s.ui.frames),
            f (s.ui.maxFrameMs), quoted (s.ui.topPainters),
            juce::String (s.callback.onSlowCores), f (s.callback.avgPercentFastCores),
            f (s.callback.avgPercentSlowCores), quoted (s.cores) };

        *csv << cols.joinIntoString (",") << "\n";
        csv->flush();   // so the file is complete even if the app is killed
    }

    if (pluginCsv != nullptr)
    {
        for (auto& p : s.plugins)
            if (p.maxPercent >= 0.05)
                *pluginCsv << t << "," << quoted (label) << "," << quoted (p.track) << "," << quoted (p.plugin)
                           << "," << f (p.avgPercent) << "," << f (p.maxPercent) << "\n";

        pluginCsv->flush();
    }

    if (threadCsv != nullptr)
    {
        for (auto& th : s.busiestThreads)
            *threadCsv << t << "," << quoted (label) << "," << th.tid << "," << quoted (th.name) << "," << f (th.percent) << "\n";

        threadCsv->flush();
    }
}

} // namespace ikaros
