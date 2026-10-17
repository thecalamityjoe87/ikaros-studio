#pragma once

// Internal to the arrangement: layout constants and grid helpers shared by
// ArrangementView's own .cpp files. Nothing outside ui/arrangement includes this.

#include "ui/arrangement/ArrangementView.h"
#include "ui/theme/Tokens.h"

namespace ikaros::arrangement
{

inline constexpr int headerWidth = tokens::size::trackHeaderWidth;
// Track rows are geometry.rowHeight tall: it changes with the zoom.
inline constexpr int maxRowHeight = tokens::size::trackRowHeight * 2;
inline constexpr int automationPickerHeight = 18;   // what a row grows by while its automation shows (the picker's line in the header)
inline constexpr int dropZoneHeight = 84;
inline constexpr int dropZoneMargin = 12;
inline constexpr int regionTop = 5;           // a region's gap from the top of its lane...
inline constexpr int regionGap = 11;          // ...and the total gap above and below it

inline constexpr int tempoLaneHeight = 44;     // the tempo lane under the ruler, when it shows

// Zoomed far out, drawing every bar would be a solid smear. This finds a
// bar step (1, 2, 4, 8...) that keeps things at least minPixels apart, going
// by the song's shortest bar.
inline int barStride (const ArrangementView::Geometry& geo, double minPixels)
{
    const double barWidth = geo.pixelsPerBeat * geo.tempo.shortestBar();
    int stride = 1;

    while (barWidth * stride < minPixels && stride < 4096)
        stride *= 2;

    return stride;
}

// Calls fn (barIndex, x) for every stride-th bar visible in a lane of the given width.
template <typename Fn>
void forEachVisibleBar (const ArrangementView::Geometry& geo, int width, int stride, Fn&& fn)
{
    geo.tempo.forEachBar (geo.scrollBeats, geo.xToBeat ((float) width), stride, [&] (int bar, double beat)
    {
        fn (bar, juce::roundToInt (geo.beatToX (beat)));
    });
}

} // namespace ikaros::arrangement
