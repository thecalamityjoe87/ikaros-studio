#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <optional>
#include <vector>

namespace ikaros
{

/** What a mapped controller knob, fader or button does in the app. The
    track ones work on the selected track, so a controller works the same
    in every session. */
enum class ControllerAction
{
    // Buttons
    play, stop, playStop, record, loop, metronome, undo, redo, rewind, fastForward, goToStart,
    previousTrack, nextTrack, muteTrack, soloTrack, armTrack,

    // Knobs and faders: the selected track's...
    volume, pan, reverbSend, delaySend,
    soundKnob,       // ...Sound tab knobs (ControllerProfile::Control::index)
    masterVolume,

    // Endless knobs ("encoders"), which say how far they turned
    selectTrack,     // the track above or below
    moveBars,        // the playhead, a bar per click

    // A controller's own fader bank switch: which tracks a Mackie Control
    // surface's faders are (ControllerProfile::Control::index = the group,
    // from 0, of ControllerProfile::faders tracks each)
    faderBank,
};

/** A controller's map: which of its controls (MIDI controller numbers) do
    what. The app has one for each controller it knows (resources/controllers),
    used whenever that controller is plugged in.

    The file format is JSON:

        {
          "name": "Arturia MiniLab 3",
          "ports": [ "Minilab3 MIDI" ],
          "controls": [
            { "cc": 107, "type": "button", "action": "play" },
            { "cc": 14,  "type": "knob",   "action": "volume" },
            { "cc": 86,  "type": "knob",   "action": "soundKnob", "index": 0 },
            { "cc": 28,  "type": "encoder", "action": "selectTrack" }
          ]
        }

    "type" is "button" (does its action when pressed), "knob" (a knob or
    fader: 0-127 is the whole range) or "encoder" (an endless knob: 64 =
    still, above = clicks to the right, below = to the left). "channel" (1-16)
    is optional: without it, any channel. A control can be a system
    message instead of a controller: "sysex": "F0 00 20 6B ... F7" (hex), with
    no "cc" or "type"; it does its action when it arrives. Or a program
    change: "pgm": 0-127 (some pads send those), also without a "type".

    A port ending in "*" matches every port whose name starts with the rest
    ("MPK mini 3*": "MPK mini 3" on Windows and macOS, "MPK mini 3 MIDI 1" on
    Linux).

    "mackiePorts" names the controller's Mackie Control ports, and "faders"
    how many faders it has if that isn't Mackie Control's 8 (see
    ControlSurface::setNumStrips).

    "alsoMackie": true marks a control that also sends Mackie Control when
    the controller is in its Mackie mode (the MiniLab 3's transport pads and
    faders do). While its Mackie port is talking, the Mackie message is the
    one that counts (see SurfaceHandler), so a press isn't done twice. */
struct ControllerProfile
{
    struct Control
    {
        enum class Type { button, knob, encoder };

        int channel = 0;      // 1-16, 0 = any
        int controller = 0;   // the MIDI controller (CC) number
        Type type = Type::button;
        ControllerAction action = ControllerAction::play;
        int index = 0;        // soundKnob: which knob, from 0; faderBank: which group
        bool alsoMackie = false;
        juce::MemoryBlock sysex;   // a system message's bytes (F0 ... F7) instead of a controller
        int program = -1;          // ...or a program change's number
    };

    juce::String name;
    juce::StringArray ports;   // the MIDI inputs it's for
    juce::StringArray mackiePorts;   // its Mackie Control ports, if any
    int faders = 8;                  // how many faders it has for Mackie Control
    std::vector<Control> controls;

    /** A profile from its JSON, or nothing if it can't be read (a control
        with an unknown action or type is skipped). */
    static std::optional<ControllerProfile> fromJson (const juce::String&);

    /** True if it's for this MIDI input (ignoring case and the spaces ALSA
        pads names with). */
    bool isFor (const juce::String& portName) const;
    bool isForMackie (const juce::String& portName) const;

    const Control* find (const juce::MidiMessage&) const;

    /** The profiles that come with the app (resources/controllers). */
    static std::vector<ControllerProfile> getBuiltIn();
};

/** Something a mapped control did. */
struct ControllerEvent
{
    ControllerAction action = ControllerAction::play;
    int index = 0;          // soundKnob: which knob
    int controller = 0;     // the control it came from, to tell moves apart (one undo step each)
    float value = 0.0f;     // knobs: 0..1
    int steps = 0;          // encoders: clicks turned, + = to the right
    bool alsoMackie = false; // the control also sends Mackie Control (see ControllerProfile)
};

/** The controller maps in use. A mapped message is swallowed (it never
    reaches the track's instrument) and queued for the app.

    No Tracktion: the engine hands it every message from a MIDI input
    (see IkarosEngineBehaviour::interceptMidiInputMessage), and the app
    collects the events on its timer (see SurfaceHandler). */
class ControllerMap
{
public:
    void setProfiles (std::vector<ControllerProfile>);

    /** The profile used for a MIDI input, if any. */
    std::optional<ControllerProfile> getProfileFor (const juce::String& portName) const;

    /** The profile whose controller has this Mackie Control port, if any. */
    std::optional<ControllerProfile> getProfileForMackiePort (const juce::String& portName) const;

    /** On the MIDI thread: true if the message is mapped (and so swallowed). */
    bool handleMessage (const juce::String& portName, const juce::MidiMessage&);

    /** On the message thread: what happened since the last call. */
    std::vector<ControllerEvent> takeEvents();

private:
    mutable juce::CriticalSection lock;   // the MIDI thread and the message thread share both (mutable: getProfileFor is const)
    std::vector<ControllerProfile> profiles;
    std::vector<ControllerEvent> events;
};

} // namespace ikaros
