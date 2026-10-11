#pragma once

#include "engine/EngineTypes.h"

namespace ikaros
{

class AudioEngine;

//==============================================================================
/**
    The "EQ" tab: a simple 4-band EQ on a track or on the master channel
    (pass masterTrackId). The bands are a low shelf, two peaks and a high
    shelf.

    Under the hood it's Tracktion's own 4-band equaliser, kept off the
    effects list (it isn't one of the user's effects). It's only added to a
    track the first time a band is changed, so untouched tracks and older
    sessions don't change. A track's EQ sits after its effects; the
    master's after the master effects.

    Every method runs on the message thread. Changes record into the
    session's UndoManager.
*/
class EqController
{
public:
    explicit EqController (AudioEngine&);

    /** The EQ's settings: flat (every gain 0 dB) until it's been changed. */
    EqSettings getEq (TrackId) const;

    /** Sets one band. No undo step of its own: the UI starts one when a drag
        starts, so a whole drag undoes in one go. Values are clamped. */
    void setBand (TrackId, int band, float frequencyHz, float gainDb, float q);

    /** Turns the EQ on or off ("Turn EQ On/Off" undo step). */
    void setEnabled (TrackId, bool enabled);

    /** Back to flat, with every band at its starting frequency ("Reset EQ"). */
    void reset (TrackId);

    /** How loud the EQ makes each of these frequencies, in dB, for drawing
        its curve. (Worked out from the settings, so it's flat until the EQ
        is changed, and 0 everywhere while it's off.) */
    std::vector<float> getResponse (TrackId, const std::vector<float>& frequenciesHz) const;

    static constexpr float minFrequency = 20.0f, maxFrequency = 20000.0f;
    static constexpr float minGain = -20.0f, maxGain = 20.0f;
    static constexpr float minQ = 0.1f, maxQ = 4.0f;

    /** The settings of a flat EQ: where each band starts. */
    static EqSettings getDefaults();

private:
    AudioEngine& owner;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EqController)
};

} // namespace ikaros
