#pragma once

// Design tokens: every font size, corner radius and shared dimension the UI
// uses comes from here, so the look can be tuned in one place without
// touching layout code. Colours change with the theme, so they live in
// Theme.h; this file includes it so one include gives you both.
//
// `inline constexpr` variables (C++17) let us define constants in a header
// without multiple-definition link errors: every .cpp that includes this
// file sees the same single object.

#include "ui/theme/Theme.h"

namespace ikaros::tokens
{

// ------------------------------------------------------------------ text sizes (px)
// Pass these to the ikaros::fonts functions, e.g. fonts::ui (textSize::md).
// 11px is the smallest size for UI text; only the transport readout's caption
// labels (BAR · BEAT, BPM, TIME, KEY) may go below it.
namespace textSize
{
    inline constexpr float caption = 9.5f;    // transport readout captions (the only text allowed below 11px)
    inline constexpr float tiny    = 10.0f;   // TODO(milestone-7): below the 11px minimum
    inline constexpr float xs      = 11.0f;
    inline constexpr float sm      = 12.0f;
    inline constexpr float smPlus  = 12.5f;
    inline constexpr float md      = 13.0f;
    inline constexpr float mdPlus  = 13.5f;
    inline constexpr float lg      = 14.0f;
    inline constexpr float lgPlus  = 14.5f;
    inline constexpr float xl      = 15.0f;
    inline constexpr float xxl     = 16.0f;
    inline constexpr float title   = 18.0f;   // dialog titles
    inline constexpr float display = 20.0f;
    inline constexpr float lcd     = 22.0f;   // the transport position readout
    inline constexpr float hero    = 26.0f;
}

// ------------------------------------------------------------------ sizes (px)
namespace size
{
    inline constexpr int menuBarHeight     = 30;
    inline constexpr int logoSize          = 18;    // the app logo in the title bar
    inline constexpr int toolbarHeight     = 64;
    inline constexpr int sidebarWidth      = 264;   // 250–280 in the spec
    inline constexpr int rulerHeight       = 30;
    inline constexpr int trackHeaderWidth  = 320;   // also the width of the track inspector below, so they line up
    // Track rows: View > Compact Tracks (the default) or Tall Tracks. Rows
    // grow when zoomed in, and while the track's automation picker shows
    // (see ArrangementLayout.h).
    inline constexpr int trackRowCompact   = 84;
    inline constexpr int trackRowTall      = 100;
    inline constexpr int bottomPanelHeight = 290;
    inline constexpr int minHitTarget      = 44;
    inline constexpr int panelPadding      = 14;    // standard inset from a panel's edges
    inline constexpr int scrollbarWidth    = 10;

    // The library sidebar
    inline constexpr int libraryTileHeight = 50;    // category tiles, icon over label, 4 to a row
    inline constexpr int libraryRowHeight  = 36;    // a sound (or file, plug-in, pack) in the list
    inline constexpr int libraryRowInset   = 12;    // text inside a row; the breadcrumb and count line up with it
    inline constexpr int roundButton       = 24;    // a row's round preview or download button

    inline constexpr float radiusHair      = 2.0f;
    inline constexpr float radiusTiny      = 3.0f;
    inline constexpr float radiusXs        = 4.0f;
    inline constexpr float radiusSmall     = 5.0f;
    inline constexpr float radiusMedium    = 6.0f;   // track icon tiles
    inline constexpr float radius          = 8.0f;
    inline constexpr float radiusLarge     = 10.0f;
    inline constexpr float radiusCard      = 12.0f;
    inline constexpr float radiusPill      = 14.0f;
}

} // namespace ikaros::tokens
