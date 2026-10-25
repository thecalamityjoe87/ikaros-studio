#include "platform/AudioHardware.h"

#if JUCE_LINUX
 #include <alsa/asoundlib.h>
 #include <array>
 #include <sched.h>
 #include <signal.h>
 #include <sys/resource.h>
 #include <sys/syscall.h>
 #include <systemd/sd-bus.h>
 #include <unistd.h>
#endif

namespace ikaros::platform
{

bool refreshAudioDeviceLists (juce::AudioDeviceManager& manager, HardwareWatch& watch)
{
   #if JUCE_LINUX
    const auto cards = juce::File ("/proc/asound/cards").loadFileAsString();

    // The first call only notes what's there.
    if (watch.cards.isEmpty())
    {
        watch.cards = cards;
        return false;
    }

    // A card is listed in /proc before its device files are usable, so one scan can miss it: scan again for a while.
    if (cards != watch.cards)
    {
        watch.cards = cards;
        watch.scansLeft = 6;
    }

    if (watch.scansLeft == 0)
        return false;

    --watch.scansLeft;

    juce::AudioIODeviceType* stale = nullptr;

    for (auto* type : manager.getAvailableDeviceTypes())
        if (type->getTypeName() == "ALSA")
            stale = type;

    if (stale == nullptr)
        return false;

    // JUCE's ALSA list never scans twice, so a new one stands in for the old.
    std::unique_ptr<juce::AudioIODeviceType> fresh (juce::AudioIODeviceType::createAudioIODeviceType_ALSA());

    if (fresh == nullptr)
        return false;

    // ALSA caches its configuration, so a card plugged in since wouldn't be listed.
    snd_config_update_free_global();
    fresh->scanForDevices();

    if (fresh->getDeviceNames (true) == stale->getDeviceNames (true)
         && fresh->getDeviceNames (false) == stale->getDeviceNames (false))
        return false;

    manager.removeAudioDeviceType (stale);
    manager.addAudioDeviceType (std::move (fresh));
    manager.sendChangeMessage();   // so an open device picker updates too
    return true;
   #else
    juce::ignoreUnused (manager, watch);
    return false;
   #endif
}

//==============================================================================
#if JUCE_LINUX
namespace
{
    // Below PipeWire's own threads (88) when we have the right to choose;
    // RealtimeKit caps it lower anyway (20 by default).
    constexpr int preferredPriority = 70;

    // The threads made real-time (the audio thread and Tracktion's render
    // workers), for the signal handler below; 0 = a free slot. Plain atomics
    // in a fixed array, as the handler can't take locks or allocate.
    std::array<std::atomic<pid_t>, 16> realtimeThreads {};

    void rememberRealtimeThread (pid_t tid)
    {
        for (auto& slot : realtimeThreads)
            if (slot.load() == tid)
                return;

        for (auto& slot : realtimeThreads)
            if (pid_t empty = 0; slot.compare_exchange_strong (empty, tid))
                return;
    }

    // The kernel sends SIGXCPU when a real-time thread has run for too long
    // without a break (the RLIMIT_RTTIME soft limit), and kills the app at
    // the hard limit. Putting the thread back to ordinary priority stops the
    // count. (Only system calls are safe in a signal handler; this is one.)
    //
    // All of them go back, not just the one that ran too long: the session is
    // too heavy, and a real-time audio thread waiting on an ordinary worker
    // would only spin. RealtimeAudioThread asks again once the load is light.
    //
    // SCHED_RESET_ON_FORK must stay set: RealtimeKit (and makeThreadRealtime
    // below) set it, and only a privileged process may clear it. Asking for
    // plain SCHED_OTHER fails with EPERM, the thread stays real-time, and the
    // kernel kills the app 100 ms later at the hard limit. That was the crash
    // when the CPU meter got near 90-100%.
    void onCpuLimit (int)
    {
        for (auto& slot : realtimeThreads)
        {
            if (const auto tid = slot.load())
            {
                sched_param ordinary {};
                sched_setscheduler (tid, SCHED_OTHER | SCHED_RESET_ON_FORK, &ordinary);
            }
        }
    }

    // RealtimeKit only helps a process that limits how long its real-time
    // threads may run without a break, and the limit is a good idea anyway.
    void limitRealtimeRuns (rlim_t maxMicroseconds)
    {
        rlimit limit {};
        getrlimit (RLIMIT_RTTIME, &limit);

        // The hard limit can only come down.
        const auto hard = juce::jmin (limit.rlim_max, maxMicroseconds);
        limit = { hard / 2, hard };
        setrlimit (RLIMIT_RTTIME, &limit);

        struct sigaction action {};
        action.sa_handler = onCpuLimit;
        sigemptyset (&action.sa_mask);
        action.sa_flags = SA_RESTART;
        sigaction (SIGXCPU, &action, nullptr);
    }

    // RealtimeKit and the Realtime portal have the same methods and
    // properties; they live on different buses under different names.
    struct RealtimeService
    {
        bool sessionBus;
        const char* name;
        const char* path;
        const char* interface;
    };

