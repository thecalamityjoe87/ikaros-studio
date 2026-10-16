#pragma once

#include "engine/AudioEngine.h"

namespace ikaros
{

/** Asks whether to switch to a device that was just plugged in (or away
    from one that was unplugged), like GarageBand. "Remember my choice"
    sets the "Audio device changes" setting to always switch or never ask.
    `onClosed` is called once the user has answered. */
void showDeviceChangePrompt (AudioEngine&, const AudioDevicePrompt&, juce::Component* associatedComponent,
                             std::function<void()> onClosed);

} // namespace ikaros
