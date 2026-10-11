#pragma once

#include "engine/EngineTypes.h"

namespace ikaros
{

class AudioEngine;

//==============================================================================
/**
    Notes in a MIDI region (the piano roll). Positions are in beats from the
    region start, and notes are identified by index (see NoteInfo).

    Every method runs on the message thread and records into the session's
    UndoManager.
*/
class NoteEditor
{
public:
    explicit NoteEditor (AudioEngine&);

    /** Adds a note and returns its index, or -1. */
    int addNote (RegionId, const NoteInfo&);

    /** Notes are kept in time order, so changing one note can renumber the
        others. After a change, map[oldIndex] is where each note is now. */
    using NoteIndexMap = std::vector<int>;

    /** Changes several notes in one undo step, e.g. a drag of a selection. */
    NoteIndexMap changeNotes (RegionId, const std::vector<NoteChange>&, const juce::String& undoName);

    void deleteNotes (RegionId, const std::vector<int>& indices);

    /** Moves note starts to the nearest grid line (on the song's timeline).
        An empty list quantises every note in the region. */
    NoteIndexMap quantiseNotes (RegionId, const std::vector<int>& indices, double gridBeats);

private:
    AudioEngine& owner;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NoteEditor)
};

} // namespace ikaros
