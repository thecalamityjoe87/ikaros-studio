#pragma once

#include "engine/DrummerTypes.h"
#include <map>
#include <vector>

namespace ikaros
{

//==============================================================================
/**
    The Drummer's grooves: human drumming from Google's Groove MIDI Dataset
    (CC BY 4.0), cut into bars and fills and scored by tools/build_grooves.py,
    which writes resources/drummer/grooves.json. The app has that file
    compiled in (the IkarosDrummerData target in CMakeLists.txt).

    Each drummer has:
      - beat performances: bars a real drummer played one after another, so
        the Drummer can play several of them in a row
      - fills of half a bar or one bar

    Every bar and fill has a complexity and a loudness score, 0..1, ranked
    within its drummer. Note positions are in ticks (ticksPerBeat to a beat)
    from the start of the bar or fill, and keep the drummer's own timing (a
    note can be a little before 0: the drummer was pushing the beat).

    Plain data, no Tracktion. Once loaded it never changes, so any thread can
    read it.
*/
class GrooveLibrary
{
public:
    static constexpr int ticksPerBeat = 480;

    struct Note
    {
        DrumPiece piece = DrumPiece::kick;
        int tick = 0;
        int velocity = 100;
    };

    struct Bar
    {
        float complexity = 0.0f, loudness = 0.0f;
        std::vector<Note> notes;
    };

    /** Bars one drummer in the dataset played in a row, at one tempo. */
    struct Performance
    {
        juce::String source, style;     // the dataset's id for the recording, and its style ("rock")
        int player = 0;                 // which of the dataset's drummers played it (1..10)
        double bpm = 120.0;
        std::vector<Bar> bars;
    };

    struct Fill
    {
        juce::String source, style;
        int player = 0;
        double bpm = 120.0;
        int beats = 4;                  // 2 (half a bar) or 4 (a bar)
        float complexity = 0.0f, loudness = 0.0f;
        std::vector<Note> notes;
    };

    struct Drummer
    {
        DrummerInfo info;
        int swingSubdivision = 8;       // what swing moves: off-beat 8ths (8) or 16ths (16)
        std::vector<Performance> beats;
        std::vector<Fill> fills;
    };

    /** The library the app ships with, read the first time it's asked for.
        Safe to call from any thread (C++ makes sure a function's static
        variable is set up only once, even if two threads arrive together). */
    static const GrooveLibrary& get();

    /** Reads a library from its JSON (see tools/build_grooves.py). For tests. */
    static GrooveLibrary fromJson (const juce::String&);

    bool isValid() const noexcept                           { return ! drummers.empty(); }
    const std::vector<Drummer>& getDrummers() const noexcept    { return drummers; }

    /** The drummer with this id, or nullptr. */
    const Drummer* findDrummer (const juce::String& id) const;

    /** The drummer with this id, or else the first (an unknown id from a
        newer version, say). nullptr only if the library is empty. */
    const Drummer* findDrummerOrFirst (const juce::String& id) const;

    /** For the tests: the Roland TD-11 notes the dataset used, and the
        piece the library maps each one to (by key, see DrumPiece). */
    const std::vector<int>& getSourceNotes() const noexcept         { return sourceNotes; }
    const std::map<int, juce::String>& getTd11Map() const noexcept  { return td11; }

private:
    std::vector<Drummer> drummers;
    std::vector<int> sourceNotes;
    std::map<int, juce::String> td11;
};

} // namespace ikaros
