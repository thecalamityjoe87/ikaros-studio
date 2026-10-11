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

int NoteEditor::addNote (RegionId id, const NoteInfo& note)
{
    auto* edit = owner.getEdit();
    auto* clip = findMidiClip (edit, id);

    if (clip == nullptr)
        return -1;

    owner.beginUndoStep ("Add Note");
    auto& sequence = clip->getSequence();
    auto* added = sequence.addNote (juce::jlimit (0, 127, note.pitch),
                                    te::BeatPosition::fromBeats (note.startBeat - contentOffsetBeats (*clip)),
                                    te::BeatDuration::fromBeats (juce::jmax (minNoteBeats, note.lengthBeats)),
                                    juce::jlimit (1, 127, note.velocity), 0, &edit->getUndoManager());

    return sequence.getNotes().indexOf (added);
}

// Where each note in `before` (a copy of an earlier note list) is now.
static NoteEditor::NoteIndexMap mapNotes (const juce::Array<te::MidiNote*>& before, te::MidiList& sequence)
{
    NoteEditor::NoteIndexMap map;
    auto& after = sequence.getNotes();

    for (auto* note : before)
        map.push_back (after.indexOf (note));

    return map;
}

NoteEditor::NoteIndexMap NoteEditor::changeNotes (RegionId id, const std::vector<NoteChange>& changes,
                                                            const juce::String& undoName)
{
    auto* edit = owner.getEdit();
    auto* clip = findMidiClip (edit, id);

    if (clip == nullptr)
        return {};

    // A copy of the note list as it is now: getNotes() is kept sorted by
    // time, so changing a note can re-order it.
    auto notes = clip->getSequence().getNotes();

    if (changes.empty())
        return mapNotes (notes, clip->getSequence());
    const auto offset = contentOffsetBeats (*clip);
    auto* um = &edit->getUndoManager();

    owner.beginUndoStep (undoName);

    for (auto& change : changes)
    {
        if (auto* n = notes[change.index])
        {
            n->setStartAndLength (te::BeatPosition::fromBeats (change.note.startBeat - offset),
                                  te::BeatDuration::fromBeats (juce::jmax (minNoteBeats, change.note.lengthBeats)), um);
            n->setNoteNumber (juce::jlimit (0, 127, change.note.pitch), um);
            n->setVelocity (juce::jlimit (1, 127, change.note.velocity), um);
        }
    }

    return mapNotes (notes, clip->getSequence());
}

void NoteEditor::deleteNotes (RegionId id, const std::vector<int>& indices)
{
    auto* edit = owner.getEdit();
    auto* clip = findMidiClip (edit, id);

    if (clip == nullptr)
        return;

    auto& sequence = clip->getSequence();
    auto notes = sequence.getNotes();
    std::vector<te::MidiNote*> toRemove;

    for (auto index : indices)
        if (auto* n = notes[index])
            if (std::find (toRemove.begin(), toRemove.end(), n) == toRemove.end())
                toRemove.push_back (n);

    if (toRemove.empty())
        return;

    owner.beginUndoStep (toRemove.size() == 1 ? "Delete Note" : "Delete Notes");

    for (auto* n : toRemove)
        sequence.removeNote (*n, &edit->getUndoManager());
}

NoteEditor::NoteIndexMap NoteEditor::quantiseNotes (RegionId id, const std::vector<int>& indices, double gridBeats)
{
    auto* clip = findMidiClip (owner.getEdit(), id);

    if (clip == nullptr || gridBeats <= 0.0)
        return {};

    // Where the notes' beat 0 is on the timeline (the region's start, or for a
    // looped region the start of its first loop), and the notes as the piano
    // roll sees them, read straight from the clip.
    const double regionStart = clip->getStartBeat().inBeats() - loopOffsetBeats (*clip);
    const auto offset = contentOffsetBeats (*clip);
    std::vector<NoteInfo> regionNotes;

    for (auto* n : clip->getSequence().getNotes())
        regionNotes.push_back ({ n->getNoteNumber(), n->getStartBeat().inBeats() + offset,
                                 n->getLengthBeats().inBeats(), n->getVelocity() });

    std::vector<NoteChange> changes;

    auto quantise = [&] (int index)
    {
        auto note = regionNotes[(size_t) index];

        // Snap on the song's timeline, so notes line up with the bars even
        // when the region doesn't start on a grid line.
        const double absolute = regionStart + note.startBeat;
        double snapped = std::round (absolute / gridBeats) * gridBeats;

        if (snapped < regionStart)
            snapped += gridBeats;   // don't push a note out of the front of the region

        if (std::abs (snapped - absolute) > 1.0e-6)
        {
            note.startBeat = snapped - regionStart;
            changes.push_back ({ index, note });
        }
    };

    if (indices.empty())
    {
        for (int i = 0; i < (int) regionNotes.size(); ++i)
            quantise (i);
    }
    else
    {
        for (auto i : indices)
            if (juce::isPositiveAndBelow (i, (int) regionNotes.size()))
                quantise (i);
    }

    // Even with nothing to change, changeNotes returns the (unchanged) map.
    return changeNotes (id, changes, "Quantize");
}

} // namespace ikaros