    bool askService (const RealtimeService& service, pid_t tid)
    {
        sd_bus* bus = nullptr;

        if ((service.sessionBus ? sd_bus_open_user (&bus) : sd_bus_open_system (&bus)) < 0)
            return false;

        // A unique_ptr with a custom deleter closes the connection however
        // this function returns.
        std::unique_ptr<sd_bus, decltype (&sd_bus_flush_close_unref)> closer (bus, &sd_bus_flush_close_unref);

        sd_bus_error error = SD_BUS_ERROR_NULL;
        std::int32_t maxPriority = 0;
        std::int64_t maxRunMicroseconds = 0;

        sd_bus_get_property_trivial (bus, service.name, service.path, service.interface,
                                     "MaxRealtimePriority", &error, 'i', &maxPriority);
        sd_bus_error_free (&error);

        sd_bus_get_property_trivial (bus, service.name, service.path, service.interface,
                                     "RTTimeUSecMax", &error, 'x', &maxRunMicroseconds);
        sd_bus_error_free (&error);

        if (maxPriority <= 0)
            return false;

        if (maxRunMicroseconds > 0)
            limitRealtimeRuns ((rlim_t) maxRunMicroseconds);

        const int result = sd_bus_call_method (bus, service.name, service.path, service.interface,
                                               "MakeThreadRealtimeWithPID", &error, nullptr, "ttu",
                                               (std::uint64_t) getpid(), (std::uint64_t) tid,
                                               (std::uint32_t) juce::jmin ((int) maxPriority, preferredPriority));
        if (result < 0)
            DBG ("Real-time audio thread refused by " << service.name << ": " << error.message);

        sd_bus_error_free (&error);
        return result >= 0;
    }
}
#endif

std::int64_t currentThreadId()
{
   #if JUCE_LINUX
    return (std::int64_t) syscall (SYS_gettid);
   #else
    return 0;
   #endif
}

bool makeThreadRealtime (std::int64_t threadId)
{
   #if JUCE_LINUX
    const auto tid = (pid_t) threadId;

    if (tid <= 0)
        return false;

    rememberRealtimeThread (tid);
    limitRealtimeRuns (200000);   // 200 ms, RealtimeKit's usual limit

    // Allowed for users with a real-time limit (e.g. in the "audio" group).
    // SCHED_RESET_ON_FORK: anything the thread starts is ordinary again.
    sched_param param {};
    param.sched_priority = preferredPriority;

    if (sched_setscheduler (tid, SCHED_RR | SCHED_RESET_ON_FORK, &param) == 0)
        return true;

    // Everyone else asks a service that's allowed to. Inside a Flatpak only
    // the portal can be reached.
    if (juce::File ("/.flatpak-info").exists())
        return askService ({ true, "org.freedesktop.portal.Desktop", "/org/freedesktop/portal/desktop",
                             "org.freedesktop.portal.Realtime" }, tid);

    return askService ({ false, "org.freedesktop.RealtimeKit1", "/org/freedesktop/RealtimeKit1",
                         "org.freedesktop.RealtimeKit1" }, tid);
   #else
    juce::ignoreUnused (threadId);
    return false;
   #endif
}

void forgetRealtimeThread (std::int64_t threadId)
{
   #if JUCE_LINUX
    // Thread IDs are reused (by any process), so a gone thread's mustn't stay
    // where the signal handler would demote whatever thread gets it next.
    for (auto& slot : realtimeThreads)
        if (pid_t tid = (pid_t) threadId; tid > 0)
            slot.compare_exchange_strong (tid, 0);
   #else
    juce::ignoreUnused (threadId);
   #endif
}

std::optional<bool> isThreadRealtime (std::int64_t threadId)
{
   #if JUCE_LINUX
    if (threadId <= 0)
        return false;

    // -1 if the thread is gone. The reset-on-fork flag comes back with the policy.
    const int policy = sched_getscheduler ((pid_t) threadId);
    return policy >= 0 && ((policy & ~SCHED_RESET_ON_FORK) == SCHED_FIFO || (policy & ~SCHED_RESET_ON_FORK) == SCHED_RR);
   #else
    juce::ignoreUnused (threadId);
    return std::nullopt;
   #endif
}

bool keepOnFastestCores (std::int64_t threadId)
{
   #if JUCE_LINUX
    const auto tid = (pid_t) threadId;

    if (tid <= 0)
        return false;

    // The cores this process may use (a cpu_set_t is a bit per core).
    cpu_set_t allowed;
    CPU_ZERO (&allowed);

    if (sched_getaffinity (0, sizeof (allowed), &allowed) != 0)
        return false;

    // Each allowed core's top speed, from the kernel's cpufreq files.
    std::vector<std::pair<int, int>> speeds;   // core, kHz
    int fastest = 0;

    for (int cpu = 0; cpu < CPU_SETSIZE; ++cpu)
    {
        if (! CPU_ISSET (cpu, &allowed))
            continue;

        const juce::File f ("/sys/devices/system/cpu/cpu" + juce::String (cpu) + "/cpufreq/cpuinfo_max_freq");
        const int kHz = f.existsAsFile() ? f.loadFileAsString().trim().getIntValue() : 0;

        if (kHz <= 0)
            return false;   // speeds unknown (a VM, say): leave the scheduler to it

        speeds.push_back ({ cpu, kHz });
        fastest = juce::jmax (fastest, kHz);
    }

    cpu_set_t fast;
    CPU_ZERO (&fast);

    for (auto [cpu, kHz] : speeds)
        if (kHz >= fastest * 9 / 10)
            CPU_SET (cpu, &fast);

    // All cores alike: nothing to choose.
    if (CPU_COUNT (&fast) == (int) speeds.size())
        return false;

    return sched_setaffinity (tid, sizeof (fast), &fast) == 0;
   #else
    juce::ignoreUnused (threadId);
    return false;
   #endif
}

} // namespace ikaros::platform
