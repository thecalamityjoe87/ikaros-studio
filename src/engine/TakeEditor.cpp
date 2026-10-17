#include "engine/TakeEditor.h"
#include "engine/AudioEngine.h"
#include "engine/TakeHelpers.h"
#include "engine/TrackHelpers.h"
#include <set>

namespace te = tracktion;

namespace ikaros
{

using namespace detail;

// Two recordings of the same cycle start and end within this of each other.
static constexpr double sameEdgeSeconds = 0.001;

// A cycle pass at least this close to the loop's length is complete
// (Tracktion cuts the recording into passes by sample count).
static constexpr double completePassSlackSeconds = 0.02;

//==============================================================================
// Shared helpers (declared in TakeHelpers.h)
namespace detail
{

int currentTakeIndex (te::Clip& clip)
{
    if (auto* midi = dynamic_cast<te::MidiClip*> (&clip))
        return countTakes (clip) > 0 ? midi->getCurrentTake() : -1;

    // An audio clip plays the take whose file is its source.
    auto takes = getTakesTree (clip);
    const auto source = clip.state[te::IDs::source].toString();

    for (int i = 0; i < takes.getNumChildren(); ++i)
        if (takes.getChild (i)[te::IDs::source].toString() == source)
            return i;

    return -1;
}

void assignNoteIdsToEveryTake (te::MidiClip& clip)
{
    assignNoteIds (clip);   // the current take (or the only notes)

    for (int i = 0; i < countTakes (clip); ++i)
    {
        auto* list = clip.getTakeSequence (i);

        if (list == nullptr)
            continue;

        // As assignNoteIds (TrackHelpers.h) does for the current take.
        NoteId highest = 0;

        for (auto* n : list->getNotes())
            highest = std::max (highest, getNoteId (*n));

        for (auto* n : list->getNotes())
            if (getNoteId (*n) == 0)
                n->state.setProperty (noteIdProp, (juce::int64) ++highest, nullptr);
    }
}

} // namespace detail

static juce::UndoManager* undoManagerOf (te::Clip& clip)
{
    return &clip.edit.getUndoManager();
}

static double alignOf (const juce::ValueTree& take)
{
    return (double) take.getProperty (takeAlignProp, 0.0);
}

static juce::File fileOfTake (te::Edit& edit, juce::ValueTree take)
{
    return te::SourceFileReference (edit, take, te::IDs::source).getFile();
}

// Read from the file itself: Tracktion's cached file info can be out of date
// for the files it has just cut a cycle recording into.
static double lengthOfFile (te::Edit& edit, const juce::File& f)
{
    auto& formats = edit.engine.getAudioFileFormatManager().readFormatManager;

    if (std::unique_ptr<juce::AudioFormatReader> reader { formats.createReaderFor (f) }; reader != nullptr && reader->sampleRate > 0)
        return (double) reader->lengthInSamples / reader->sampleRate;

    return 0.0;
}

// Points an audio clip at one of its takes: its file, and the clip's offset
// moved by the difference in where the two takes' audio starts.
static void playWaveTake (te::WaveAudioClip& clip, int index)
{
    auto takes = getTakesTree (clip);
    auto next = takes.getChild (index);

    if (! next.isValid())
        return;

    const int current = currentTakeIndex (clip);
    const double fromAlign = current >= 0 ? alignOf (takes.getChild (current)) : 0.0;
    const double offset = clip.getPosition().getOffset().inSeconds() + alignOf (next) - fromAlign;

    // Tracktion reloads the clip's audio when its source property changes
    // (and again when Undo puts the old one back).
    clip.state.setProperty (te::IDs::source, next[te::IDs::source], undoManagerOf (clip));
    clip.setOffset (te::TimeDuration::fromSeconds (juce::jmax (0.0, offset)));
}

// An audio clip's take list, made (with the clip's own file as its only
// take) if it hasn't got one.
static juce::ValueTree makeWaveTakes (te::WaveAudioClip& clip)
{
    auto* um = undoManagerOf (clip);
    auto takes = getTakesTree (clip);

    if (! takes.isValid())
    {
        takes = clip.state.getOrCreateChildWithName (te::IDs::TAKES, um);
        juce::ValueTree take (te::IDs::TAKE);
        take.setProperty (te::IDs::source, clip.state[te::IDs::source], nullptr);   // not in the Edit yet: no undo needed
        takes.addChild (take, -1, um);
    }

    return takes;
}

// A MIDI clip's take list, made if it hasn't got one by moving its notes
// (its SEQUENCE) into it, the way Tracktion's MidiClip::addTake does.
static juce::ValueTree makeMidiTakes (te::MidiClip& clip)
{
    auto* um = undoManagerOf (clip);
    auto takes = getTakesTree (clip);

    if (! takes.isValid())
    {
        takes = clip.state.getOrCreateChildWithName (te::IDs::TAKES, um);

        if (auto sequence = clip.state.getChildWithName (te::IDs::SEQUENCE); sequence.isValid())
        {
            clip.state.removeChild (sequence, um);
            takes.addChild (sequence, -1, um);
        }
    }

    return takes;
}

// MidiClip keeps a list of its takes in the order they were *added*, not
// their order in the state, so takes can only ever be appended. To put a
// take first, everything is taken out and added back in the new order.
static void setMidiTakes (te::MidiClip& clip, const std::vector<juce::ValueTree>& lists, int current)
{
    auto* um = undoManagerOf (clip);
    auto takes = makeMidiTakes (clip);

    for (int i = takes.getNumChildren(); --i >= 0;)
        takes.removeChild (i, um);

    for (auto list : lists)
        takes.addChild (list, -1, um);

    clip.setCurrentTake (current);
}

static std::vector<juce::ValueTree> midiTakeListsOf (te::MidiClip& clip)
{
    std::vector<juce::ValueTree> lists;

    if (auto takes = getTakesTree (clip); takes.isValid())
        for (auto list : takes)
            lists.push_back (list);
    else if (auto sequence = clip.state.getChildWithName (te::IDs::SEQUENCE); sequence.isValid())
        lists.push_back (sequence);

    return lists;
}

//==============================================================================
TakeEditor::TakeEditor (AudioEngine& o) : owner (o)
{
}

std::vector<TakeInfo> TakeEditor::getTakes (RegionId id) const
{
    std::vector<TakeInfo> result;

    if (auto* clip = findClip (owner.getEdit(), id))
    {
        const int current = currentTakeIndex (*clip);

        for (int i = 0; i < countTakes (*clip); ++i)
            result.push_back ({ "Take " + juce::String (i + 1), i == current });
    }

    return result;
}

bool TakeEditor::setCurrentTake (RegionId id, int takeIndex)
{
    auto* clip = findClip (owner.getEdit(), id);

    if (clip == nullptr || ! juce::isPositiveAndBelow (takeIndex, countTakes (*clip))
         || takeIndex == currentTakeIndex (*clip))
        return false;

    owner.beginUndoStep ("Choose Take");

    if (auto* midi = dynamic_cast<te::MidiClip*> (clip))
    {
        midi->setCurrentTake (takeIndex);   // a CachedValue: Undo puts it back
    }
    else if (auto* wave = dynamic_cast<te::WaveAudioClip*> (clip))
    {
        playWaveTake (*wave, takeIndex);
    }

    return true;
}

bool TakeEditor::deleteUnusedTakes (RegionId id)
{
    auto* clip = findClip (owner.getEdit(), id);

    if (clip == nullptr || countTakes (*clip) < 2)
        return false;

    const int current = currentTakeIndex (*clip);

    if (current < 0)
        return flattenTakes (id);

    owner.beginUndoStep ("Delete Unused Takes");
    auto* um = undoManagerOf (*clip);

    if (auto* midi = dynamic_cast<te::MidiClip*> (clip))
    {
        // Tracktion's clearTakes keeps just the current take, as the clip's
        // only notes; they go back into a take list of one.
        midi->clearTakes();
        makeMidiTakes (*midi);
    }
    else
    {
        // The files stay on disk (Undo needs them); the next save leaves
        // them out of the session.
        auto takes = getTakesTree (*clip);

        for (int i = takes.getNumChildren(); --i >= 0;)
            if (i != current)
                takes.removeChild (i, um);
    }

    return true;
}

bool TakeEditor::flattenTakes (RegionId id)
{
    auto* clip = findClip (owner.getEdit(), id);

    if (clip == nullptr || countTakes (*clip) == 0)
        return false;

    owner.beginUndoStep ("Flatten Takes");

    if (auto* midi = dynamic_cast<te::MidiClip*> (clip))
        midi->clearTakes();   // the current take's notes become the clip's notes
    else
        clip->state.removeChild (getTakesTree (*clip), undoManagerOf (*clip));

    return true;
}

//==============================================================================
// After recording
//==============================================================================

// Regions whose audio can be kept as a take: plain recordings or files, not
// looped, stretched, pitched or reversed (their take would sound different).
static bool canBecomeTake (te::WaveAudioClip& clip)
{
    return ! clip.isLooping() && ! clip.getAutoTempo() && ! clip.getAutoPitch() && clip.getSpeedRatio() == 1.0
            && clip.getPitchChange() == 0.0f && clip.getTransposeSemiTones (false) == 0 && ! clip.getIsReversed()
            && clip.getSourceFileReference().getFile().existsAsFile();
}

static bool sameRange (te::Clip& a, te::Clip& b)
{
    const auto ra = a.getEditTimeRange(), rb = b.getEditTimeRange();
    return std::abs ((ra.getStart() - rb.getStart()).inSeconds()) < sameEdgeSeconds
        && std::abs ((ra.getEnd() - rb.getEnd()).inSeconds()) < sameEdgeSeconds;
}

// The other regions on the clip's track that it overlaps, leaving out the
// ones recorded in the same take.
template <typename ClipType>
static std::vector<ClipType*> findCovered (te::Clip& clip, const std::set<RegionId>& recorded)
{
    std::vector<ClipType*> covered;

    if (auto* track = clip.getClipTrack())
        for (auto* other : track->getClips())
            if (other != &clip && recorded.count (other->itemID.getRawID()) == 0
                 && other->getEditTimeRange().overlaps (clip.getEditTimeRange()))
                if (auto* typed = dynamic_cast<ClipType*> (other))
                    covered.push_back (typed);

    return covered;
}

// Tracktion cuts a cycle recording into one file per pass, but lists the
// first pass twice (first and last), and keeps a last pass of a moment
// (Stop pressed just after the loop came round). This tidies that up and
// makes the newest complete pass the one that plays.
static void tidyCyclePasses (te::WaveAudioClip& clip)
{
    auto takes = getTakesTree (clip);

    if (! takes.isValid())
        return;

    auto& edit = clip.edit;
    auto* um = undoManagerOf (clip);
    juce::StringArray seen;

    for (int i = 0; i < takes.getNumChildren();)
    {
        const auto source = takes.getChild (i)[te::IDs::source].toString();

        if (seen.contains (source))
        {
            takes.removeChild (i, um);
        }
        else
        {
            seen.add (source);
            ++i;
        }
    }

    const double passLength = clip.getPosition().getLength().inSeconds();
    const double beatLength = 60.0 / edit.tempoSequence.getTempoAt (clip.getPosition().getStart()).getBpm();
    auto passLengthOf = [&] (int i) { return lengthOfFile (edit, fileOfTake (edit, takes.getChild (i))); };

    if (const int last = takes.getNumChildren() - 1; last > 0 && passLengthOf (last) < beatLength)
        takes.removeChild (last, um);

    if (takes.getNumChildren() < 2)
    {
        clip.state.removeChild (takes, um);   // just one pass: an ordinary region
        return;
    }

    int newest = takes.getNumChildren() - 1;

    while (newest > 0 && passLengthOf (newest) < passLength - completePassSlackSeconds)
        --newest;

    playWaveTake (clip, newest);
}

// Another recording of the same cycle: its takes join the existing region's.
static void addTakesTo (te::WaveAudioClip& existing, te::WaveAudioClip& recorded)
{
    auto* um = undoManagerOf (existing);
    auto existingTakes = makeWaveTakes (existing);
    const int existingCurrent = juce::jmax (0, currentTakeIndex (existing));
    const double existingAlign = alignOf (existingTakes.getChild (existingCurrent));
    const double existingOffset = existing.getPosition().getOffset().inSeconds();

    auto recordedTakes = makeWaveTakes (recorded);
    const int recordedCurrent = juce::jmax (0, currentTakeIndex (recorded));
    const double recordedAlign = alignOf (recordedTakes.getChild (recordedCurrent));
    const double recordedOffset = recorded.getPosition().getOffset().inSeconds();
    const double startDifference = (existing.getPosition().getStart() - recorded.getPosition().getStart()).inSeconds();

    const int first = existingTakes.getNumChildren();

    for (auto take : recordedTakes)
    {
        // The offset the existing region needs to play this take in time,
        // relative to its current take's (see takeAlignProp).
        const double offsetNeeded = recordedOffset - recordedAlign + alignOf (take) + startDifference;
        auto copy = take.createCopy();
        copy.setProperty (takeAlignProp, offsetNeeded - existingOffset + existingAlign, nullptr);
        existingTakes.addChild (copy, -1, um);
    }

    playWaveTake (existing, first + recordedCurrent);
    recorded.removeFromParent();
}

// Writes what the covered regions played during the recorded clip into a
// new file, lined up with the clip's own file (silence where none of them
// played). Their gain and fades are part of it. Returns the file, or {}.
static juce::File writeCoveredAudio (te::WaveAudioClip& clip, std::vector<te::WaveAudioClip*>& covered)
{
    auto& engine = clip.edit.engine;
    auto& formats = engine.getAudioFileFormatManager().readFormatManager;
    const auto clipFile = clip.getSourceFileReference().getFile();
    std::unique_ptr<juce::AudioFormatReader> clipReader (formats.createReaderFor (clipFile));

    if (clipReader == nullptr)
        return {};

    const double rate = clipReader->sampleRate;
    const auto sourceStart = clip.getPosition().getStartOfSource();   // the timeline time of the file's first sample
    const auto range = clip.getEditTimeRange();

    struct Part
    {
        te::WaveAudioClip* clip;
        std::unique_ptr<juce::AudioFormatReader> reader;
        juce::Range<juce::int64> samples;   // where it plays in the new file
    };

    std::vector<Part> parts;
    int channels = 1;

    for (auto* other : covered)
    {
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (other->getSourceFileReference().getFile()));

        // A file at another sample rate would need resampling: it's left as it is.
        if (reader == nullptr || reader->sampleRate != rate)
            continue;

        const auto heard = other->getEditTimeRange().getIntersectionWith (range);
        auto toSample = [&] (te::TimePosition t) { return (juce::int64) std::llround ((t - sourceStart).inSeconds() * rate); };
        channels = juce::jmax (channels, (int) reader->numChannels);
        parts.push_back ({ other, std::move (reader), { toSample (heard.getStart()), toSample (heard.getEnd()) } });
    }

