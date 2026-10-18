#pragma once

// The Editor's grid: where notes and region edges snap to in the piano roll
// and the audio editor, and what Quantize lines things up with. It's picked
// in the Region inspector and shared by both editors.

#include "engine/Quantize.h"
#include <juce_core/juce_core.h>

namespace ikaros
{

enum class EditorGrid { bar, quarter, eighth, sixteenth, thirtySecond, off };

/** The grid as a note length in whole notes (1/16 = sixteenth notes), as
    QuantizeSettings::note takes it: 0 = bar lines, -1 = off. */
inline double gridNote (EditorGrid grid)
{
    switch (grid)
    {
        case EditorGrid::bar:          return 0.0;
        case EditorGrid::quarter:      return 1.0 / 4.0;
        case EditorGrid::eighth:       return 1.0 / 8.0;
        case EditorGrid::sixteenth:    return 1.0 / 16.0;
        case EditorGrid::thirtySecond: return 1.0 / 32.0;
        case EditorGrid::off:          break;
    }

    return -1.0;
}

/** How long one grid step is, in beats, in the bar that holds `beat` (0 =
    off). A beat is one count of the time signature, so a sixteenth note is
    a quarter of a beat in 4/4 but half of one in 6/8. */
inline double gridBeats (EditorGrid grid, const TempoMap& tempo, double beat)
{
    if (grid == EditorGrid::bar)
        return tempo.beatsPerBarAt (beat);

    const double note = gridNote (grid);
    return note > 0.0 ? quantize::stepBeatsAt (note, tempo, beat) : 0.0;
}

/** The nearest grid line to a beat on the song's timeline (or, with
    roundDown, the one at or before it). The lines follow the song's bars
    and time signatures, from its tempo map, the same way Quantize does. */
inline double snapToEditorGrid (EditorGrid grid, const TempoMap& tempo, double beat, bool roundDown = false)
{
    if (grid == EditorGrid::off)
        return beat;

    QuantizeSettings straight;
    straight.note = gridNote (grid);
    return quantize::gridLine (beat, straight, tempo, roundDown);
}

/** What Quantize uses for this grid, with the Editor's strength and swing.
    With the grid off, Quantize uses sixteenth notes. */
inline QuantizeSettings quantizeSettingsFor (EditorGrid grid, double strength, double swing)
{
    QuantizeSettings settings;
    settings.note = grid == EditorGrid::off ? 1.0 / 16.0 : gridNote (grid);
    settings.strength = strength;
    settings.swing = swing;
    return settings;
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
