#include "ui/theme/Theme.h"

namespace ikaros
{

namespace
{
    // juce::Colour (0xAARRGGBB): alpha first, then red, green, blue.
    juce::Colour hex (juce::uint32 argb)   { return juce::Colour (argb); }

    const juce::Colour none = juce::Colours::transparentBlack;

    //==========================================================================
    ThemePalette makeDark()
    {
        ThemePalette p;

        // Surfaces
        p.windowBg            = hex (0xff121418);
        p.titleBarBg          = hex (0xff0e1013);
        p.titleBarBorder      = hex (0xff000000);
        p.toolbarBg           = hex (0xff1a1d23);
        p.toolbarBorder       = hex (0xff000000);
        p.transportWell       = hex (0xff0e1013);
        p.transportWellBorder = hex (0xff262a32);
        p.librarySidebarBg    = hex (0xff171a1f);
        p.tracksBarBg         = hex (0xff1b1e24);
        p.sectionsRowBg       = hex (0xff1b1e24);
        p.trackColumnBg       = hex (0xff1b1e24);
        p.trackRowLine        = hex (0xff252a31);
        p.rulerBg             = hex (0xff171a1f);
        p.sectionsStripBg     = hex (0xff121418);
        p.bottomPanelBg       = hex (0xff1a1d23);
        p.bottomPanelBorder   = hex (0xff000000);
        p.inspectorBg         = hex (0xff171a1f);
        p.divider             = hex (0xff000000);
        p.dialogBg            = hex (0xff1a1d23);
        p.dialogSidebarBg     = hex (0xff171a1f);
        p.popupBg             = hex (0xff1a1d23);

        p.wellBg              = hex (0xff0e1013);
        p.cardBg              = hex (0xff15181d);
        p.raised              = hex (0xff22262e);
        p.controlFill         = hex (0xff2a2f38);
        p.selectedRow         = hex (0xff242831);
        p.tileBg              = hex (0xff1c1f25);
        p.tileBorder          = hex (0xff262a32);
        p.rowHover            = p.raised.withAlpha (0.6f);
        p.rowPressed          = p.raised;

        p.neutralActive       = hex (0xff323844);
        p.neutralActiveBorder = hex (0xff2f343e);
        p.selectedBorder      = hex (0xff6b7280);
        p.selectedTileShadow  = none;
        p.highlight           = hex (0xffe8eaee);

        p.selectedBg          = hex (0xff2c323d);
        p.selectedOutline     = hex (0xffe8eaee);
        p.selectedInset       = hex (0xff4a5260);
        p.trackTileAlpha      = 0.16f;

        p.border              = hex (0xff2f343e);
        p.borderSoft          = hex (0xff262a32);
        p.borderStrong        = hex (0xff3a404b);
        p.outline             = hex (0xff424958);

        p.buttonFill          = hex (0xff262b34);
        p.buttonBorder        = hex (0xff323844);
        p.buttonIcon          = hex (0xffa2a9b4);
        p.raisedBorder        = none;
        p.armBorder           = hex (0xff323844);

        p.segmentWell         = hex (0xff15181d);
        p.segmentWellBorder   = hex (0xff2f343e);
        p.segmentSelected     = hex (0xff323844);
        p.segmentShadow       = none;

        p.inputBg             = hex (0xff0e1013);
        p.inputBorder         = hex (0xff262a32);
        p.field               = hex (0xff1c2027);

        p.lcdBg               = hex (0xff050607);
        p.lcdBorder           = none;
        p.lcdLabel            = hex (0xff8f98a6);
        p.lcdValue            = hex (0xffffaa2b);   // the playhead's amber
        p.lcdInTune           = hex (0xff4fd18b);

        // Text
        p.text                = hex (0xffeef1f5);
        p.textSecondary       = hex (0xffa3abb8);
        p.textSoft            = hex (0xffc4c9d1);
        p.textMenu            = hex (0xffc9d0da);
        p.textOnAccent        = hex (0xff1d1405);
        p.textOnColour        = hex (0xffffffff);
        p.textDisabled        = hex (0xff6b7280);

        // Sliders and knobs
        p.sliderTrack         = hex (0xff2a2f38);
        p.sliderFill          = hex (0xffa3abb8);
        p.sliderThumb         = hex (0xffeef1f5);
        p.thumbBorder         = none;
        p.thumbShadow         = none;
        p.volumeSliderTrack   = hex (0xff2a2f38);
        p.thumb               = hex (0xffffffff);
        p.meterThumbRing      = hex (0xff1a1d23);
        p.faderCap            = hex (0xffc9d0da);

        p.knobBody            = hex (0xff2a2f38);
        p.knobRim             = hex (0xff3a404b);
        p.knobPointer         = hex (0xffeef1f5);
        p.knobTrack           = hex (0xff2a2f38);
        p.compactKnobBody     = hex (0xff14171c);
        p.compactKnobRim      = hex (0xff323844);
        p.compactKnobPointer  = hex (0xffffffff);
        p.compactKnobTrack    = hex (0xff3a404b);

        p.smartCardBg         = hex (0xff15181d);
        p.smartCardBorder     = none;
        p.smartCardShadow     = none;

        // The on-screen keyboard
        p.keyboardTray        = hex (0xff050607);
        p.keyboardTrayBorder  = none;
        p.pianoWhite          = hex (0xffe9edf2);
        p.pianoWhiteDown      = hex (0xffa2a9b4);
        p.pianoKeyDivider     = hex (0xffffffff);   // JUCE's white key background, showing between the keys
        p.pianoBlack          = hex (0xff1a1d23);
        p.pianoBlackDown      = hex (0xff6b7280);
        p.pianoBlackLip       = hex (0xff2f343e);
        p.pianoLabel          = hex (0xff5b6270);
        p.rollKeyWhite        = hex (0xffc9d0da);
        p.rollKeyBlack        = hex (0xff0e1013);

        p.previewFill         = hex (0xffe8eaee);
        p.previewIcon         = hex (0xff15181d);
        p.logoOutline         = hex (0xff323844);

        // Accent and status
        p.accent              = hex (0xffffaa2b);
        p.accentLight         = p.accent.brighter (0.15f);
        p.accentBorder        = none;
        p.record              = hex (0xffff5a5a);
        p.warning             = hex (0xffffd25a);
        p.solo                = hex (0xffffd25a);
        p.meterGreen          = hex (0xff4fd18b);
        p.meterYellow         = hex (0xffffd25a);
        p.meterRed            = hex (0xffff5a5a);
        p.effectOn            = hex (0xff4fd18b);
        p.realtimeBadgeText   = hex (0xff4fd18b);
        p.realtimeBadgeBg     = hex (0xff4fd18b).withAlpha (0.18f);

        p.tintLight           = hex (0xffffffff);
        p.tintDark            = hex (0xff000000);

        p.pedalboardFloor     = hex (0xff0e1013);
        p.pedalboardCable     = hex (0xff4a505b);   // lighter than the floor, so it shows

        p.scrim               = juce::Colours::black.withAlpha (0.55f);
        p.scrimStrong         = juce::Colours::black.withAlpha (0.6f);    // session chooser
        p.scrimLight          = juce::Colours::black.withAlpha (0.45f);   // "Saving…" overlay

        // A juce::DropShadow is colour + blur radius + offset; drawForRectangle
        // or drawForPath paints it under a shape.
        p.dialogShadow        = { juce::Colours::black.withAlpha (0.6f), 30, { 0, 8 } };
        p.popoverShadow       = { juce::Colours::black.withAlpha (0.5f), 14, { 0, 4 } };

        // Instrument families, in the order of tokens::Family.
        p.familyColours = { hex (0xffe8c547),    // drums
                            hex (0xff4b93e6),    // audio
                            hex (0xff33b56b),    // strings
                            hex (0xff9b7bea),    // bass
                            hex (0xff5bc0d9),    // keys
                            hex (0xffe070b0),    // synths
                            hex (0xffe08a4f),    // guitar
                            hex (0xff7cc4a0),    // woodwinds
                            hex (0xffd6a35a),    // brass
                            hex (0xffa2a9b4),    // loops
                            hex (0xffa2a9b4) };  // master: neutral grey
        p.inkColours = p.familyColours;          // dark mode: ink is the family colour
        p.knobArcColours = p.familyColours;

        using F = tokens::Family;
        p.eqBands = { p.family (F::audio), p.family (F::strings), p.family (F::drums), p.family (F::bass) };

        // Automation curves: a shade lighter than the track colours, to stand
        // out over regions.
        p.automationVolume    = hex (0xff7cc0ff);   // blue
        p.automationPan       = hex (0xff6fe3a3);   // green
        p.automationReverb    = hex (0xffd4a6ff);   // purple
        p.automationDelay     = hex (0xffffa877);   // orange
        p.automationSound     = hex (0xffffe07a);   // yellow: the Sound tab's knobs
        p.automationPlugin    = hex (0xffff8ac6);   // pink: plug-in parameters
        p.automated           = hex (0xff7cc0ff);

        // The arrangement
        auto& a = p.arrangement;
        a.laneBg              = hex (0xff121418);
        a.laneDivider         = hex (0xff000000);
        a.gridBar             = juce::Colours::white.withAlpha (0.05f);
        a.gridBeat            = juce::Colours::white.withAlpha (0.025f);
        a.selectedLane        = juce::Colours::white.withAlpha (0.045f);
        a.text                = hex (0xffeef1f5);
        a.textSecondary       = hex (0xffa3abb8);
        a.outline             = hex (0xff3a404b);
        a.highlight           = hex (0xffe8eaee);
        a.selectionOutline    = hex (0xffe8eaee);
        a.tintLight           = hex (0xffffffff);
        a.tintDark            = hex (0xff000000);
        a.bubble              = hex (0xff22262e);
        a.tempoCurve          = hex (0xffa2a9b4);
        a.chipFill            = hex (0xff22262e);
        a.chipLitFill         = hex (0xff2a2f38);
        a.chipBorder          = hex (0xff3a404b);
        a.playhead            = hex (0xffffaa2b);
        a.cycle               = hex (0xffffaa2b);
        a.record              = hex (0xffff5a5a);
        a.scrollbar           = hex (0xff3a404b);
        a.regionTextDark      = hex (0xff15181d);
        a.regionTextLight     = hex (0xffffffff);
        a.strip               = hex (0xff15181d);

        return p;
    }

