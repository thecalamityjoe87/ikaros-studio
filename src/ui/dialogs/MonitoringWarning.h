#pragma once

#include "engine/AudioEngine.h"

namespace ikaros
{

/** Turns monitoring on or off for a track. The first time monitoring is
    turned on (ever, on this computer), it first explains feedback, like
    GarageBand, and only turns it on if the user goes ahead. */
void setMonitoringWithWarning (AudioEngine&, TrackId, bool shouldMonitor, juce::Component* associatedComponent);

/** Just the warning: calls back with true if the user went ahead (straight
    away, without asking, once it has been shown). */
void confirmMonitoring (AudioEngine&, juce::Component* associatedComponent, std::function<void (bool goAhead)>);

} // namespace ikaros
