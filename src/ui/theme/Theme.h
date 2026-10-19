#pragma once

// The app's colours, as two palettes: dark and light. Every colour the UI
// paints with comes from theme(), the active palette, and is read while
// painting (never kept in a member), so switching themes only needs a
// repaint. Sizes, radii and text sizes don't change with the theme: they
// stay in Tokens.h.
//
// The palette has two groups:
//   - chrome: everything around the arrangement (title bar, toolbar, library,
//     track headers, ruler, bottom panel, inspector, dialogs). These are the
//     plain members of ThemePalette and change between light and dark.
//   - arrangement: the track lanes, grid, regions, playhead and cycle range
//     (ThemePalette::arrangement). Like GarageBand, the arrangement stays
//     dark in both themes; only a few shades differ.
//
// Instrument family colours (regions, track colours) are the same in both
// themes. Their "ink" shades are for icons and arcs drawn on a chrome
// surface: deeper in light mode so they read on light grey, and equal to
// the family colours in dark mode.

#include "ui/theme/Family.h"
#include <juce_graphics/juce_graphics.h>
#include <array>

namespace ikaros
{

struct ThemePalette
{
    //==========================================================================
    // Chrome: surfaces, one per area of the window

    juce::Colour windowBg;            // behind everything
    juce::Colour titleBarBg, titleBarBorder;
    juce::Colour toolbarBg, toolbarBorder;
    juce::Colour transportWell, transportWellBorder;   // the group around the transport buttons
    juce::Colour librarySidebarBg;
    juce::Colour tracksBarBg;         // "TRACKS" above the track headers
    juce::Colour sectionsRowBg;       // "SECTIONS" under it
    juce::Colour trackColumnBg;       // the track headers column
    juce::Colour trackRowLine;        // between track headers
    juce::Colour rulerBg;
    juce::Colour sectionsStripBg;     // the section blocks' strip above the lanes
    juce::Colour bottomPanelBg, bottomPanelBorder;
    juce::Colour inspectorBg;         // the inspector column in the bottom panel
    juce::Colour divider;             // general lines between areas (the library's and inspector's right edges...)
    juce::Colour dialogBg;            // a dialog's panel (Preferences, Export, About...)
    juce::Colour dialogSidebarBg;     // a dialog's side column (Preferences' categories, the session chooser's)
    juce::Colour popupBg;             // popup menus, popovers, tooltips' panels

    // Generic surfaces inside the areas
    juce::Colour wellBg;              // inset wells (popover segment wells, the fader slot)
    juce::Colour cardBg;              // effect rows, mixer strips, message cards
    juce::Colour raised;              // raised buttons
    juce::Colour controlFill;         // inactive pills, the tempo lane's chips
    juce::Colour selectedRow;
    juce::Colour tileBg;              // an unselected library category tile
    juce::Colour tileBorder;
    juce::Colour rowHover, rowPressed;   // a library sound row under the mouse

    // The "pressed" fill for neutral toggles, chosen tabs and segments, and
    // the stopped Play button. Not the accent: those aren't "on" states.
    juce::Colour neutralActive, neutralActiveBorder;
    juce::Colour selectedBorder;      // border of a chosen tile or card
    juce::Colour selectedTileShadow;  // under the chosen library category (transparent: none)
    juce::Colour highlight;           // drop targets, insertion lines, marquee boxes, the focus ring

    // The selected track, wherever it shows (header, mixer strip). Neutral:
    // the accent only ever means "on".
    juce::Colour selectedBg, selectedOutline, selectedInset;
    float trackTileAlpha = 0.16f;     // a track's icon tile: its family colour at this alpha

    // Lines and borders
    juce::Colour border, borderSoft, borderStrong;
    juce::Colour outline;             // dashed "add" outlines

    // Small square buttons (a track's Mute, Solo...) and the record-arm button
    juce::Colour buttonFill, buttonBorder, buttonIcon;
    juce::Colour raisedBorder;        // around raised buttons (transparent: none)
    juce::Colour armBorder;           // an unarmed record button's border

    // Segmented controls (the bottom panel's tabs, About's tabs): a well with
    // the chosen segment as a pill inside it.
    juce::Colour segmentWell, segmentWellBorder, segmentSelected, segmentShadow;

    // Text fields (search, rename) and the clickable transport values
    juce::Colour inputBg, inputBorder;
    juce::Colour field;               // tempo, time signature and key in the transport readout

    // The transport readout
    juce::Colour lcdBg, lcdBorder, lcdLabel;
    juce::Colour lcdValue;            // the bar · beat position

    // Text
    juce::Colour text, textSecondary;
    juce::Colour textSoft;            // on a dashed "add" button
    juce::Colour textMenu;            // menu bar items, ghost icon buttons
    juce::Colour textOnAccent;
    juce::Colour textOnColour;        // on saturated fills (record, track colours)
    juce::Colour textDisabled;