    //==========================================================================
    ThemePalette makeLight()
    {
        // Start from dark for what both themes share (the arrangement, the
        // family colours, the meters, the automation colours), then set every
        // chrome colour. Each area gets its own grey so the panels separate;
        // white is kept for what should stand out: buttons, the selected
        // track header and library category, the Smart Controls cards, keys.
        auto p = makeDark();
        p.isLight = true;

        // Surfaces
        p.windowBg            = hex (0xffe8ebef);
        p.titleBarBg          = hex (0xffd5d9e0);
        p.titleBarBorder      = hex (0xffc3c8d0);
        p.toolbarBg           = hex (0xffe1e4e9);
        p.toolbarBorder       = hex (0xffc9ced6);
        p.transportWell       = hex (0xffd3d7de);
        p.transportWellBorder = hex (0xffc3c8d0);
        p.librarySidebarBg    = hex (0xffe8ebef);
        p.tracksBarBg         = hex (0xffe1e4e9);
        p.sectionsRowBg       = hex (0xffe8ebef);
        p.trackColumnBg       = hex (0xffeef0f3);
        p.trackRowLine        = hex (0xffdde1e6);
        p.rulerBg             = hex (0xffe1e4e9);
        p.sectionsStripBg     = hex (0xffe8ebef);
        p.bottomPanelBg       = hex (0xffe1e4e9);
        p.bottomPanelBorder   = hex (0xffc3c8d0);
        p.inspectorBg         = hex (0xffe8ebef);
        p.divider             = hex (0xffcfd4db);
        p.dialogBg            = hex (0xfff3f4f7);
        p.dialogSidebarBg     = hex (0xffe8ebef);
        p.popupBg             = hex (0xffffffff);

        p.wellBg              = hex (0xffd3d7de);
        p.pedalboardFloor     = hex (0xffd3d8e0);
        p.pedalboardCable     = hex (0xff959ca7);
        p.cardBg              = hex (0xffffffff);
        p.raised              = hex (0xffffffff);
        p.controlFill         = hex (0xffe2e5ea);
        p.selectedRow         = hex (0xffe6e9ee);
        p.tileBg              = hex (0xfff3f4f7);
        p.tileBorder          = hex (0xffd5d9df);
        p.rowHover            = hex (0xffd9dde3);
        p.rowPressed          = hex (0xffd0d5dc);

        p.neutralActive       = hex (0xffe2e5ea);
        p.neutralActiveBorder = hex (0xffc6cbd3);
        p.selectedBorder      = hex (0xff6f7783);
        p.selectedTileShadow  = juce::Colours::black.withAlpha (0.1f);
        p.highlight           = hex (0xff1a1d22);

        p.selectedBg          = hex (0xffffffff);
        p.selectedOutline     = hex (0xff6f7783);
        p.selectedInset       = hex (0xffaeb5c0);
        p.trackTileAlpha      = 0.2f;

        p.border              = hex (0xffd5d9df);
        p.borderSoft          = hex (0xffdde1e6);
        p.borderStrong        = hex (0xffc3c8d0);
        p.outline             = hex (0xffaeb5c0);

        p.buttonFill          = hex (0xffffffff);
        p.buttonBorder        = hex (0xffd5d9df);
        p.buttonIcon          = hex (0xff1a1d22);
        p.raisedBorder        = hex (0xffd5d9df);
        p.armBorder           = hex (0xffc6cbd3);

        p.segmentWell         = hex (0xffd3d7de);
        p.segmentWellBorder   = hex (0xffc3c8d0);
        p.segmentSelected     = hex (0xffffffff);
        p.segmentShadow       = juce::Colours::black.withAlpha (0.1f);   // 0 1px 2px

        p.inputBg             = hex (0xffffffff);
        p.inputBorder         = hex (0xffd5d9df);
        p.field               = hex (0xfff0f2f5);

        p.lcdBg               = hex (0xffffffff);
        p.lcdBorder           = hex (0xffd5d9df);
        p.lcdLabel            = hex (0xff5f6672);
        p.lcdValue            = hex (0xffb86e00);   // the playhead's amber, deeper: #ffaa2b is too pale on white
        p.lcdInTune           = hex (0xff1f8f4e);

        // Text
        p.text                = hex (0xff1a1d22);
        p.textSecondary       = hex (0xff5f6672);
        p.textSoft            = hex (0xff4a515c);
        p.textMenu            = hex (0xff1a1d22);
        p.textDisabled        = hex (0xffa4aab4);

        // Sliders and knobs
        p.sliderTrack         = hex (0xffd5d9df);
        p.sliderFill          = hex (0xff1a1d22);
        p.sliderThumb         = hex (0xffffffff);
        p.thumbBorder         = hex (0xffc6cbd3);
        p.thumbShadow         = juce::Colours::black.withAlpha (0.15f);
        p.volumeSliderTrack   = hex (0xffdfe2e7);
        p.thumb               = hex (0xffffffff);
        p.meterThumbRing      = hex (0xffc6cbd3);
        p.faderCap            = hex (0xffffffff);

        p.knobBody            = hex (0xffffffff);
        p.knobRim             = hex (0xffc6cbd3);
        p.knobPointer         = hex (0xff1a1d22);
        p.knobTrack           = hex (0xffe2e5ea);
        p.compactKnobBody     = p.knobBody;
        p.compactKnobRim      = p.knobRim;
        p.compactKnobPointer  = p.knobPointer;
        p.compactKnobTrack    = p.knobTrack;

        p.smartCardBg         = hex (0xffffffff);
        p.smartCardBorder     = hex (0xffe2e5ea);
        p.smartCardShadow     = juce::Colours::black.withAlpha (0.06f);

        // The on-screen keyboard
        p.keyboardTray        = hex (0xffc9ced6);
        p.keyboardTrayBorder  = hex (0xffb7bec8);
        p.pianoWhite          = hex (0xffffffff);
        p.pianoWhiteDown      = hex (0xffc6cbd3);
        p.pianoKeyDivider     = hex (0xffd5d9df);
        p.pianoBlack          = hex (0xff1a1d22);
        p.pianoBlackDown      = hex (0xff4a515c);
        p.pianoBlackLip       = hex (0xff3a3f47);
        p.pianoLabel          = hex (0xff5f6672);
        p.rollKeyWhite        = hex (0xffffffff);
        p.rollKeyBlack        = hex (0xff1a1d22);

        p.previewFill         = hex (0xff1a1d22);
        p.previewIcon         = hex (0xffffffff);
        p.logoOutline         = none;   // the logo tile is dark: it shows on a light title bar by itself

        // Accent and status. Accent fills stay amber, with a deeper border.
        p.accentBorder        = hex (0xfff09a14);
        p.record              = hex (0xffe0443a);
        p.warning             = hex (0xffb07800);   // the dark yellow is unreadable on light grey
        p.realtimeBadgeText   = hex (0xff1a8a4b);
        p.realtimeBadgeBg     = juce::Colour (31, 154, 85).withAlpha (0.12f);
        p.automated           = hex (0xff2f7fd6);   // a deeper blue for automated controls on the chrome

        // Hover and shading flip: on a light surface a hover darkens.
        p.tintLight           = hex (0xff000000);
        p.tintDark            = hex (0xffffffff);

        p.scrim               = juce::Colours::black.withAlpha (0.3f);
        p.scrimStrong         = juce::Colours::black.withAlpha (0.35f);
        p.scrimLight          = juce::Colours::black.withAlpha (0.25f);
        p.dialogShadow        = { juce::Colours::black.withAlpha (0.18f), 30, { 0, 8 } };
        p.popoverShadow       = { juce::Colours::black.withAlpha (0.15f), 14, { 0, 4 } };

        // Ink: deeper family shades for icons and arcs on the light chrome.
        p.inkColours = { hex (0xff9a7a0c),    // drums
                         hex (0xff2363b8),    // audio
                         hex (0xff1d8048),    // strings
                         hex (0xff6a48c8),    // bass
                         hex (0xff1d8aa6),    // keys
                         hex (0xffb83c86),    // synths
                         hex (0xffb8582a),    // guitar
                         hex (0xff3e8c66),    // woodwinds
                         hex (0xff9a6a22),    // brass
                         hex (0xff5f6672),    // loops
                         hex (0xff5f6672) };  // master

        // The Smart Controls arcs keep the family colours on their white
        // cards, except the drums yellow, which needs to be a little deeper.
        p.knobArcColours[(size_t) tokens::Family::drums] = hex (0xffe2b31f);

        // The arrangement stays dark, a little lighter than in dark mode.
        auto& a = p.arrangement;
        a.laneBg              = hex (0xff3e4249);
        a.laneDivider         = hex (0xff33363c);
        a.gridBar             = juce::Colours::white.withAlpha (0.08f);
        a.gridBeat            = juce::Colours::white.withAlpha (0.03f);
        a.selectedLane        = juce::Colours::white.withAlpha (0.05f);
        a.strip               = hex (0xff33363c);
        a.playhead            = hex (0xfff09a14);
        a.cycle               = hex (0xfff09a14);
        a.scrollbar           = hex (0xff5a5e66);

        return p;
    }

    const ThemePalette* active = nullptr;
}

const ThemePalette& darkPalette()
{
    // A function-local static is built the first time the function runs, so
    // the palette never depends on the order other files' globals are made.
    static const ThemePalette palette = makeDark();
    return palette;
}

const ThemePalette& lightPalette()
{
    static const ThemePalette palette = makeLight();
    return palette;
}

const ThemePalette& theme()
{
    return active != nullptr ? *active : darkPalette();
}

void setActivePalette (const ThemePalette& p)
{
    active = &p;
}

} // namespace ikaros
