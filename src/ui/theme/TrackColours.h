#pragma once

// Colour by instrument family. A track's colour isn't stored anywhere: it's
// worked out from the family of the track's sound (its library category)
// every time it's drawn, so picking a new sound recolours the track, and the
// same family always has the same colour in the library, the track header,
// its regions, the mixer and the bottom panel.
//
// This lives beside Tokens.h rather than in it because it needs TrackInfo,
// and Tokens.h shouldn't depend on the engine.

#include "engine/EngineTypes.h"
#include "ui/theme/Tokens.h"

namespace ikaros::tokens::colour
{

/** The family colour for a library category ("Keyboards", "Drums"...). Sounds
    from packs with a category we don't know get the neutral loops grey. */
inline juce::Colour forCategory (const juce::String& category)
{
    // A function-local static is built once, the first time this runs.
    struct Family { const char* category; juce::Colour colour; };
    static const Family families[]
    {
        { "Keyboards",   family::keys },
        { "Drums",       family::drums },
        { "Bass",        family::bass },
        { "Synths",      family::synths },
        { "Guitar",      family::guitar },
        { "Strings",     family::strings },
        { "Woodwinds",   family::woodwinds },
        { "Brass",       family::brass },
        { "Loops",       family::loops },
        { "Audio Files", family::audio },
    };

    for (auto& f : families)
        if (category == f.category)
            return f.colour;

    return family::loops;
}

/** A track's colour. Audio (recorded) tracks are always the audio blue,
    whatever was dropped on them; instrument tracks follow their sound. */
inline juce::Colour forTrack (const TrackInfo& t)
{
    return t.kind == TrackKind::audio ? family::audio : forCategory (t.category);
}

/** Text or icons drawn on a solid family-colour fill (a region's header):
    dark on the light colours, such as the drums yellow, white on the rest.
    getPerceivedBrightness() weights green most and blue least, the way eyes
    do, so it tells light colours from dark ones better than plain brightness. */
inline juce::Colour textOn (juce::Colour fill)
{
    constexpr float lightFill = 0.65f;   // keys, drums, woodwinds and brass are above it; strings, audio, bass below
    return fill.getPerceivedBrightness() > lightFill ? palette::background : textOnColour;
}

} // namespace ikaros::tokens::colour
