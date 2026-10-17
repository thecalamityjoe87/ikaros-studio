#pragma once

// The Editor's grid: where notes and region edges snap to in the piano roll
// and the audio editor. It's picked in the Region inspector and shared by
// both editors.

#include "engine/TempoMap.h"
#include <juce_core/juce_core.h>

namespace ikaros
{

enum class EditorGrid { bar, quarter, eighth, sixteenth, thirtySecond, off };

/** How long one grid step is, in beats (0 = off). For the bar grid, pass
    the length of the bar in question (bars can differ in length). */
inline double gridBeats (EditorGrid grid, double beatsPerBar)
{
    switch (grid)
    {
        case EditorGrid::bar:          return beatsPerBar;
        case EditorGrid::quarter:      return 1.0;
        case EditorGrid::eighth:       return 0.5;
        case EditorGrid::sixteenth:    return 0.25;
        case EditorGrid::thirtySecond: return 0.125;
        case EditorGrid::off:          break;
    }

    return 0.0;
}

/** The nearest grid line to a beat on the song's timeline (or, with
    roundDown, the one at or before it). The bar grid follows the song's bar
    lines, from its tempo map. */
inline double snapToEditorGrid (EditorGrid grid, const TempoMap& tempo, double beat, bool roundDown = false)
{
    if (grid == EditorGrid::bar)
        return roundDown ? tempo.barStartAt (beat + 1.0e-9) : tempo.nearestBarLine (beat);

    const double g = gridBeats (grid, 0.0);

    if (g <= 0.0)
        return beat;

    return (roundDown ? std::floor (beat / g + 1.0e-9) : std::round (beat / g)) * g;
}

/** The short name shown on the Grid button. */
inline juce::String gridName (EditorGrid grid)
{
    switch (grid)
    {
        case EditorGrid::bar:          return "Bar";
        case EditorGrid::quarter:      return "1/4";
        case EditorGrid::eighth:       return "1/8";
        case EditorGrid::sixteenth:    return "1/16";
        case EditorGrid::thirtySecond: return "1/32";
        case EditorGrid::off:          break;
    }

    return "Off";
}

} // namespace ikaros
