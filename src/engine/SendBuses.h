#pragma once

#include "engine/EngineTypes.h"

namespace ikaros
{

class AudioEngine;

//==============================================================================
/**
    Two shared effects, Reverb and Delay, that every track can send some of
    its sound to (the "send" knobs on the mixer's channel strips). Sharing one
    reverb gives the whole song the same space and costs much less CPU than
    a reverb on every track.

    How it's built in Tracktion: each bus is a hidden track holding an Aux
    Return plugin followed by the effect, set fully wet. A track's send is an
    Aux Send plugin placed after its volume plugin (post-fader, so turning a
    track down turns its reverb down too). Both are created the first time a
    send is turned up, and saved with the session.

    Every method runs on the message thread. Like the volume fader, the caller
    starts the undo step (once per knob drag).
*/
class SendBuses
{
public:
    explicit SendBuses (AudioEngine&);

    static constexpr int reverb = 0, delay = 1, numBuses = 2;
    static juce::String getBusName (int bus);

    /** 0 = nothing sent, 1 = full level (0 dB). */
    float getSendLevel (TrackId, int bus) const;
    void setSendLevel (TrackId, int bus, float level0to1);

private:
    AudioEngine& owner;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SendBuses)
};

} // namespace ikaros
