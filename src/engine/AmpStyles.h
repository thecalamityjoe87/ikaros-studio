#pragma once

#include <juce_graphics/juce_graphics.h>
#include <array>
#include <vector>

namespace ikaros
{

//==============================================================================
/**
    The Amp's voices and cabinets, as the user sees them: names, starting
    settings and looks. One row per voice, so adding a voice means adding a
    row here (and its sound in AmpPlugin.cpp's table), never new UI code:
    AmpView draws any row.

    Tracktion-free on purpose, so the UI can include it. What each voice
    sounds like (its Guitarix amp model, tone stack and levels) is engine
    detail, in AmpPlugin.cpp.

    Never reorder or remove rows of either table: sessions store a voice or
    cab as its index here.
*/

/** The Amp effect's plugin type (AmpPlugin::xmlTypeName), for the UI. */
inline constexpr const char* ampPluginType = "ikarosAmp";

/** The six knobs, left to right, by their parameter IDs (see AmpPlugin). */
constexpr int ampKnobCount = 6;
inline const std::array<const char*, ampKnobCount> ampKnobIds    { "gain", "bass", "mid", "treble", "presence", "master" };
inline const std::array<const char*, ampKnobCount> ampKnobLabels { "Gain", "Bass", "Mid", "Treble", "Presence", "Master" };

/** The knobs' caps. */
enum class AmpKnobCap { black, chrome, cream };

/** The grille cloth's weave (drawn over the speakers). */
enum class AmpGrille { plain, diamond, basketweave, perforated };

struct AmpVoiceStyle
{
    juce::String name;                              // e.g. "Crunch"
    std::array<float, ampKnobCount> knobDefaults;   // 0..1, in ampKnobIds' order
    bool bright = false;                            // the Bright switch's starting state
    int cab = 0;                                    // its cabinet: an index into getAmpCabs()

    // Looks, the same in light and dark themes.
    juce::Colour tolex, tolex2;     // the covering's two-tone diagonal texture
    juce::Colour piping;            // the inset piping line and the voice name
    juce::Colour faceplate;         // the control panel
    juce::Colour lettering;         // the panel's labels and the knobs' value arcs
    AmpKnobCap knobCap = AmpKnobCap::black;
    juce::Colour grilleTint;        // the grille cloth's colour (drawn translucent)
    AmpGrille grille = AmpGrille::plain;
};

struct AmpCab
{
    juce::String name;      // e.g. "4×12 Closed Back": speaker count and size, and a word
    int speakers = 1;       // 1, 2 or 4: how many AmpView draws
};

/** Every voice, in the Voice menu's order. */
const std::vector<AmpVoiceStyle>& getAmpVoices();

/** Every cabinet, in the Cab menu's order. "Off" (no cabinet) comes after
    them: its index is getAmpCabs().size(). */
const std::vector<AmpCab>& getAmpCabs();

/** A cab's name, or "Off" for the index after the last cab. */
juce::String getAmpCabName (int cab);

/** The voice for a new guitar track and a new bass track (by track icon:
    "bass" gets the Bass voice, anything else Clean). */
int getDefaultAmpVoiceFor (const juce::String& trackIcon);

} // namespace ikaros
