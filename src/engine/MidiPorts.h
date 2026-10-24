#pragma once

#include <juce_core/juce_core.h>

namespace ikaros
{

/** True for a controller's MIDI port that carries DAW remote control rather
    than notes, going by its name. Many controllers show up as several ports:
    the MiniLab 3 as "Minilab3 MIDI" (keys, pads, knobs), "Minilab3 MCU/HUI"
    (transport pads in DAW mode), "Minilab3 DIN THRU" and "Minilab3 ALV"
    (Analog Lab's screen). Only the first should play instruments: routing
    the others too makes the transport pads play notes.

    No Tracktion here, so the tests can check real port names. */
inline bool isControlSurfacePort (const juce::String& portName)
{
    // Whole words only, so a name that merely contains "daw" isn't caught.
    // ALSA pads names with spaces, and "MCU/HUI" is two words.
    const auto words = juce::StringArray::fromTokens (portName.toUpperCase(), " /-_()", {});

    for (const auto* word : { "MCU", "HUI", "DAW", "MACKIE", "ALV" })
        if (words.contains (word))
            return true;

    // A MIDI-out jack's port: nothing to play from.
    return words.contains ("DIN") && words.contains ("THRU");
}

/** True for a port that speaks Mackie Control (see ControlSurface), e.g.
    "Minilab3 MCU/HUI". */
inline bool isMackiePort (const juce::String& portName)
{
    const auto words = juce::StringArray::fromTokens (portName.toUpperCase(), " /-_()", {});
    return words.contains ("MCU") || words.contains ("MACKIE");
}

} // namespace ikaros
