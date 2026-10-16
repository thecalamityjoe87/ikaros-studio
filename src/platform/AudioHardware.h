#pragma once

#include <juce_audio_devices/juce_audio_devices.h>

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

} // namespace ikaros::platform