    if (parts.empty())
        return {};

    auto file = clipFile.getParentDirectory().getNonexistentChildFile (clipFile.getFileNameWithoutExtension() + "_before",
                                                                     ".wav", false);
    // On success the writer takes the stream (and out becomes nullptr).
    std::unique_ptr<juce::OutputStream> out = file.createOutputStream();
    std::unique_ptr<juce::AudioFormatWriter> writer;

    if (out != nullptr)
        writer = juce::WavAudioFormat().createWriterFor (out, juce::AudioFormatWriterOptions()
                                                                  .withSampleRate (rate)
                                                                  .withNumChannels (channels)
                                                                  .withBitsPerSample (24));
    if (writer == nullptr)
    {
        file.deleteFile();
        return {};
    }

    const juce::int64 length = clipReader->lengthInSamples;
    const int chunk = 65536;
    juce::AudioBuffer<float> mix (channels, chunk), read (channels, chunk);

    for (juce::int64 pos = 0; pos < length; pos += chunk)
    {
        const int num = (int) juce::jmin ((juce::int64) chunk, length - pos);
        mix.clear();

        for (auto& part : parts)
        {
            const auto overlap = part.samples.getIntersectionWith ({ pos, pos + num });

            if (overlap.isEmpty())
                continue;

            // Where this chunk's samples are in the covered region's own file.
            auto& c = *part.clip;
            const double fileStart = (c.getPosition().getStartOfSource() - sourceStart).inSeconds() * rate;
            const auto readStart = overlap.getStart() - (juce::int64) std::llround (fileStart);
            const int offsetInChunk = (int) (overlap.getStart() - pos);
            read.clear();
            part.reader->read (&read, 0, (int) overlap.getLength(), readStart, true, true);

            // Its gain and its own fades (where they fall inside the recording).
            const double gain = c.getGain();
            const double start = (c.getPosition().getStart() - sourceStart).inSeconds() * rate;
            const double end = (c.getPosition().getEnd() - sourceStart).inSeconds() * rate;
            const double fadeIn = juce::jmax (1.0, c.getFadeIn().inSeconds() * rate);
            const double fadeOut = juce::jmax (1.0, c.getFadeOut().inSeconds() * rate);

            for (int i = 0; i < (int) overlap.getLength(); ++i)
            {
                const double s = (double) (overlap.getStart() + i);
                const auto g = (float) (gain * juce::jlimit (0.0, 1.0, (s - start) / fadeIn)
                                             * juce::jlimit (0.0, 1.0, (end - s) / fadeOut));

                for (int ch = 0; ch < channels; ++ch)
                    mix.addSample (ch, offsetInChunk + i, read.getSample (ch, i) * g);
            }
        }

        writer->writeFromAudioSampleBuffer (mix, 0, num);
    }

