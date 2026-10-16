#pragma once

// Design tokens: every colour and size the UI uses comes from here, so the look
// can be tuned in one place. `inline constexpr` (C++17) lets us define constants
// in a header without multiple-definition link errors.

#include <juce_graphics/juce_graphics.h>

namespace ikaros::tokens
{

// ------------------------------------------------------------------ colours
namespace colour
{
    inline const juce::Colour windowBg       { 0xff121418 };
    inline const juce::Colour toolbarBg      { 0xff1a1d23 };   // toolbar + bottom panel
    inline const juce::Colour sidebarBg      { 0xff171a1f };   // library + track headers + ruler
    inline const juce::Colour menuBarBg      { 0xff0e1013 };
    inline const juce::Colour wellBg         { 0xff0e1013 };   // transport pill, search field, inset wells
    inline const juce::Colour cardBg         { 0xff15181d };   // knob tiles, effect rows, mixer strips
    inline const juce::Colour raised         { 0xff22262e };   // raised buttons
    inline const juce::Colour controlFill    { 0xff2a2f38 };   // slider tracks, inactive pills
    inline const juce::Colour selectedRow    { 0xff242831 };
    inline const juce::Colour border         { 0xff2f343e };
    inline const juce::Colour borderSoft     { 0xff262a32 };
    inline const juce::Colour borderStrong   { 0xff3a404b };
    inline const juce::Colour divider        { 0xff000000 };
    inline const juce::Colour lcdBg          { 0xff050607 };
    inline const juce::Colour lcdLabel       { 0xff8f98a6 };

    inline const juce::Colour text           { 0xffeef1f5 };
    inline const juce::Colour textSecondary  { 0xffa3abb8 };
    inline const juce::Colour textMenu       { 0xffc9d0da };
    inline const juce::Colour textOnAccent   { 0xff121418 };

    inline const juce::Colour accent         { 0xffffb547 };
    inline const juce::Colour accentLight    { 0xffffc46b };
    inline const juce::Colour record         { 0xffff5a5a };
    inline const juce::Colour mute           { 0xffff8f5a };
    inline const juce::Colour solo           { 0xffffd25a };

    inline const juce::Colour meterGreen     { 0xff4fd18b };
    inline const juce::Colour meterYellow    { 0xffffd25a };
    inline const juce::Colour meterRed       { 0xffff5a5a };
    inline const juce::Colour effectOn       { 0xff4fd18b };

    inline const juce::Colour pianoWhite     { 0xffe9edf2 };
    inline const juce::Colour pianoWhiteLit  { 0xffffd79a };
    inline const juce::Colour pianoBlack     { 0xff1a1d23 };
    inline const juce::Colour pianoLabel     { 0xff5b6270 };

    // Track colours (also stored per-track in the session; see TrackController)
    inline const juce::Colour trackBlue      { 0xff5aa9ff };
    inline const juce::Colour trackOrange    { 0xffff8f5a };
    inline const juce::Colour trackGreen     { 0xff4fd18b };
    inline const juce::Colour trackPurple    { 0xffc78bff };
    inline const juce::Colour trackYellow    { 0xffffd25a };

    // Automation curves, by what they move (see getAutomationColour). A
    // shade lighter than the track colours, to stand out over regions.
    inline const juce::Colour automationVolume { 0xff7cc0ff };   // blue
    inline const juce::Colour automationPan    { 0xff6fe3a3 };   // green
    inline const juce::Colour automationReverb { 0xffd4a6ff };   // purple
    inline const juce::Colour automationDelay  { 0xffffa877 };   // orange
    inline const juce::Colour automationSound  { 0xffffe07a };   // yellow: the Sound tab's knobs
    inline const juce::Colour automationPlugin { 0xffff8ac6 };   // pink: plug-in parameters

    /** Darker variant of a track colour for region label strips (white text). */
    inline juce::Colour regionStrip (juce::Colour track)
    {
        if (track == trackYellow)
            return track;                       // yellow keeps its colour and uses dark text

        return track.withMultipliedSaturation (1.05f).withMultipliedBrightness (0.6f);
    }

    inline juce::Colour regionStripText (juce::Colour track)
    {
        return track == trackYellow ? textOnAccent : juce::Colours::white;
    }
}

// ------------------------------------------------------------------ sizes (px)
namespace size
{
    inline constexpr int menuBarHeight     = 30;
    inline constexpr int toolbarHeight     = 64;
    inline constexpr int sidebarWidth      = 264;   // 250–280 in the spec
    inline constexpr int rulerHeight       = 30;
    inline constexpr int trackHeaderWidth  = 320;   // also the width of the track inspector below, so they line up
    inline constexpr int trackRowHeight    = 100;   // grows while the track's automation picker shows (see ArrangementLayout.h)
    inline constexpr int bottomPanelHeight = 290;
    inline constexpr int minHitTarget      = 44;

    inline constexpr float radiusSmall     = 5.0f;
    inline constexpr float radius          = 8.0f;
    inline constexpr float radiusLarge     = 10.0f;
    inline constexpr float radiusPill      = 14.0f;
}

} // namespace ikaros::tokens
