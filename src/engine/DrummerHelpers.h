#pragma once

// Drummer helpers shared by the engine's controllers (DrummerEditor,
// RegionEditor, EffectsController, TrackController). Engine .cpp files only:
// it pulls in Tracktion (see TrackHelpers.h).

#include "engine/DrummerTypes.h"
#include "engine/TrackHelpers.h"

namespace ikaros
{
class SoundLibrary;
}

namespace ikaros::detail
{

// A Drummer track is an instrument track whose kindProp is "drummer" (audio
// tracks' is "audio", other instrument tracks have none), so everything that
// treats "not audio" as "instrument" carries on working for it.
inline const juce::String drummerKind ("drummer");

// On a Drummer track: the kit it should play (a library sound id). It plays
// the fallback kit while that one's pack isn't installed.
inline const juce::Identifier drummerKitProp ("ikarosDrummerKit");

// Inside a Drummer region's clip state: its settings (see DrummerEditor.cpp).
inline const juce::Identifier drummerStateType ("IKAROSDRUMMER");

// On each note a Drummer made: its drum piece (DrumPiece as a number), so the
// note can move to another kit's key.
inline const juce::Identifier drumPieceProp ("ikarosDrumPiece");

inline bool isDrummerTrack (const te::AudioTrack& t)
{
    return t.state.getProperty (kindProp).toString() == drummerKind;
}

inline bool isDrummerClip (const te::Clip& c)
{
    return c.state.getChildWithName (drummerStateType).isValid();
}

/** The key each drum piece plays on a library sound: its drum map (see
    SoundLibrary.h), General MIDI for any piece it leaves out. */
std::array<int, numDrumPieces> getDrumKeys (const SoundLibrary&, const juce::String& soundId);

/** Moves the notes a Drummer made on a track's MIDI regions (Drummer regions,
    and ones converted to MIDI) to the keys of the track's current kit. In
    the current undo step, or none (um = nullptr). */
void moveDrumNotesToKit (te::AudioTrack&, const SoundLibrary&, juce::UndoManager*);

} // namespace ikaros::detail