    writer.reset();   // finishes the file

    // Only the regions that made it into the file are cut.
    covered.clear();

    for (auto& part : parts)
        covered.push_back (part.clip);

    return file;
}

// Recording over audio regions: what they played there becomes the first
// take of the new region, and they're cut back to the parts outside it.
static void keepCoveredAsTake (te::WaveAudioClip& clip, std::vector<te::WaveAudioClip*> covered)
{
    const auto file = writeCoveredAudio (clip, covered);

    if (file == juce::File())
        return;   // left overlapping, as before takes

    auto& edit = clip.edit;
    auto* um = undoManagerOf (clip);

    for (auto* other : covered)
        te::deleteRegionOfClip (*other, clip.getEditTimeRange());

    auto takes = makeWaveTakes (clip);
    const int current = juce::jmax (0, currentTakeIndex (clip));

    juce::ValueTree take (te::IDs::TAKE);
    {
        te::SourceFileReference ref (edit, take, te::IDs::source);
        ref.setToFile (file, te::SourceFileReference::PathStyle::chooseBest, true);
    }

    // Lined up with the take that plays now (it was written that way).
    take.setProperty (takeAlignProp, alignOf (takes.getChild (current)), nullptr);
    takes.addChild (take, 0, um);
}

static void gatherAudio (te::WaveAudioClip& clip, const std::set<RegionId>& recorded)
{
    tidyCyclePasses (clip);

    std::vector<te::WaveAudioClip*> covered;

    for (auto* other : findCovered<te::WaveAudioClip> (clip, recorded))
        if (canBecomeTake (*other))
            covered.push_back (other);

    for (auto* other : covered)
    {
        if (sameRange (*other, clip))
        {
            addTakesTo (*other, clip);
            return;
        }
    }

    if (! covered.empty())
        keepCoveredAsTake (clip, covered);
}

