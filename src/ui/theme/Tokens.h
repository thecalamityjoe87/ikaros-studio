#pragma once

// Design tokens: every colour, font size, corner radius and shared dimension
// the UI uses comes from here, so the look can be tuned (or a whole alternate
// theme tried) in one place without touching layout code.
//
// `inline` variables (C++17) let us define constants in a header without
// multiple-definition link errors: every .cpp that includes this file sees the
// same single object. juce::Colour isn't a literal type, so colours are
// `inline const`; plain numbers can be `inline constexpr`.

#include <juce_graphics/juce_graphics.h>

namespace ikaros::tokens
{

// ------------------------------------------------------------------ palette
// The Milestone 7 makeover palette. These are the target values; the role
// colours in `colour` below are moved onto them phase by phase, and once every
// role points here the old hex values go away.
namespace palette
{
    inline const juce::Colour background      { 0xff15181d };
    inline const juce::Colour panel           { 0xff1b1e24 };
    inline const juce::Colour card            { 0xff232831 };
    inline const juce::Colour button          { 0xff262b34 };
    inline const juce::Colour buttonBorder    { 0xff323844 };
    inline const juce::Colour line            { 0xff2a2f38 };
    inline const juce::Colour lineSoft        { 0xff252a31 };

    inline const juce::Colour text            { 0xffe8eaee };
    inline const juce::Colour textMuted       { 0xffa2a9b4 };
    inline const juce::Colour dim             { 0xff3a404b };
    inline const juce::Colour disabled        { 0xff6b7280 };

    inline const juce::Colour accent          { 0xffffaa2b };   // only ever means "on" (see Phase 2)
    inline const juce::Colour onAccent        { 0xff1d1405 };
    inline const juce::Colour record          { 0xfff05a4f };
    inline const juce::Colour meter           { 0xff3fcf73 };

    inline const juce::Colour selectedBg      { 0xff2c323d };
    inline const juce::Colour selectedOutline { 0xffe8eaee };
    inline const juce::Colour selectedInset   { 0xff4a5260 };   // 1px inset outline on the selected track header
    inline const juce::Colour neutralActive   { 0xff323844 };   // "pressed" fill for non-accent toggles, tabs, segments
    inline const juce::Colour field           { 0xff1c2027 };   // clickable fields in the transport readout
}

// Instrument family colours: tracks, regions, library categories and the
// Smart Controls arcs are coloured by the family of the track's sound.
namespace family
{
    inline const juce::Colour drums     { 0xffe8c547 };
    inline const juce::Colour audio     { 0xff4b93e6 };
    inline const juce::Colour strings   { 0xff33b56b };
    inline const juce::Colour bass      { 0xff9b7bea };
    inline const juce::Colour keys      { 0xff5bc0d9 };
    inline const juce::Colour synths    { 0xffe070b0 };
    inline const juce::Colour guitar    { 0xffe08a4f };
    inline const juce::Colour woodwinds { 0xff7cc4a0 };
    inline const juce::Colour brass     { 0xffd6a35a };
    inline const juce::Colour loops     { 0xffa2a9b4 };
}

// ------------------------------------------------------------------ colours
// Colours by role. Code should use these, not the palette directly.
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

    // The "pressed" fill for neutral toggles, chosen tabs and segments, and
    // the stopped Play button. Not the accent: those aren't "on" states.
    inline const juce::Colour neutralActive  = palette::neutralActive;
    // Border of a chosen tile or card (library category, dialog choices).
    inline const juce::Colour selectedBorder = palette::disabled;
    // Neutral marker for drop targets, insertion lines, marquee boxes,
    // range selections and the keyboard focus ring.
    inline const juce::Colour highlight      = palette::selectedOutline;

    // The selected track, wherever it shows (header, lane, regions, mixer
    // strip). Deliberately neutral: the accent only ever means "on".
    inline const juce::Colour selectedBg      = palette::selectedBg;
    inline const juce::Colour selectedOutline = palette::selectedOutline;
    inline const juce::Colour selectedInset   = palette::selectedInset;
    inline constexpr float selectedLaneTint   = 0.045f;   // alpha of tintLight over the selected lane
    inline const juce::Colour tileBg         { 0xff1c1f25 };   // library category tiles
    inline const juce::Colour trackHeaderBg  { 0xff1f232a };   // an unselected track header
    inline const juce::Colour border         { 0xff2f343e };
    inline const juce::Colour borderSoft     { 0xff262a32 };
    inline const juce::Colour borderStrong   { 0xff3a404b };
    inline const juce::Colour divider        { 0xff000000 };
    inline const juce::Colour lcdBg          { 0xff050607 };
    inline const juce::Colour lcdLabel       { 0xff8f98a6 };

    inline const juce::Colour text           { 0xffeef1f5 };
    inline const juce::Colour textSecondary  { 0xffa3abb8 };
    inline const juce::Colour textMenu       { 0xffc9d0da };   // menu bar items, icon buttons, slider thumbs
    inline const juce::Colour textOnAccent   = palette::onAccent;
    inline const juce::Colour textOnColour   { 0xffffffff };   // icons/text on saturated fills (record, track colours)
    inline const juce::Colour textDisabled   = palette::disabled;   // icons/text of a disabled control (e.g. Redo with nothing to redo)

    // Clickable values in the transport readout (tempo, time signature, key):
    // a shade lighter than the readout, brightened on hover.
    inline const juce::Colour field          = palette::field;
    inline const juce::Colour logoOutline    = palette::buttonBorder;   // keeps the dark logo tile visible on the title bar

