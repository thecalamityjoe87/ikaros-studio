#pragma once

// Internal to PianoRoll: layout constants and small helpers shared by its own
// .cpp files (PianoRoll, PianoRollGestures, PianoRollPainting). Nothing else
// includes this.

#include "engine/EngineTypes.h"
#include <algorithm>

namespace ikaros::pianoroll
{

inline constexpr int keysWidth = 44;
inline constexpr int toolbarHeight = 36;
inline constexpr int velocityHeight = 52;
inline constexpr int scrollBarSize = 10;
inline constexpr int numRows = 128;
inline constexpr int dragThreshold = 3;
inline constexpr int resizeHandle = 6;          // pixels at a note's right end that resize it

inline constexpr double minPixelsPerBeat = 8.0, maxPixelsPerBeat = 400.0;
inline constexpr float minRowHeight = 6.0f, maxRowHeight = 28.0f;

inline bool isBlackKey (int note)
{
    const int n = note % 12;
    return n == 1 || n == 3 || n == 6 || n == 8 || n == 10;
}

inline bool sameNotes (const std::vector<NoteInfo>& a, const std::vector<NoteInfo>& b)
{
    // std::equal with a lambda: compares the two lists element by element.
    return std::equal (a.begin(), a.end(), b.begin(), b.end(), [] (const NoteInfo& x, const NoteInfo& y)
    {
        return x.pitch == y.pitch && x.startBeat == y.startBeat
            && x.lengthBeats == y.lengthBeats && x.velocity == y.velocity;
    });
}

} // namespace ikaros::pianoroll