//==============================================================================
// The notes of the covered MIDI regions inside the recorded clip, as a
// take for it (a MidiList's state, not yet in the Edit).
static juce::ValueTree coveredNotesAsTake (te::MidiClip& clip, const std::vector<te::MidiClip*>& covered)
{
    auto state = te::MidiList::createMidiList();
    te::MidiList list (state, nullptr);   // not in the Edit yet: no undo needed
    list.setMidiChannel (clip.getSequence().getMidiChannel());

    const double start = clip.getStartBeat().inBeats(), end = clip.getEndBeat().inBeats();
    const double toList = -start - contentOffsetBeats (clip);   // timeline beat -> position in the clip's notes

    for (auto* other : covered)
    {
        const double origin = other->getStartBeat().inBeats() + contentOffsetBeats (*other);
        const double from = juce::jmax (start, other->getStartBeat().inBeats());
        const double to = juce::jmin (end, other->getEndBeat().inBeats());

        for (auto* n : other->getSequence().getNotes())
        {
            const double noteStart = juce::jmax (from, origin + n->getStartBeat().inBeats());
            const double noteEnd = juce::jmin (to, origin + n->getEndBeat().inBeats());

            if (noteEnd > noteStart)
                list.addNote (n->getNoteNumber(), te::BeatPosition::fromBeats (noteStart + toList),
                              te::BeatDuration::fromBeats (noteEnd - noteStart), n->getVelocity(), n->getColour(), nullptr);
        }
    }

    return state;
}

