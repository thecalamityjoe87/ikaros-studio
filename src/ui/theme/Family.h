#pragma once

namespace ikaros::tokens
{

/** The instrument families. Tracks, regions, library categories and the
    Smart Controls arcs are coloured by the family of the track's sound.

    Components remember *which* family they show, not its colour, and look
    the colour up when they paint (theme().family in Theme.h), so switching
    themes recolours them. `master` is the master channel's neutral grey.

    This tiny header exists so component headers can name a Family without
    including all of Tokens.h. */
enum class Family { drums, audio, strings, bass, keys, synths, guitar, woodwinds, brass, loops, plugins, master };

} // namespace ikaros::tokens
