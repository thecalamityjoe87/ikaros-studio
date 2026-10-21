#pragma once

#include <juce_graphics/juce_graphics.h>
#include <vector>

namespace ikaros
{

//==============================================================================
/**
    How an effect looks as a stompbox on the pedalboard.

    One row per pedal, keyed by the effect's plugin type (PluginSlot::type),
    so it works the same for Tracktion's own effects and our
    BuiltInEffectPlugins. Adding a pedal means adding a row to the table in
    PedalStyles.cpp; PedalView draws any row without new UI code.

    The table is also what makes an effect a "pedal": on an audio track,
    effects with a row here are kept at the start of the chain, before any
    other effects (see EffectsController).

    Tracktion-free on purpose, so the UI can include it.
*/
struct PedalKnob
{
    juce::String parameterId;      // ParameterInfo::id of the setting it turns
    juce::String label;            // shown under the knob, e.g. "Drive" (drawn in capitals)
    bool reversed = false;         // turning it up turns the setting down (e.g. Sustain = a lower threshold)
};

struct PedalStyle
{
    juce::String pluginType;       // the effect it skins, e.g. "chorus"
    juce::String name;             // e.g. "Shimmer Chorus"
    juce::String typeLabel;        // under the name, e.g. "Chorus"
    juce::String group;            // the Add Pedal menu's section, e.g. "Modulation"
    juce::Colour colour;           // the enclosure; the same in light and dark themes
    bool darkLettering = false;    // dark text and knob arcs (on light enclosures), else light
    std::vector<PedalKnob> knobs;  // 2 or 3, left to right
};

/** Every pedal, in the order the Add Pedal menu lists them. */
const std::vector<PedalStyle>& getPedalStyles();

/** The pedal that skins an effect of this type, or nullptr if it isn't one. */
const PedalStyle* findPedalStyle (const juce::String& pluginType);

} // namespace ikaros