static void gatherMidi (te::MidiClip& clip, const std::set<RegionId>& recorded)
{
    std::vector<te::MidiClip*> covered;

    for (auto* other : findCovered<te::MidiClip> (clip, recorded))
        if (! other->isLooping())
            covered.push_back (other);

    if (covered.empty())
        return;

    auto lists = midiTakeListsOf (clip);
    const int current = juce::jmax (0, clip.getCurrentTake());

    // Recording the same cycle again: the new passes join that region's takes
    // (both regions' notes are measured from the same beat).
    for (auto* other : covered)
    {
        if (sameRange (*other, clip) && std::abs (contentOffsetBeats (*other) - contentOffsetBeats (clip)) < 1.0e-6)
        {
            auto* um = undoManagerOf (*other);
            auto takes = makeMidiTakes (*other);
            const int first = takes.getNumChildren();

            for (auto& list : lists)
                takes.addChild (list.createCopy(), -1, um);

            other->setCurrentTake (first + current);
            clip.removeFromParent();
            return;
        }
    }

    // Otherwise what was there becomes the first take, and the old regions
    // are cut back to the parts outside the recording.
    auto before = coveredNotesAsTake (clip, covered);

    for (auto* other : covered)
        te::deleteRegionOfClip (*other, clip.getEditTimeRange());

    lists.insert (lists.begin(), before);
    setMidiTakes (clip, lists, current + 1);
}

