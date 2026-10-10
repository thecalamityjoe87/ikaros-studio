#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace ikaros
{

// Line icons drawn from SVG path data (24×24 viewBox, matching the mockup).
// Drawing them as vector paths keeps them crisp at any display scale.
enum class Icon
{
    library, plus, undo, toStart, play, pause, record, loop, metronome, search,
    keyboards, drums, bass, synths, guitar, strings, loops, mic,
    windowMinimise, windowMaximise, windowRestore, windowClose,
    power, more, chevronDown, chevronRight
};

namespace icons
{
    /** The icon's outline, in a 0..24 coordinate space. */
    const juce::Path& get (Icon);

    /** True for icons that are drawn filled rather than stroked (play, pause, record). */
    bool isFilled (Icon);

    /** Draws an icon centred in `area`, scaled to fit `iconSize` pixels. */
    void draw (juce::Graphics&, Icon, juce::Rectangle<float> area, juce::Colour,
               float iconSize = 18.0f, float strokeWidth = 1.8f);

    /** Maps a library category name ("Keyboards", "Drums", ...) to its icon. */
    Icon forCategory (const juce::String& category);
}

} // namespace ikaros
