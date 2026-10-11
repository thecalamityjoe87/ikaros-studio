#pragma once

// Internal to the arrangement: layout constants and grid helpers shared by
// ArrangementView's own .cpp files. Nothing outside ui/arrangement includes this.

#include "ui/arrangement/ArrangementView.h"
#include "ui/theme/Tokens.h"

namespace ikaros::arrangement
{

inline constexpr int headerWidth = tokens::size::trackHeaderWidth;
// Track rows are geometry.rowHeight tall: it changes with the zoom.
inline constexpr int maxRowHeight = tokens::size::trackRowHeight * 7 / 3;
inline constexpr int dropZoneHeight = 84;
inline constexpr int dropZoneMargin = 12;
inline constexpr int regionTop = 5;           // a region's gap from the top of its lane...
inline constexpr int regionGap = 11;          // ...and the total gap above and below it

// Zoomed far out, drawing every bar would be a solid smear. This finds a
// bar step (1, 2, 4, 8...) that keeps things at least minPixels apart.
inline int barStride (const ArrangementView::Geometry& geo, double minPixels)
{
    const double barWidth = geo.pixelsPerBeat * geo.beatsPerBar;
    int stride = 1;

    while (barWidth * stride < minPixels && stride < 4096)
        stride *= 2;

    return stride;
}

// Calls fn (barIndex, x) for every stride-th bar visible in a lane of the given width.
template <typename Fn>
void forEachVisibleBar (const ArrangementView::Geometry& geo, int width, int stride, Fn&& fn)
{
    const int firstBar = juce::jmax (0, (int) std::floor (geo.scrollBeats / geo.beatsPerBar));
    const int lastBar = (int) std::ceil (geo.xToBeat ((float) width) / geo.beatsPerBar);

    for (int bar = firstBar - firstBar % stride; bar <= lastBar; bar += stride)
        fn (bar, juce::roundToInt (geo.beatToX (bar * geo.beatsPerBar)));
}

} // namespace ikaros::arrangement
