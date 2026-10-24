#include "engine/MackieControl.h"

namespace ikaros::mackie
{

namespace
{
    // Knobs and the jog wheel send how far they turned: bit 6 set = to the
    // left, the bits below it = how many clicks.
    int clicks (int value)
    {
        const int amount = value & 0x3f;
        return (value & 0x40) != 0 ? -amount : amount;
    }

    std::optional<Event> button (int noteNumber)
    {
        using T = Event::Type;

        auto stripButton = [noteNumber] (T type, int first) -> std::optional<Event>
        {
            Event e;
            e.type = type;
            e.strip = noteNumber - first;
            return e;
        };

        if (noteNumber >= note::recArm   && noteNumber < note::recArm + numStrips)   return stripButton (T::recArm, note::recArm);
        if (noteNumber >= note::solo     && noteNumber < note::solo + numStrips)     return stripButton (T::solo, note::solo);
        if (noteNumber >= note::mute     && noteNumber < note::mute + numStrips)     return stripButton (T::mute, note::mute);
        if (noteNumber >= note::select   && noteNumber < note::select + numStrips)   return stripButton (T::select, note::select);
        if (noteNumber >= note::vpotPush && noteNumber < note::vpotPush + numStrips) return stripButton (T::vpotPush, note::vpotPush);

        static const std::pair<int, T> transport[]
        {
            { note::bankLeft, T::bankLeft },        { note::bankRight, T::bankRight },
            { note::channelLeft, T::channelLeft },  { note::channelRight, T::channelRight },
            { note::save, T::save },                { note::undo, T::undo },
            { note::cycle, T::cycle },              { note::click, T::click },
            { note::rewind, T::rewind },            { note::fastForward, T::fastForward },
            { note::stop, T::stop },                { note::play, T::play },
            { note::record, T::record },            { note::up, T::up },
            { note::down, T::down },
        };

        for (auto [number, type] : transport)   // a "structured binding": unpacks each pair
            if (number == noteNumber)
                return Event { type };

        return std::nullopt;
    }
}

std::optional<Event> decode (const juce::MidiMessage& m)
{
    // Faders: pitch bend, channel 1-8 = strips, 9 = master.
    if (m.isPitchWheel() && m.getChannel() <= masterStrip + 1)
    {
        Event e;
        e.type = Event::Type::fader;
        e.strip = m.getChannel() - 1;
        // 0x3f70, not 0x3fff: what Tracktion's own MCU support uses, as some
        // faders don't reach the very top.
        e.position = juce::jlimit (0.0f, 1.0f, (float) m.getPitchWheelValue() / (float) 0x3f70);
        return e;
    }

    if (m.isController() && m.getChannel() == 1)
    {
        const int number = m.getControllerNumber();

        if (number >= 0x10 && number < 0x10 + numStrips)
        {
            Event e;
            e.type = Event::Type::vpot;
            e.strip = number - 0x10;
            e.steps = clicks (m.getControllerValue());
            return e;
        }

        if (number == 0x3c)
        {
            Event e;
            e.type = Event::Type::jog;
            e.steps = clicks (m.getControllerValue());
            return e;
        }

        return std::nullopt;
    }

    // Buttons. A note-on with velocity 0 counts as a note-off in JUCE.
    if ((m.isNoteOn() || m.isNoteOff()) && m.getChannel() == 1)
    {
        const int number = m.getNoteNumber();
        const bool pressed = m.isNoteOn();

        if (number >= note::faderTouch && number <= note::faderTouch + masterStrip)
        {
            Event e;
            e.type = Event::Type::faderTouch;
            e.strip = number - note::faderTouch;
            e.pressed = pressed;
            return e;
        }

        if (pressed)
            return button (number);
    }

    return std::nullopt;
}

juce::MidiMessage light (int noteNumber, bool on)
{
    return juce::MidiMessage::noteOn (1, noteNumber, (juce::uint8) (on ? 0x7f : 0));
}

juce::MidiMessage faderPosition (int strip, float position)
{
    return juce::MidiMessage::pitchWheel (strip + 1, juce::jlimit (0, 0x3fff, juce::roundToInt (position * 0x3fff)));
}

juce::MidiMessage panRing (int strip, float pan)
{
    // The ring's 11 lights, 6 = the middle one (one light lit: "dot" mode).
    return juce::MidiMessage::controllerEvent (1, 0x30 + strip, juce::jlimit (1, 11, 6 + juce::roundToInt (5.0f * pan)));
}

juce::MidiMessage ringOff (int strip)
{
    return juce::MidiMessage::controllerEvent (1, 0x30 + strip, 0);
}

} // namespace ikaros::mackie
