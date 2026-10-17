#pragma once

#include "engine/EngineTypes.h"
#include "engine/Quantize.h"

namespace ikaros
{

class AudioEngine;

//==============================================================================
/**
    Notes in a MIDI region (the piano roll). Positions are in beats from the
    region start, and notes are identified by their NoteId (see NoteInfo).

    Every method runs on the message thread and records into the session's
    UndoManager.
*/
class NoteEditor
{
public:
    explicit NoteEditor (AudioEngine&);

    /** Adds a note and returns its ID, or 0. */
    NoteId addNote (RegionId, const NoteInfo&);

    /** Changes several notes in one undo step, e.g. a drag of a selection.
        Each note's ID says which note it is; the rest is its new pitch,
        position, length and velocity. */
    void changeNotes (RegionId, const std::vector<NoteInfo>&, const juce::String& undoName);

    void deleteNotes (RegionId, const std::vector<NoteId>&);

    /** Moves note starts towards the nearest grid line (on the song's
        timeline, following its time signatures), as far as the settings'
        strength says, as one "Quantize" undo step. An empty list quantises
        every note in the region. */
    void quantiseNotes (RegionId, const std::vector<NoteId>&, const QuantizeSettings&);

private:
    AudioEngine& owner;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NoteEditor)
};

} // namespace ikaros
