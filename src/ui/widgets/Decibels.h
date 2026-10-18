#pragma once

#include <juce_core/juce_core.h>

namespace ikaros
{

/** A volume as the app shows it: "−3.0 dB", "0.0 dB", "+2.5 dB", or
    "−∞ dB" when it's all the way down. Uses a real minus sign (U+2212),
    which lines up with the digits in a monospace font, unlike a hyphen. */
inline juce::String decibelText (float db)
{
    const auto minus = juce::String::fromUTF8 ("\xe2\x88\x92");

    if (db <= -99.0f)
        return minus + juce::String::fromUTF8 ("\xe2\x88\x9e dB");   // −∞

    // Round first, so e.g. -0.04 shows as "0.0 dB", not "−0.0 dB".
    const auto rounded = std::round (db * 10.0f) / 10.0f;
    const auto sign = rounded > 0.0f ? juce::String ("+") : rounded < 0.0f ? minus : juce::String();
    return sign + juce::String (std::abs (rounded), 1) + " dB";
}

} // namespace ikaros
