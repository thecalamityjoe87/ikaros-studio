#pragma once

#include "engine/DrummerTypes.h"
#include <map>
#include <vector>

namespace ikaros
{

//==============================================================================
/**
    Which key of a drum kit plays which of the Drummer's pieces, for kits
    whose instrument.json has no "drumMap" (see SoundLibrary.h): most kits
    don't, and few follow General MIDI (an 808 kit's snare may be on 51, a
    big acoustic kit's high tom on 47 with a cymbal choke on 50).

    It reads the names each key goes by: in an SFZ program, the $define
    names used for its keys ("$key_t1", "$kickkey") and the samples' paths
    and labels ("kicks/hard-kick-01", "HatClosed", "sn_center"); for a kit
    of key zones, its sample files' names. Then each piece goes to the key
    whose names say it best (a kick, a closed hi-hat, the ride's bell...),
    preferring the plain hit over a rim shot, a choke or a brush, and the
    General MIDI key where there's a choice. Toms go high to low by their
    names or numbers ("t1", "t14", "Floor Tom", "lt8"). A piece the kit
    hasn't got plays a close cousin (the pedal hat plays the closed hat, the
    ride bell the ride); on a kit laid out the General MIDI way, it plays
    its General MIDI key instead.

    Only reads files; no Tracktion.
*/
namespace drumkit
{
    /** The names of a key, by key number. */
    using KeyNames = std::map<int, std::vector<juce::String>>;

    /** Every key an SFZ program plays a sample on (on its own or in a range
        of up to three keys), with its names. Follows #include and #define. */
    KeyNames readSfzKeyNames (const juce::File& sfz);

    /** The key each piece is on (in DrumPiece order). An empty `names`
        gives General MIDI. */
    std::array<int, numDrumPieces> guessKeys (const KeyNames& names);

    /** The words a name is made of, lowercase: split at punctuation, at
        lower-to-upper case ("HatClosed") and at letters to digits ("bd8"),
        plus each whole run of letters and digits ("t18"). */
    juce::StringArray wordsOf (const juce::String& name);
}

} // namespace ikaros
