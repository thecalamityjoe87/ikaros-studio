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
#include "ui/theme/Theme.h"

namespace ikaros::tokens::colour
{

/** The family of a library category ("Keyboards", "Drums"...). Sounds from
    packs with a category we don't know count as loops (neutral grey). */
inline Family familyOf (const juce::String& category)
{
    // A function-local static is built once, the first time this runs.
    struct Entry { const char* category; Family family; };
    static const Entry entries[]
    {
        { "Keyboards",   Family::keys },
        { "Drums",       Family::drums },
        { "Bass",        Family::bass },
        { "Synths",      Family::synths },
        { "Guitar",      Family::guitar },
        { "Strings",     Family::strings },
        { "Woodwinds",   Family::woodwinds },
        { "Brass",       Family::brass },
        { "Loops",       Family::loops },
        { "Plug-ins",    Family::plugins },
        { "Sampler",     Family::synths },   // the user's own sound (EffectsController::setOwnSound)
        { "Audio Files", Family::audio },
    };

    for (auto& e : entries)
        if (category == e.category)
            return e.family;

    return Family::loops;
}

/** A track's family. Instrument tracks follow their sound. Audio (recorded)
    tracks are audio blue, whatever was dropped on them, except guitar and
    bass tracks (by their icon, see TrackController::addGuitarTrack), which
    take the guitar and bass colours. */
inline Family familyOf (const TrackInfo& t)
{
    if (t.kind != TrackKind::audio)
        return familyOf (t.category);

    return t.icon == "guitar" ? Family::guitar
         : t.icon == "bass"   ? Family::bass
                              : Family::audio;
}

/** Shorthands for painting: the colour of a category's or a track's family
    in the current theme. */
inline juce::Colour forCategory (const juce::String& category)   { return theme().family (familyOf (category)); }
inline juce::Colour forTrack (const TrackInfo& t)                { return theme().family (familyOf (t)); }

/** Text or icons drawn on a solid family-colour fill (a region's header):
    dark on the light colours, such as the drums yellow, white on the rest.
    getPerceivedBrightness() weights green most and blue least, the way eyes
    do, so it tells light colours from dark ones better than plain brightness. */
inline juce::Colour textOn (juce::Colour fill)
{
    constexpr float lightFill = 0.65f;   // keys, drums, woodwinds and brass are above it; strings, audio, bass below
    const auto& arrangement = theme().arrangement;
    return fill.getPerceivedBrightness() > lightFill ? arrangement.regionTextDark : arrangement.regionTextLight;
}

} // namespace ikaros::tokens::colour