    // The accent only ever means "on": active toggles (cycle, metronome,
    // count-in, a muted track's Mute, Flex, open automation), Play while
    // playing, the playhead and the cycle range. Nothing else uses it.
    inline const juce::Colour accent         = palette::accent;
    inline const juce::Colour accentLight    = palette::accent.brighter (0.15f);
    inline const juce::Colour record         { 0xffff5a5a };
    inline const juce::Colour warning        { 0xffffd25a };   // e.g. a plug-in that failed to load
    inline const juce::Colour solo           { 0xffffd25a };

    inline const juce::Colour meterGreen     { 0xff4fd18b };
    inline const juce::Colour meterYellow    { 0xffffd25a };
    inline const juce::Colour meterRed       { 0xffff5a5a };
    inline const juce::Colour effectOn       { 0xff4fd18b };

    inline const juce::Colour pianoWhite     { 0xffe9edf2 };
    inline const juce::Colour pianoWhiteDown = palette::textMuted;   // a pressed key
    inline const juce::Colour pianoBlackDown = palette::disabled;
    inline const juce::Colour pianoBlack     { 0xff1a1d23 };
    inline const juce::Colour pianoLabel     { 0xff5b6270 };

    // Translucent overlays. Grid lines, hover highlights and shading are
    // drawn as tintLight/tintDark with a small alpha at the call site, so a
    // light theme can swap these two and keep the same contrast.
    inline const juce::Colour tintLight      { 0xffffffff };
    inline const juce::Colour tintDark       { 0xff000000 };

    // Dims everything behind a modal dialog.
    inline const juce::Colour scrim          = juce::Colours::black.withAlpha (0.55f);
    inline const juce::Colour scrimStrong    = juce::Colours::black.withAlpha (0.6f);    // session chooser
    inline const juce::Colour scrimLight     = juce::Colours::black.withAlpha (0.45f);   // "Saving…" overlay

    // Track colours (also stored per-track in the session; see TrackController)
    inline const juce::Colour trackBlue      { 0xff5aa9ff };
    inline const juce::Colour trackOrange    { 0xffff8f5a };
    inline const juce::Colour trackGreen     { 0xff4fd18b };
    inline const juce::Colour trackPurple    { 0xffc78bff };
    inline const juce::Colour trackYellow    { 0xffffd25a };

    // Library category colours (see LibrarySidebar)
    inline const juce::Colour categoryKeys      { 0xff5aa9ff };
    inline const juce::Colour categoryDrums     { 0xffff8f5a };
    inline const juce::Colour categoryBass      { 0xff4fd18b };
    inline const juce::Colour categorySynths    { 0xffc78bff };
    inline const juce::Colour categoryGuitar    { 0xffffd25a };
    inline const juce::Colour categoryStrings   { 0xffff7aa8 };
    inline const juce::Colour categoryWoodwinds { 0xff8fd9a8 };
    inline const juce::Colour categoryBrass     { 0xffe6b85c };
    inline const juce::Colour categoryLoops     { 0xffffb547 };
    inline const juce::Colour categoryAudio     { 0xffa3abb8 };
    inline const juce::Colour categoryPlugins   { 0xff6fd6d0 };

    // Automation curves, by what they move (see getAutomationColour). A
    // shade lighter than the track colours, to stand out over regions.
    inline const juce::Colour automationVolume { 0xff7cc0ff };   // blue
    inline const juce::Colour automationPan    { 0xff6fe3a3 };   // green
    inline const juce::Colour automationReverb { 0xffd4a6ff };   // purple
    inline const juce::Colour automationDelay  { 0xffffa877 };   // orange
    inline const juce::Colour automationSound  { 0xffffe07a };   // yellow: the Sound tab's knobs
    inline const juce::Colour automationPlugin { 0xffff8ac6 };   // pink: plug-in parameters
    inline const juce::Colour tempoCurve       = palette::textMuted;   // the tempo lane's line and points

    // A control whose value is driven by automation (needle, thumb, fader
    // cap, its name in the automation picker, the "it's automated" hint).
    inline const juce::Colour automated        { 0xff7cc0ff };

    // The master channel has no instrument family, so it's neutral grey
    // (its knob arcs, its mixer strip's top edge).
    inline const juce::Colour master           = palette::textMuted;

    /** Darker variant of a track colour for region label strips (white text). */
    inline juce::Colour regionStrip (juce::Colour track)
    {
        if (track == trackYellow)
            return track;                       // yellow keeps its colour and uses dark text

        return track.withMultipliedSaturation (1.05f).withMultipliedBrightness (0.6f);
    }

    inline juce::Colour regionStripText (juce::Colour track)
    {
        return track == trackYellow ? textOnAccent : textOnColour;
    }
}

// ------------------------------------------------------------------ shadows
namespace shadow
{
    // A juce::DropShadow is just colour + blur radius + offset; drawForRectangle
    // or drawForPath paints it under a shape.
    inline const juce::DropShadow dialog  { juce::Colours::black.withAlpha (0.6f), 30, { 0, 8 } };
    inline const juce::DropShadow popover { juce::Colours::black.withAlpha (0.5f), 14, { 0, 4 } };
}

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
    inline constexpr int trackRowHeight    = 100;   // grows while the track's automation picker shows (see ArrangementLayout.h)
    inline constexpr int bottomPanelHeight = 290;
    inline constexpr int minHitTarget      = 44;
    inline constexpr int panelPadding      = 14;    // standard inset from a panel's edges
    inline constexpr int scrollbarWidth    = 10;

    inline constexpr float radiusHair      = 2.0f;
    inline constexpr float radiusTiny      = 3.0f;
    inline constexpr float radiusXs        = 4.0f;
    inline constexpr float radiusSmall     = 5.0f;
    inline constexpr float radius          = 8.0f;
    inline constexpr float radiusLarge     = 10.0f;
    inline constexpr float radiusCard      = 12.0f;
    inline constexpr float radiusPill      = 14.0f;
}

} // namespace ikaros::tokens