void TakeEditor::gatherRecordedTakes (const std::vector<RegionId>& recordedIds, bool midiCycleTakes)
{
    auto* edit = owner.getEdit();
    const std::set<RegionId> recorded (recordedIds.begin(), recordedIds.end());

    for (auto id : recordedIds)
    {
        // Looked up each time: gathering removes clips (a region that joined
        // another's takes).
        if (auto* clip = findClip (edit, id))
        {
            if (auto* wave = dynamic_cast<te::WaveAudioClip*> (clip))
                gatherAudio (*wave, recorded);
            else if (auto* midi = dynamic_cast<te::MidiClip*> (clip); midi != nullptr && midiCycleTakes)
                gatherMidi (*midi, recorded);
        }
    }
}

//==============================================================================
// Crash recovery
void detail::splitIntoPassTakes (te::WaveAudioClip& clip, te::TimeDuration loopLength)
{
    auto& edit = clip.edit;
    const auto file = clip.getSourceFileReference().getFile();
    const double total = lengthOfFile (edit, file);
    const double pass = loopLength.inSeconds();

    if (pass <= 0.0 || total <= pass + completePassSlackSeconds)
        return;   // one pass or less: an ordinary region

    std::vector<juce::File> passes;
    const double beatLength = 60.0 / edit.tempoSequence.getTempoAt (clip.getPosition().getStart()).getBpm();

    for (int i = 0; i * pass < total; ++i)
    {
        const double start = i * pass, end = juce::jmin (total, start + pass);

        if (i > 0 && end - start < beatLength)
            break;   // a last pass of a moment

        auto passFile = file.getParentDirectory().getNonexistentChildFile (file.getFileNameWithoutExtension() + "_take_" + juce::String (i + 1),
                                                                         file.getFileExtension(), false);

        if (te::AudioFileUtils::copySectionToNewFile (edit.engine, file, passFile,
                                                      { te::TimePosition::fromSeconds (start), te::TimePosition::fromSeconds (end) }) <= 0)
            return;

        passes.push_back (passFile);
    }

    // The clip covers one pass, and plays the newest complete one.
    clip.setLength (loopLength, true);

    for (auto& f : passes)
        clip.addTake (f);

    int newest = (int) passes.size() - 1;

    while (newest > 0 && lengthOfFile (edit, passes[(size_t) newest]) < pass - completePassSlackSeconds)
        --newest;

    playWaveTake (clip, newest);
}

} // namespace ikaros
