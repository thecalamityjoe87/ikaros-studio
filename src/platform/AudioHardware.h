#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <optional>

namespace ikaros::platform
{

/*  Makes the device manager's lists notice sound cards that were plugged in
    or removed. Call it every second or so; true means the lists changed.

    macOS and Windows tell JUCE about changes themselves, so there this does
    nothing. On Linux, JUCE lists the ALSA devices once and never again, so
    this watches /proc/asound/cards and has the list scanned afresh when it
    changes, and again for a few calls after, as a new card isn't always
    usable right away. `watch` is the caller's memory between calls.
*/
struct HardwareWatch
{
    juce::String cards;
    int scansLeft = 0;
};

bool refreshAudioDeviceLists (juce::AudioDeviceManager&, HardwareWatch& watch);

/*  The kernel's ID for the calling thread (Linux's gettid; 0 elsewhere).
    It's a system call, so the audio thread should ask once, not every block.
*/
std::int64_t currentThreadId();

/*  Gives a thread real-time priority, so the rest of the desktop can't keep
    it waiting: an audio thread that's late by a millisecond is a click.

    macOS and Windows already run audio threads that way, so there this does
    nothing. On Linux, JUCE's ALSA thread runs at ordinary priority, and most
    users aren't allowed to raise it themselves. This tries directly first,
    then asks RealtimeKit (the service PipeWire uses for the same thing), or,
    in a Flatpak, the Realtime portal. If the thread ever hogs the CPU for a
    long stretch it's put back to ordinary priority, so an overloaded session
    can't freeze the desktop.

    It waits on D-Bus: call it from the message thread, never the audio
    thread. True if the thread is now real-time.
*/
bool makeThreadRealtime (std::int64_t threadId);

/*  Whether a thread runs at real-time priority right now (it may have been
    put back since makeThreadRealtime, or never got it). Empty on macOS and
    Windows, where the OS looks after audio threads itself.
*/
std::optional<bool> isThreadRealtime (std::int64_t threadId);

/*  Keeps a thread on the CPU's fastest cores. Laptop CPUs often mix fast
    "performance" cores with slower "efficiency" ones, and Linux puts a
    real-time thread on whichever core is free, without regard to its speed:
    on the development laptop the same session cost the audio thread 30% of
    its time on a performance core, 50% on an efficiency core and 93% on a
    low-power one (docs/perf-report.md, section 3.2).

    "Fastest" = a top speed within 10% of the fastest core's. On a CPU whose
    cores are all alike this changes nothing (and returns false). Linux only;
    false elsewhere. Call it from the message thread, like makeThreadRealtime.
*/
bool keepOnFastestCores (std::int64_t threadId);

} // namespace ikaros::platform
