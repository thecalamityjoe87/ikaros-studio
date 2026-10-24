#pragma once

// Plain data types for the Drummer (see DrummerEditor), shared by the engine
// and the UI. Like EngineTypes.h, nothing here knows about Tracktion.

#include <juce_core/juce_core.h>
#include <array>
#include <optional>

namespace ikaros
{

/** The drum pieces the Drummer plays. The groove library (see
    GrooveLibrary.h) numbers them in this order, and each kit says which key
    plays each one (instrument.json's "drumMap", see SoundLibrary.h). */
enum class DrumPiece
{
    kick, snare, hihatClosed, hihatPedal, hihatOpen, crash, ride, rideBell, tomHigh, tomMid, tomFloor
};

inline constexpr int numDrumPieces = 11;

/** The name a piece goes by in the groove library and in kits' drum maps. */
inline const char* getDrumPieceKey (DrumPiece p)
{
    // `static constexpr` inside a function: one table, made at compile time.
    static constexpr std::array<const char*, numDrumPieces> keys {
        "kick", "snare", "hihatClosed", "hihatPedal", "hihatOpen", "crash",
        "ride", "rideBell", "tomHigh", "tomMid", "tomFloor"
    };

    return keys[(size_t) p];
}

inline std::optional<DrumPiece> drumPieceFromKey (const juce::String& key)
{
    for (int i = 0; i < numDrumPieces; ++i)
        if (key == getDrumPieceKey ((DrumPiece) i))
            return (DrumPiece) i;

    return std::nullopt;
}

/** The General MIDI key for each piece: what a kit without a drum map plays. */
inline int getGeneralMidiNote (DrumPiece p)
{
    static constexpr std::array<int, numDrumPieces> notes { 36, 38, 42, 44, 46, 49, 51, 53, 50, 47, 43 };
    return notes[(size_t) p];
}

/** The groups the Drummer editor's piece buttons turn on and off. */
enum class DrumGroup { kickSnare, hihat, cymbals, toms };

inline DrumGroup getDrumGroup (DrumPiece p)
{
    switch (p)
    {
        case DrumPiece::kick:
        case DrumPiece::snare:          return DrumGroup::kickSnare;
        case DrumPiece::hihatClosed:
        case DrumPiece::hihatPedal:
        case DrumPiece::hihatOpen:      return DrumGroup::hihat;
        case DrumPiece::crash:
        case DrumPiece::ride:
        case DrumPiece::rideBell:       return DrumGroup::cymbals;
        case DrumPiece::tomHigh:
        case DrumPiece::tomMid:
        case DrumPiece::tomFloor:       return DrumGroup::toms;
    }

    return DrumGroup::kickSnare;
}

/** What keeps time: the hi-hat or the ride. Automatic picks by loudness. */
enum class Timekeeping { automatic, hihat, ride };

/** A Drummer region's settings (the Drummer editor). The region's notes are
    made from these (see DrummerEngine.h). */
struct DrummerSettings
{
    juce::String drummer { "rock" };   // a DrummerInfo id
    float complexity = 0.45f;          // 0 = simple .. 1 = complex
    float loudness = 0.5f;             // 0 = soft .. 1 = loud
    float fills = 0.4f;                // 0 = only before sections .. 1 = about every four bars, and longer
    float swing = 0.0f;                // 0 = straight .. 1 = a triplet feel
    bool kickSnare = true, hihat = true, cymbals = true, toms = true;   // which pieces play
    Timekeeping timekeeping = Timekeeping::automatic;
    int seed = 1;                      // the variation: the same settings and seed always give the same notes

    bool plays (DrumGroup g) const noexcept
    {
        switch (g)
        {
            case DrumGroup::kickSnare:  return kickSnare;
            case DrumGroup::hihat:      return hihat;
            case DrumGroup::cymbals:    return cymbals;
            case DrumGroup::toms:       return toms;
        }

        return true;
    }

    bool plays (DrumPiece p) const noexcept    { return plays (getDrumGroup (p)); }

    bool operator== (const DrummerSettings&) const = default;
};

/** One of the drummers in the Drummer editor's list. */
struct DrummerInfo
{
    juce::String id;            // stored in DrummerSettings::drummer
    juce::String name;          // e.g. "Rock"
    juce::String description;   // one line, for the drummer's card
    juce::String kit;           // the library sound (SoundInfo::id) a new Drummer track plays
};

} // namespace ikaros
