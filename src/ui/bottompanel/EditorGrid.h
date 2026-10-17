#pragma once

// The Editor's grid: where notes and region edges snap to in the piano roll
// and the audio editor. It's picked in the Region inspector and shared by
// both editors.

#include <juce_core/juce_core.h>

namespace ikaros
{

enum class EditorGrid { bar, quarter, eighth, sixteenth, thirtySecond, off };

/** How long one grid step is, in beats (0 = off). */
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
