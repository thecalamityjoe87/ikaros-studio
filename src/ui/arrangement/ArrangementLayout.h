#pragma once

// Internal to the arrangement: layout constants and grid helpers shared by
// ArrangementView's own .cpp files. Nothing outside ui/arrangement includes this.

#include "ui/arrangement/ArrangementView.h"
#include "ui/theme/Fonts.h"
#include "ui/theme/Tokens.h"

namespace ikaros::arrangement
{

inline constexpr int headerWidth = tokens::size::trackHeaderWidth;
// Track rows are geometry.rowHeight tall: it changes with the zoom.
inline constexpr int maxRowHeight = tokens::size::trackRowTall * 2;
inline constexpr int automationPickerHeight = 18;   // what a row grows by while its automation shows (the picker's line in the header)
inline constexpr int dropZoneHeight = 84;
inline constexpr int dropZoneMargin = 12;
inline constexpr int regionTop = 5;           // a region's gap from the top of its lane...
inline constexpr int regionGap = 11;          // ...and the total gap above and below it

inline constexpr int tempoLaneHeight = 44;     // the tempo lane under the ruler, when it shows
inline constexpr int sectionStripHeight = 24;  // the marker strip with the song's sections, above the tracks

// The headers column: everything in it (the TRACKS and SECTIONS bars, the
// track headers, "Add track") is inset this much from its left and right
// edges, so their edges line up.
inline constexpr int columnPadding = tokens::size::panelPadding;
inline constexpr int columnRight = headerWidth - 1 - columnPadding;   // the right edge (the column's last pixel is a divider)
inline constexpr int addTrackGap = 8;                                  // above and below the "Add track" button

// The "TRACKS" and "SECTIONS" labels.
inline juce::Font columnLabelFont()
{
    return fonts::uiSemiBold (tokens::textSize::xs).withExtraKerningFactor (0.1f);
}

inline int columnLabelWidth (const juce::String& text)
{
    return (int) std::ceil (juce::GlyphArrangement::getStringWidth (columnLabelFont(), text));
}

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