    // Sliders: a linear slider's track and fill, and its round thumb (the
    // zoom sliders, the Region inspector's).
    juce::Colour sliderTrack, sliderFill;
    juce::Colour sliderThumb, thumbBorder, thumbShadow;
    // The volume sliders with a level meter in them (MeterSlider): the
    // master's in the toolbar uses sliderTrack, the track headers' this one.
    juce::Colour volumeSliderTrack;
    juce::Colour thumb, meterThumbRing;
    juce::Colour faderCap;            // the mixer's vertical faders

    // Knobs: the big Smart Controls knobs and the compact ones (pan, sends).
    juce::Colour knobBody, knobRim, knobPointer, knobTrack;
    juce::Colour compactKnobBody, compactKnobRim, compactKnobPointer, compactKnobTrack;

    // Smart Controls cards
    juce::Colour smartCardBg, smartCardBorder, smartCardShadow;

    // The on-screen keyboard
    juce::Colour keyboardTray, keyboardTrayBorder;
    juce::Colour pianoWhite, pianoWhiteDown, pianoKeyDivider;   // the divider: the gap between white keys
    juce::Colour pianoBlack, pianoBlackDown, pianoBlackLip;     // the lip: a strip along a black key's bottom
    juce::Colour pianoLabel;
    juce::Colour rollKeyWhite, rollKeyBlack;   // the piano roll's keys column

    // The round play button on a hovered library sound
    juce::Colour previewFill, previewIcon;

    juce::Colour logoOutline;         // keeps the dark logo tile visible on a dark title bar

    // The accent only ever means "on": active toggles (cycle, metronome,
    // count-in, a muted track's Mute, Flex, open automation), Play while
    // playing. Nothing else uses it.
    juce::Colour accent, accentLight, accentBorder;
    juce::Colour record, warning, solo;
    juce::Colour meterGreen, meterYellow, meterRed;
    juce::Colour effectOn;
    juce::Colour realtimeBadgeText, realtimeBadgeBg;   // "RT" under the CPU readout

    // Translucent overlays: hover highlights and shading are tintLight or
    // tintDark with a small alpha at the call site.
    juce::Colour tintLight, tintDark;

    // Dims everything behind a modal dialog.
    juce::Colour scrim, scrimStrong, scrimLight;

    juce::DropShadow dialogShadow, popoverShadow;

    // The EQ's four bands, low to high.
    std::array<juce::Colour, 4> eqBands;

    // Automation curves, by what they move (see getAutomationColour).
    juce::Colour automationVolume, automationPan, automationReverb,
                 automationDelay, automationSound, automationPlugin;

    // A control whose value is driven by automation (needle, thumb, fader
    // cap, its name in the automation picker, the "it's automated" hint).
    juce::Colour automated;

    //==========================================================================
    // Instrument families

    /** A family's colour: regions, track colours, the Smart Controls arcs.
        The same in both themes. */
    juce::Colour family (tokens::Family f) const      { return familyColours[(size_t) f]; }

    /** A family's colour for icons and arcs drawn on a chrome surface. */
    juce::Colour ink (tokens::Family f) const         { return inkColours[(size_t) f]; }

    /** A Smart Controls knob's value arc (on a card). */
    juce::Colour knobArc (tokens::Family f) const     { return knobArcColours[(size_t) f]; }

    static constexpr size_t numFamilies = (size_t) tokens::Family::master + 1;
    std::array<juce::Colour, numFamilies> familyColours, inkColours, knobArcColours;

    //==========================================================================
    // The arrangement: stays dark in both themes.
    struct Arrangement
    {
        juce::Colour laneBg, laneDivider;
        juce::Colour gridBar, gridBeat;      // already translucent
        juce::Colour selectedLane;           // laid over the selected track's lane
        juce::Colour text, textSecondary;    // labels drawn over the lanes
        juce::Colour outline;                // the dashed drop zone under the tracks
        juce::Colour highlight;              // drop targets and marquee boxes on the lanes
        juce::Colour selectionOutline;       // around the selected track's regions
        juce::Colour tintLight, tintDark;    // hover and shading over the lanes
        juce::Colour bubble;                 // a value shown while dragging an automation point
        juce::Colour tempoCurve;
        juce::Colour chipFill, chipLitFill, chipBorder;   // time-signature chips in the tempo lane
        juce::Colour playhead, cycle;
        juce::Colour record;                 // a region being recorded
        juce::Colour scrollbar;
        juce::Colour regionTextDark, regionTextLight;     // a region's name on its header strip
        juce::Colour strip;                  // the piano roll's velocity strip

        // Region bodies are the family colour at these alphas over the lane;
        // their headers are the full colour.
        float regionBodyAlpha = 0.2f, regionBodySelectedAlpha = 0.28f;
    };

    Arrangement arrangement;

    bool isLight = false;
};

/** The two palettes. Built once, the first time they're asked for. */
const ThemePalette& darkPalette();
const ThemePalette& lightPalette();

/** The active palette: paint with theme().text, theme().arrangement.laneBg... */
const ThemePalette& theme();

/** Makes a palette the active one. Doesn't repaint anything by itself. */
void setActivePalette (const ThemePalette&);

} // namespace ikaros
