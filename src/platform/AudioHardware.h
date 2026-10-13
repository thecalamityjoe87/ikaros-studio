#pragma once

#include <juce_audio_devices/juce_audio_devices.h>

namespace ikaros::platform
{

/*  Makes the device manager's lists notice sound cards that were plugged in
    or removed. Call it every second or so; true means the lists changed.

    macOS and Windows tell JUCE about changes themselves, so there this does
    nothing. On Linux, JUCE lists the ALSA devices once and never again, so
    this watches /proc/asound/cards and has the list scanned afresh when it
    changes. `lastSeen` is the caller's memory between calls (start it empty).
*/
bool refreshAudioDeviceLists (juce::AudioDeviceManager&, juce::String& lastSeen);

} // namespace ikaros::platform
