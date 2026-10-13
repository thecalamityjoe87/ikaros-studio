#pragma once

#include "engine/EngineTypes.h"
#include <functional>
#include <optional>

namespace ikaros
{

class AudioEngine;
class SendBuses;

//==============================================================================
/**
    Automation: curves that move a track's settings as the song plays.

    What can be automated (a "target", see AutomationTarget): the track's
    volume and pan, its Reverb and Delay sends, the Sound tab's knobs, and
    the parameters of third-party plug-ins on the track.

    Points sit on the timeline in beats, and hold the value the setting's
    own control shows (0..1), so the UI never deals with dB or semitones.
    Automation belongs to the track: moving a region doesn't move it.

    How Tracktion stores it: each parameter keeps its curve in its plug-in's
    state (an AUTOMATIONCURVE with POINTs), and our version stores a point's
    time in seconds. When the tempo changes, Tracktion moves the points so
    they stay on the same beat (plug-ins have remapOnTempoChange set).
    A missing plug-in has no parameters for Tracktion to move, so
    keepUnloadedCurvesOnBeats does that for it.

    Each track has one automation lane, shown or hidden from its header,
    and picks which target it shows. That's saved with the session but isn't
    an undo step: like scrolling, it changes the view, not the song.

    Every method runs on the message thread. Like the faders, the caller
    starts the undo step (once per drag).
*/
class AutomationEditor
{
public:
    AutomationEditor (AudioEngine&, SendBuses&);

    //==============================================================================
    /** Everything on the track that can be automated, in the order the picker
        shows them: the mixer's, the Sound tab's, then each plug-in's. */
    std::vector<AutomationTarget> getTargets (TrackId) const;
    std::optional<AutomationTarget> getTarget (TrackId, const juce::String& targetId) const;

    /** The ids of the targets that have points. */
    juce::StringArray getAutomatedTargets (TrackId) const;

    //==============================================================================
    // The lane under each track
    void setLaneShown (TrackId, bool);
    bool isLaneShown (TrackId) const;

    /** Shows or hides every track's lane at once (the "Show Automation"
        button). Showing them switches a lane whose setting has no
        automation to one that has, so all of the session's automation is
        on screen. */
    void setAllLanesShown (bool);
    bool areAllLanesShown() const;   // false if there are no tracks

    /** What the lane shows. Volume until the user picks something else. */
    void setLaneTarget (TrackId, const juce::String& targetId);
    juce::String getLaneTarget (TrackId) const;

    //==============================================================================
    // Points
    /** A target's points, sorted by beat (empty if it isn't automated). */
    std::vector<AutomationPoint> getPoints (TrackId, const juce::String& targetId) const;

    /** Replaces a target's points: adding, moving and deleting points all go
        through here. They're sorted by beat, and values kept within 0..1.
        No points means no automation: the control works by hand again.
        Returns false if the target can't be automated (unknown, or on a
        missing plug-in). */
    bool setPoints (TrackId, const juce::String& targetId, std::vector<AutomationPoint>);

    /** The value the target's control shows right now, 0..1: a new point
        starts at it, and a lane without points draws it as a flat line. */
    float getCurrentValue (TrackId, const juce::String& targetId) const;

    /** A value as the target's control would show it, e.g. "-6.0 dB" or "L 40". */
    juce::String describeValue (TrackId, const juce::String& targetId, float value) const;

    //==============================================================================
    /** @internal Runs a tempo change, keeping the points of curves Tracktion
        can't move itself (on missing plug-ins) on the same beat. */
    void keepUnloadedCurvesOnBeats (const std::function<void()>& changeTempo);

private:
    struct Resolved;
    std::optional<Resolved> resolve (TrackId, const juce::String& targetId, bool createIfNeeded) const;

    AudioEngine& owner;
    SendBuses& sends;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AutomationEditor)
};

} // namespace ikaros
