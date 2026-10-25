#pragma once

// Takes on Tracktion clips, for the engine's .cpp files (see TakeEditor for
// what takes are). Implemented in TakeEditor.cpp.

#include <tracktion_engine/tracktion_engine.h>

namespace ikaros::detail
{

namespace te = tracktion;

/** On an audio clip's TAKE: how much later in its file this take's audio
    starts than the take the clip was made with, in seconds (default 0).
    Takes from one cycle recording all start together; a take added later
    (another recording of the same cycle, or what was recorded over) may
    not, as the clip's offset was set for the recording it was made from. */
inline const juce::Identifier takeAlignProp ("ikarosAlign");

/** On a track: the number of its last recording ("<track> Recording <n>"). */
inline const juce::Identifier recordingCountProp ("ikarosRecordingCount");

/** A clip's TAKES list: TAKE children (one per file) on an audio clip,
    SEQUENCE children (MidiLists) on a MIDI clip. Invalid if it has none. */
inline juce::ValueTree getTakesTree (const te::Clip& clip)    { return clip.state.getChildWithName (te::IDs::TAKES); }
inline int countTakes (const te::Clip& clip)                  { return getTakesTree (clip).getNumChildren(); }

/** Which take the clip plays (-1 if, somehow, none of them). */
int currentTakeIndex (te::Clip&);

/** assignNoteIds for every take of a MIDI clip, not just the current one,
    so choosing a take never has to change the session (and Undo puts it
    back exactly). */
void assignNoteIdsToEveryTake (te::MidiClip&);

/** For crash recovery: an audio clip made from a cycle recording's whole
    file gets one take per pass, as Tracktion makes them after a recording
    that wasn't cut short. Not an undo step. */
void splitIntoPassTakes (te::WaveAudioClip&, te::TimeDuration loopLength);

} // namespace ikaros::detail
