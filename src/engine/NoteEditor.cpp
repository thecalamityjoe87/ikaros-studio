#include "engine/NoteEditor.h"
#include "engine/AudioEngine.h"
#include "engine/TrackHelpers.h"
#include <algorithm>

namespace te = tracktion;

namespace ikaros
{

using namespace detail;

NoteEditor::NoteEditor (AudioEngine& o) : owner (o)
{
}

//==============================================================================
// Notes
namespace
{
    constexpr double minNoteBeats = 1.0 / 32.0;
}

NoteId NoteEditor::addNote (RegionId id, const NoteInfo& note)
{
    auto* edit = owner.getEdit();
    auto* clip = findMidiClip (edit, id);

    if (clip == nullptr)
        return 0;

    owner.beginUndoStep ("Add Note");
    assignNoteIds (*clip);   // so the new note's ID can't clash with one handed out later

    auto& sequence = clip->getSequence();
    auto* added = sequence.addNote (juce::jlimit (0, 127, note.pitch),
                                    te::BeatPosition::fromBeats (note.startBeat - contentOffsetBeats (*clip)),
                                    te::BeatDuration::fromBeats (juce::jmax (minNoteBeats, note.lengthBeats)),
                                    juce::jlimit (1, 127, note.velocity), 0, &edit->getUndoManager());

    if (added == nullptr)
        return 0;

    assignNoteIds (*clip);
    return getNoteId (*added);
}

void NoteEditor::changeNotes (RegionId id, const std::vector<NoteInfo>& changes, const juce::String& undoName)
{
    auto* edit = owner.getEdit();
    auto* clip = findMidiClip (edit, id);

    if (clip == nullptr || changes.empty())
        return;

    // Find every note first: setting a note's start re-sorts the sequence.
    std::vector<std::pair<te::MidiNote*, NoteInfo>> found;

    for (auto& change : changes)
        if (auto* n = findNote (*clip, change.id))
            found.push_back ({ n, change });

    if (found.empty())
        return;

    const auto offset = contentOffsetBeats (*clip);
    auto* um = &edit->getUndoManager();
    owner.beginUndoStep (undoName);

    // Structured binding: unpacks each pair into the note and its new values.
    for (auto& [n, note] : found)
    {
        n->setStartAndLength (te::BeatPosition::fromBeats (note.startBeat - offset),
                              te::BeatDuration::fromBeats (juce::jmax (minNoteBeats, note.lengthBeats)), um);
        n->setNoteNumber (juce::jlimit (0, 127, note.pitch), um);
        n->setVelocity (juce::jlimit (1, 127, note.velocity), um);
    }
}

void NoteEditor::deleteNotes (RegionId id, const std::vector<NoteId>& ids)
{
    auto* edit = owner.getEdit();
    auto* clip = findMidiClip (edit, id);

    if (clip == nullptr)
        return;

    std::vector<te::MidiNote*> toRemove;

    for (auto noteId : ids)
        if (auto* n = findNote (*clip, noteId))
            if (std::find (toRemove.begin(), toRemove.end(), n) == toRemove.end())
                toRemove.push_back (n);

    if (toRemove.empty())
        return;

    owner.beginUndoStep (toRemove.size() == 1 ? "Delete Note" : "Delete Notes");

    for (auto* n : toRemove)
        clip->getSequence().removeNote (*n, &edit->getUndoManager());
}

void NoteEditor::quantiseNotes (RegionId id, const std::vector<NoteId>& ids, double gridBeats)
{
    auto* clip = findMidiClip (owner.getEdit(), id);

    if (clip == nullptr || gridBeats <= 0.0)
        return;

    assignNoteIds (*clip);

    // Where the notes' beat 0 is on the timeline (the region's start, or for a
    // looped region the start of its first loop), and how far the clip's
    // own note positions are from that.
    const double regionStart = clip->getStartBeat().inBeats() - loopOffsetBeats (*clip);
    const auto offset = contentOffsetBeats (*clip);
    std::vector<NoteInfo> changes;

    for (auto* n : clip->getSequence().getNotes())
    {
        const auto noteId = getNoteId (*n);

        // An empty list means every note.
        if (! ids.empty() && std::find (ids.begin(), ids.end(), noteId) == ids.end())
            continue;

        NoteInfo note { n->getNoteNumber(), n->getStartBeat().inBeats() + offset,
                        n->getLengthBeats().inBeats(), n->getVelocity(), noteId };

        // Snap on the song's timeline, so notes line up with the bars even
        // when the region doesn't start on a grid line.
        const double absolute = regionStart + note.startBeat;
        double snapped = std::round (absolute / gridBeats) * gridBeats;

        if (snapped < regionStart)
            snapped += gridBeats;   // don't push a note out of the front of the region

        if (std::abs (snapped - absolute) > 1.0e-6)
        {
            note.startBeat = snapped - regionStart;
            changes.push_back (note);
        }
    }

    changeNotes (id, changes, "Quantize");
}

} // namespace ikaros
