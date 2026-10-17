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

} // namespace ikaros::platform
