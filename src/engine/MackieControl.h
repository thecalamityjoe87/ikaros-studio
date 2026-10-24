#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <optional>

/** The Mackie Control (MCU) protocol: what a control surface's buttons,
    faders and knobs send, and what turns its lights on and moves its motor
    faders. Most controllers with a "DAW" or "MCU" mode speak it, in the
    layout Logic and Cubase use.

    Just the messages, no devices and no Tracktion (see ControlSurface), so
    the tests can check it with made-up messages.

    A surface has 8 channel strips, each with a fader, a knob (a "V-Pot",
    turned in clicks rather than to a position) and Rec / Solo / Mute /
    Select buttons, plus a master fader and the transport buttons. Buttons
    are note-ons on channel 1 (velocity 127 = pressed, 0 = released), faders
    are pitch bends (channel 1-8 = strips, 9 = master), knobs and the jog
    wheel are controllers that count clicks. The host lights a button by
    sending its note back. */
namespace ikaros::mackie
{

constexpr int numStrips = 8;
constexpr int masterStrip = 8;   // Event::strip for the master fader

// The buttons' note numbers. The strip buttons are the first strip's
// number plus the strip (0-7).
namespace note
{
    enum : int
    {
        recArm       = 0x00,
        solo         = 0x08,
        mute         = 0x10,
        select       = 0x18,
        vpotPush     = 0x20,
        bankLeft     = 0x2e,
        bankRight    = 0x2f,
        channelLeft  = 0x30,
        channelRight = 0x31,
        save         = 0x50,
        undo         = 0x51,
        cycle        = 0x56,
        click        = 0x59,
        rewind       = 0x5b,
        fastForward  = 0x5c,
        stop         = 0x5d,
        play         = 0x5e,
        record       = 0x5f,
        up           = 0x60,
        down         = 0x61,
        faderTouch   = 0x68,   // + strip; 0x70 = master. Pressed while a finger is on a touch-sensitive fader.
    };
}

struct Event
{
    enum class Type
    {
        // Buttons (only presses are reported, except faderTouch)
        play, stop, record, cycle, click, rewind, fastForward, undo, save,
        bankLeft, bankRight, channelLeft, channelRight, up, down,
        select, mute, solo, recArm, vpotPush,
        faderTouch,

        // Faders and knobs
        fader,
        vpot,
        jog,
    };

    Type type = Type::play;
    int strip = -1;           // 0-7 for a strip's controls; masterStrip for the master fader
    bool pressed = true;      // faderTouch: false = the finger came off
    float position = 0.0f;    // fader: 0..1
    int steps = 0;            // vpot, jog: clicks turned, + = clockwise
};

/** What a message from a surface means, or nothing for a button release,
    a button we don't use, or anything that isn't Mackie Control. */
std::optional<Event> decode (const juce::MidiMessage&);

/** Turns a button's light on or off (note: see mackie::note). */
juce::MidiMessage light (int noteNumber, bool on);

/** Moves a motor fader (strip 0-7, or masterStrip) to a position 0..1. */
juce::MidiMessage faderPosition (int strip, float position);

/** Shows a pan (-1..1) on a knob's ring of lights (strip 0-7). */
juce::MidiMessage panRing (int strip, float pan);

/** Turns a knob's ring of lights off (a strip with no track). */
juce::MidiMessage ringOff (int strip);

} // namespace ikaros::mackie
