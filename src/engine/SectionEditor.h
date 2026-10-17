#pragma once

#include "engine/EngineTypes.h"
#include <juce_data_structures/juce_data_structures.h>
#include <juce_graphics/juce_graphics.h>
#include <optional>

namespace ikaros
{

class AudioEngine;

using SectionId = std::uint64_t;

/** One part of the song in the marker strip ("Intro", "Verse"...). It runs
    from startBeat up to (not including) endBeat. Sections don't overlap,
    but there can be gaps between them. */
struct SectionInfo
{
    SectionId id = 0;
    juce::String name;
    juce::Colour colour;
    double startBeat = 0.0, endBeat = 0.0;

    double lengthBeats() const noexcept     { return endBeat - startBeat; }
};

//==============================================================================
/**
    The arrangement markers: sections of the song, shown in the strip above
    the tracks. A section can be added, renamed, recoloured and resized
    without touching the music. Moving, copying or deleting one rearranges
    the song itself:

      - moveSection takes everything inside it on every track (regions and
        automation) and puts it somewhere else, and whatever was between the
        two places slides over to make room (the sections swap places);
      - moveSection with copy = true inserts a copy, pushing everything
        after that point later;
      - deleteSection removes everything inside it and closes the gap.

    Regions that cross one of the places where the song is cut get split
    there first, so each piece goes where its part of the song goes.
    Tempo and time-signature changes stay where they are.

    Sections are saved in the session (an IKAROSSECTIONS tree in the Edit's
    own state, in beats, so they follow tempo changes like regions do).
    Every method runs on the message thread, and each user action is one
    undo step.
*/
class SectionEditor
{
public:
    explicit SectionEditor (AudioEngine&);

    /** Every section, in song order. */
    std::vector<SectionInfo> getSections() const;
    std::optional<SectionInfo> getSection (SectionId) const;

    /** How long a new section is, unless the next one starts sooner. */
    static constexpr int defaultBars = 8;

    /** "Add Section": a new section from the start of a bar (numbered from
        0, as in TempoMap), up to defaultBars long. It gets a name ("Intro",
        "Verse", "Chorus"...) and a colour of its own. Returns 0 (and adds
        nothing) if that bar is already inside a section. */
    SectionId addSection (int bar);

    void renameSection (SectionId, const juce::String& newName);
    void setSectionColour (SectionId, juce::Colour);

    /** Moves a section's edges, without touching the music under it. Starts
        no undo step: call AudioEngine::beginUndoStep when the drag starts,
        so a whole drag undoes in one go. Returns false (and changes
        nothing) if the section would overlap another, or be empty. */
    bool setSectionRange (SectionId, double startBeat, double endBeat);

    /** Whether moveSection can put the section at this beat: anywhere that
        isn't inside a section (its own included); moving to its own start
        or end changes nothing, so that's false too unless copying. */
    bool canMoveSectionTo (SectionId, double beat, bool copy) const;

    /** "Move Section" (or "Copy Section"): the section's contents go to
        `beat`, which should be a bar line (see canMoveSectionTo). Moving
        left, the section then starts at `beat`; moving right, it ends there
        (the music between slides left to fill its old place). A copy always
        starts at `beat`. Returns false if nothing changed. */
    bool moveSection (SectionId, double beat, bool copy);

    /** "Delete Section": removes the section and everything inside it on
        every track, then moves everything after it back to close the gap. */
    void deleteSection (SectionId);

    /** "Remove Section Marker": takes the section away but leaves the music. */
    void removeSectionMarker (SectionId);

    /** The colours sections can have, in the order the colour menu lists them. */
    static const std::vector<juce::Colour>& getSectionColours();

    /** True for the session tree that holds the sections, or one of them
        (AudioEngine uses it to tell listeners what kind of thing changed). */
    static bool isSectionState (const juce::ValueTree&);

private:
    AudioEngine& owner;

    // Where each part of the song goes: content from `from` up to `to` goes
    // to start at `dest`. Content in no piece is removed; content in two
    // pieces is copied (the first piece moves the original).
    struct Piece { double from, to, dest; };

    void rearrange (const std::vector<Piece>&);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SectionEditor)
};

} // namespace ikaros
