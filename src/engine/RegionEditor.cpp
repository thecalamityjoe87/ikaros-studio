#include "engine/RegionEditor.h"
#include "engine/AudioEngine.h"
#include "engine/LoopKey.h"
#include "engine/SoundLibrary.h"
#include "engine/TakeHelpers.h"
#include "engine/TrackHelpers.h"
#include <algorithm>

namespace te = tracktion;

namespace ikaros
{

using namespace detail;

//==============================================================================
RegionEditor::RegionEditor (AudioEngine& o, CreateAudioTrackFn createTrack)
    : owner (o), createAudioTrack (std::move (createTrack))
{
}

void RegionEditor::sessionChanged()
{
    clipboard.clear();   // copied regions can refer to the old session's files
    waveforms.clear();
}

//==============================================================================
// Region helpers
static bool isAudioRegion (te::Clip& clip)
{
    return dynamic_cast<te::WaveAudioClip*> (&clip) != nullptr;
}

// MIDI regions belong on instrument tracks, audio regions on audio tracks.
static bool fitsOnTrack (te::Clip& clip, te::AudioTrack& track)
{
    return isAudioRegion (clip) == isAudioTrack (track);
}

// How far a region's edges can be dragged out (see RegionInfo).
static void findTrimLimits (te::Clip& clip, double& minStartBeat, double& maxEndBeat)
{
    minStartBeat = 0.0;
    maxEndBeat = std::numeric_limits<double>::max();

    // A looped region repeats its content as far as it's dragged, both ways.
    if (clip.isLooping())
        return;

    if (auto* wave = dynamic_cast<te::WaveAudioClip*> (&clip))
    {
        // The recording begins at the clip's content start and lasts as long as the file.
        auto pos = clip.getPosition();
        auto contentStart = pos.time.getStart() - pos.offset;
        auto sourceLength = wave->getSourceLength();

        minStartBeat = juce::jmax (0.0, timeToBeat (clip.edit, contentStart));
        maxEndBeat = sourceLength > te::TimeDuration() ? timeToBeat (clip.edit, contentStart + sourceLength)
                                                       : clip.getEndBeat().inBeats();
    }
}

// The region that was just placed wins: cut away the parts of other regions on
// its track that it now covers. `placed` lists every region placed in the same
// step, so a group that was moved together doesn't cut itself.
static void clearSpaceFor (te::Clip& clip, const std::vector<te::Clip*>& placed)
{
    auto* track = clip.getClipTrack();

    if (track == nullptr)
        return;

    auto range = clip.getEditTimeRange();
    std::vector<te::Clip*> covered;

    // Collect first: cutting adds and removes clips in the list we'd be looping over.
    for (auto* other : track->getClips())
        if (other != &clip
             && std::find (placed.begin(), placed.end(), other) == placed.end()
             && other->getEditTimeRange().overlaps (range))
            covered.push_back (other);

    for (auto* other : covered)
        te::deleteRegionOfClip (*other, range);
}

static void clearSpaceForAll (const std::vector<te::Clip*>& placed)
{
    for (auto* clip : placed)
        clearSpaceFor (*clip, placed);
}

static std::vector<RegionId> idsOf (const std::vector<te::Clip*>& clips)
{
    std::vector<RegionId> ids;

    for (auto* clip : clips)
        ids.push_back (clip->itemID.getRawID());

    return ids;
}

// How far the user moved an audio region in pitch (the audio editor's
// Transpose), leaving out a loop's move to the session key. A loop that
// follows the key uses Tracktion's auto-pitch, which adds the clip's
// "transpose" to the key's move; any other region is moved by its
// "pitchChange". (TakeEditor's canBecomeTake checks both: a transposed
// region's audio can't simply become a take.)
static int userTransposeOf (te::AudioClipBase& clip)
{
    return clip.getAutoPitch() ? clip.getTransposeSemiTones (false) : juce::roundToInt (clip.getPitchChange());
}

static RegionInfo makeRegionInfo (te::Clip& clip, WaveformCache& waveforms)
{
    RegionInfo r;
    r.id = clip.itemID.getRawID();
    r.name = clip.getName();
    r.startBeat = clip.getStartBeat().inBeats();
    r.lengthBeats = clip.getLengthInBeats().inBeats();
    findTrimLimits (clip, r.minStartBeat, r.maxEndBeat);

    if (auto* track = clip.getClipTrack())
        r.trackId = track->itemID.getRawID();

    r.takeCount = countTakes (clip);
    r.currentTake = juce::jmax (0, currentTakeIndex (clip));

    if (clip.isLooping())
    {
        r.loopLengthBeats = clip.getLoopLengthBeats().inBeats();
        r.loopOffsetBeats = loopOffsetBeats (clip);
    }

    if (auto* midi = dynamic_cast<te::MidiClip*> (&clip))
    {
        r.isMidi = true;
        auto contentOffset = contentOffsetBeats (clip);

        // Notes get their IDs when they're made, recorded or loaded (see
        // AudioEngine::assignAllNoteIds); this only catches anything missed.
        assignNoteIds (*midi);

        for (auto* n : midi->getSequence().getNotes())
            r.notes.push_back ({ n->getNoteNumber(),
                                 n->getStartBeat().inBeats() + contentOffset,
                                 n->getLengthBeats().inBeats(),
                                 n->getVelocity(),
                                 getNoteId (*n) });
    }
    else
    {
        r.isMidi = false;

        if (auto* wave = dynamic_cast<te::WaveAudioClip*> (&clip))
        {
            r.peaks = waveforms.computePeaks (*wave, 256);

            // The waveform shows the region's own volume too, like the audio editor's.
            const auto gain = wave->getGain();

            for (auto& peak : r.peaks)
                peak = juce::jmin (1.0f, peak * gain);
        }

        // Fades are stored in seconds; the UI works in beats.
        if (auto* audio = dynamic_cast<te::AudioClipBase*> (&clip))
        {
            auto& edit = clip.edit;
            const auto range = clip.getPosition().time;
            r.fadeInBeats = timeToBeat (edit, range.getStart() + audio->getFadeIn()) - r.startBeat;
            r.fadeOutBeats = r.startBeat + r.lengthBeats - timeToBeat (edit, range.getEnd() - audio->getFadeOut());

            if (followsSessionKey (*audio))
            {
                r.followsKey = true;
                r.keyShiftSemitones = audio->getTransposeSemiTones (true) - audio->getTransposeSemiTones (false);
            }

            r.gainDb = audio->getGainDB();
            r.reversed = audio->getIsReversed();
            r.transposeSemitones = userTransposeOf (*audio);
        }
    }

    return r;
}

std::vector<RegionInfo> RegionEditor::getRegions (TrackId id) const
{
    std::vector<RegionInfo> result;

    if (auto* t = findTrack (owner.getEdit(), id))
        for (auto* clip : t->getClips())
            result.push_back (makeRegionInfo (*clip, waveforms));

    return result;
}

std::optional<RegionInfo> RegionEditor::getRegion (RegionId id) const
{
    if (auto* clip = findClip (owner.getEdit(), id))
        return makeRegionInfo (*clip, waveforms);

    return std::nullopt;
}

RegionId RegionEditor::createChordRegion (TrackId id, double startBeat)
{
    auto* edit = owner.getEdit();
    auto* track = findTrack (edit, id);

    if (track == nullptr || isAudioTrack (*track))
        return 0;   // MIDI regions only go on instrument tracks

    // Four bars from the start of the bar the beat is in. Bars can differ in
    // length (time-signature changes), so the tempo map works them out.
    const auto tempoMap = owner.getTempoMap();
    const int firstBar = tempoMap.barAt (juce::jmax (0.0, startBeat));
    const double start = tempoMap.barToBeat (firstBar);
    double length = tempoMap.barToBeat (firstBar + 4) - start;

    // Don't stack regions on top of each other: shorten the new one to end at
    // the next region, and give up if there's less than a bar of space.
    for (auto* clip : track->getClips())
    {
        auto cs = clip->getStartBeat().inBeats();
        auto ce = cs + clip->getLengthInBeats().inBeats();

        if (start >= cs && start < ce)
            return 0;

        if (cs > start)
            length = juce::jmin (length, cs - start);
    }

    if (length < tempoMap.barToBeat (firstBar + 1) - start - 1.0e-6)
        return 0;

    owner.beginUndoStep ("Add Region");

    auto& ts = edit->tempoSequence;
    te::TimeRange range (ts.toTime (te::BeatPosition::fromBeats (start)),
                         ts.toTime (te::BeatPosition::fromBeats (start + length)));

    auto* clip = dynamic_cast<te::MidiClip*> (track->insertNewClip (te::TrackItem::Type::midi, "Chords", range, nullptr));

    if (clip == nullptr)
        return 0;

    // I – V – vi – IV in C major: one chord per bar, played on beats 1 and 3,
    // with the root held in the bass.
    struct Chord { int bass; std::array<int, 3> notes; };
    const std::array<Chord, 4> progression {{
        { 48, { 60, 64, 67 } },   // C
        { 43, { 59, 62, 67 } },   // G
        { 45, { 60, 64, 69 } },   // Am
        { 41, { 60, 65, 69 } },   // F
    }};

    auto& seq = clip->getSequence();
    auto* um = &edit->getUndoManager();
    for (int bar = 0; tempoMap.barToBeat (firstBar + bar + 1) - start <= length + 1.0e-6; ++bar)
    {
        auto& chord = progression[(size_t) bar % progression.size()];
        const double barStart = tempoMap.barToBeat (firstBar + bar) - start;   // in the region
        const double bpb = tempoMap.barToBeat (firstBar + bar + 1) - start - barStart;
        const double half = bpb / 2.0;

        seq.addNote (chord.bass, te::BeatPosition::fromBeats (barStart),
                     te::BeatDuration::fromBeats (bpb - 0.1), 90, 0, um);

        for (double hit : { 0.0, half })
            for (int n : chord.notes)
                seq.addNote (n, te::BeatPosition::fromBeats (barStart + hit),
                             te::BeatDuration::fromBeats (half - 0.15), hit == 0.0 ? 92 : 76, 0, um);
    }

    // IDs now, as part of the new region (so undo and redo keep them), rather
    // than the first time someone reads the region.
    assignNoteIds (*clip);
    return clip->itemID.getRawID();
}

void RegionEditor::deleteRegion (RegionId id)
{
    deleteRegions ({ id });
}

void RegionEditor::deleteRegions (const std::vector<RegionId>& ids)
{
    // Clip::Ptr is ref-counted: it keeps each clip alive while it's removed.
    std::vector<te::Clip::Ptr> clips;

    for (auto id : ids)
        if (auto* clip = findClip (owner.getEdit(), id))
            clips.push_back (clip);

    if (clips.empty())
        return;

    owner.beginUndoStep (clips.size() == 1 ? "Delete Region" : "Delete Regions");

    for (auto& clip : clips)
        clip->removeFromParent();
}

//==============================================================================
// Region editing
void RegionEditor::moveRegions (const std::vector<RegionPlacement>& placements)
{
    auto* edit = owner.getEdit();

    // Check everything first, so a bad placement cancels the whole move
    // instead of leaving it half done.
    struct Move { te::Clip* clip; te::AudioTrack* track; te::TimePosition start; };
    std::vector<Move> moves;

    for (auto& p : placements)
    {
        auto* clip = findClip (edit, p.id);
        auto* track = findTrack (edit, p.track);

        if (clip == nullptr || track == nullptr || ! fitsOnTrack (*clip, *track))
            return;

        moves.push_back ({ clip, track, beatToTime (*edit, juce::jmax (0.0, p.startBeat)) });
    }

    if (moves.empty())
        return;

    owner.beginUndoStep (moves.size() == 1 ? "Move Region" : "Move Regions");
    std::vector<te::Clip*> moved;

    for (auto& m : moves)
    {
        if (m.clip->getClipTrack() != m.track)
            m.clip->moveTo (*m.track);

        // false, true = keep the content's offset and the region's length.
        m.clip->setStart (m.start, false, true);
        moved.push_back (m.clip);
    }

    clearSpaceForAll (moved);
}

void RegionEditor::trimRegion (RegionId id, double newStart, double newEnd)
{
    auto* edit = owner.getEdit();
    auto* clip = findClip (edit, id);

    if (clip == nullptr)
        return;

    double minStart = 0.0, maxEnd = 0.0;
    findTrimLimits (*clip, minStart, maxEnd);
    newStart = juce::jmax (newStart, minStart);
    newEnd = juce::jmin (newEnd, maxEnd);

    const double oldStart = clip->getStartBeat().inBeats();
    const double oldEnd = clip->getEndBeat().inBeats();

    if (newEnd - newStart < minRegionBeats || (newStart == oldStart && newEnd == oldEnd))
        return;

    owner.beginUndoStep ("Trim Region");
    auto* um = &edit->getUndoManager();

    if (newStart != oldStart && clip->isLooping())
    {
        // A looped region keeps repeating in both directions: moving its
        // start just changes where in the loop it begins. Tracktion's offset
        // can't go below zero, so when the start moves earlier, wrap the
        // offset round by whole loops (the content still lines up).
        const double loopLength = clip->getLoopLengthBeats().inBeats();
        double offset = loopOffsetBeats (*clip) - (oldStart - newStart);
        offset -= std::floor (offset / loopLength) * loopLength;

        auto pos = clip->getPosition();
        pos.time = { beatToTime (*edit, newStart), pos.time.getEnd() };
        pos.offset = pos.time.getStart() - beatToTime (*edit, newStart - offset);
        clip->setPosition (pos);
    }
    else if (newStart != oldStart)
    {
        auto* midi = dynamic_cast<te::MidiClip*> (clip);
        const double contentStart = clip->getContentStartBeat().inBeats();

        if (midi != nullptr && newStart < contentStart)
        {
            // Tracktion's content offset can't go below zero, so a MIDI region
            // can't simply grow past the start of its content. Instead, start
            // the content at the new region start and shift the notes later by
            // the same amount: on the timeline they stay exactly where they were.
            midi->getSequence().moveAllBeatPositions (te::BeatDuration::fromBeats (contentStart - newStart), um);
            clip->setPosition ({ { beatToTime (*edit, newStart), clip->getPosition().time.getEnd() }, {} });
        }
        else
        {
            // true = "preserve sync": the content stays put on the timeline,
            // so trimming hides or reveals part of it rather than sliding it.
            clip->setStart (beatToTime (*edit, newStart), true, false);
        }
    }

    if (newEnd != oldEnd)
        clip->setEnd (beatToTime (*edit, newEnd), true);

    clearSpaceFor (*clip, { clip });
}

bool RegionEditor::loopRegion (RegionId id, double newEndBeat)
{
    auto* edit = owner.getEdit();
    auto* clip = findClip (edit, id);

    if (clip == nullptr)
        return false;

    const double start = clip->getStartBeat().inBeats();
    const double oldEnd = clip->getEndBeat().inBeats();
    newEndBeat = juce::jmax (newEndBeat, start + minRegionBeats);

    if (std::abs (newEndBeat - oldEnd) < 1.0e-9)
        return false;

    // Not looped yet and not made any longer: nothing to repeat.
    if (! clip->isLooping() && newEndBeat <= oldEnd)
        return false;

    owner.beginUndoStep ("Loop Region");

    if (! clip->isLooping())
    {
        // Tracktion only loops audio it knows the length of in beats (library
        // loops know theirs). For any other recording, work it out from the
        // session's tempo, so every region can loop.
        if (auto* wave = dynamic_cast<te::WaveAudioClip*> (clip); wave != nullptr && ! wave->canLoop())
        {
            auto& loopInfo = wave->getLoopInfo();
            const auto seconds = wave->getSourceLength().inSeconds();
            loopInfo.setNumBeats (seconds * edit->tempoSequence.getBeatsPerSecondAt (clip->getPosition().getStart()));

            if (loopInfo.getNumerator() <= 0 || loopInfo.getDenominator() <= 0)
            {
                loopInfo.setNumerator (4);
                loopInfo.setDenominator (4);
            }
        }

        if (! clip->canLoop())
            return false;

        // What the region shows now becomes the part that repeats: the loop
        // range is in the clip's own content time, starting at its offset.
        // The offset then goes back to 0: the region starts at the loop's start.
        auto pos = clip->getPosition();
        clip->setLoopRange ({ te::TimePosition() + pos.offset, pos.getLength() });

        pos.offset = {};
        clip->setPosition (pos);
    }

    clip->setEnd (beatToTime (*edit, newEndBeat), true);
    clearSpaceFor (*clip, { clip });
    return true;
}

std::vector<RegionId> RegionEditor::splitRegions (const std::vector<RegionId>& ids, double beat)
{
    auto* edit = owner.getEdit();
    std::vector<te::Clip*> toSplit;

    // Only regions the split point is properly inside (not at an edge).
    for (auto id : ids)
        if (auto* clip = findClip (edit, id))
            if (beat > clip->getStartBeat().inBeats() + 0.01 && beat < clip->getEndBeat().inBeats() - 0.01)
                toSplit.push_back (clip);

    if (toSplit.empty())
        return {};

    owner.beginUndoStep (toSplit.size() == 1 ? "Split Region" : "Split Regions");
    std::vector<te::Clip*> rightHalves;

    for (auto* clip : toSplit)
        if (auto* track = clip->getClipTrack())
            if (auto* right = track->splitClip (*clip, beatToTime (*edit, beat)))
            {
                // Tracktion takes the fades away where it cuts, which is
                // exactly where a click would be.
                for (auto* half : { clip, right })
                    if (auto* audio = dynamic_cast<te::AudioClipBase*> (half))
                        ensureEdgeFades (*audio);

                rightHalves.push_back (right);
            }

    return idsOf (rightHalves);
}

std::vector<RegionId> RegionEditor::duplicateRegions (const std::vector<RegionId>& ids)
{
    auto* edit = owner.getEdit();
    std::vector<te::Clip*> originals;
    double first = std::numeric_limits<double>::max(), last = 0.0;

    for (auto id : ids)
    {
        if (auto* clip = findClip (edit, id))
        {
            originals.push_back (clip);
            first = juce::jmin (first, clip->getStartBeat().inBeats());
            last = juce::jmax (last, clip->getEndBeat().inBeats());
        }
    }

    if (originals.empty())
        return {};

    owner.beginUndoStep (originals.size() == 1 ? "Duplicate Region" : "Duplicate Regions");

    // The copies keep their spacing and start where the last original ends.
    const double shift = last - first;
    std::vector<te::Clip*> copies;

    for (auto* clip : originals)
    {
        if (auto copy = te::duplicateClip (*clip))
        {
            copy->setStart (beatToTime (*edit, clip->getStartBeat().inBeats() + shift), false, true);
            copies.push_back (copy.get());
        }
    }

    clearSpaceForAll (copies);
    return idsOf (copies);
}

void RegionEditor::renameRegion (RegionId id, const juce::String& newName)
{
    auto name = newName.trim();

    if (auto* clip = findClip (owner.getEdit(), id))
    {
        if (name.isNotEmpty() && name != clip->getName())
        {
            owner.beginUndoStep ("Rename Region");
            clip->setName (name);
        }
    }
}

void RegionEditor::setFade (RegionId id, bool fadeIn, double lengthBeats)
{
    auto* edit = owner.getEdit();
    auto* clip = dynamic_cast<te::AudioClipBase*> (findClip (edit, id));

    if (clip == nullptr)
        return;

    // The length in seconds, measured from the region's edge (the tempo can
    // differ along the way).
    const auto range = clip->getPosition().time;
    const double startBeat = timeToBeat (*edit, range.getStart());
    const double endBeat = timeToBeat (*edit, range.getEnd());
    lengthBeats = juce::jlimit (0.0, endBeat - startBeat, lengthBeats);

    double seconds = fadeIn ? (beatToTime (*edit, startBeat + lengthBeats) - range.getStart()).inSeconds()
                            : (range.getEnd() - beatToTime (*edit, endBeat - lengthBeats)).inSeconds();

    // It stops where the other fade starts.
    const auto other = (fadeIn ? clip->getFadeOut() : clip->getFadeIn()).inSeconds();
    seconds = juce::jlimit (minFadeSeconds, juce::jmax (minFadeSeconds, range.getLength().inSeconds() - other), seconds);

    const auto current = (fadeIn ? clip->getFadeIn() : clip->getFadeOut()).inSeconds();

    if (std::abs (seconds - current) < 1.0e-6)
        return;   // no undo step for a drag that ends where it started

    owner.beginUndoStep (fadeIn ? "Fade In" : "Fade Out");

    if (fadeIn)
        clip->setFadeIn (te::TimeDuration::fromSeconds (seconds));
    else
        clip->setFadeOut (te::TimeDuration::fromSeconds (seconds));
}

//==============================================================================
// Audio regions: the audio editor's settings
void RegionEditor::setRegionGain (RegionId id, float gainDb)
{
    auto* clip = dynamic_cast<te::AudioClipBase*> (findClip (owner.getEdit(), id));
    gainDb = juce::jlimit (minGainDb, maxGainDb, gainDb);

    // Goes into the undo step the caller began (see the header).
    if (clip != nullptr && std::abs (clip->getGainDB() - gainDb) > 1.0e-4f)
        clip->setGainDB (gainDb);
}

void RegionEditor::setRegionReversed (RegionId id, bool reversed)
{
    auto* clip = dynamic_cast<te::WaveAudioClip*> (findClip (owner.getEdit(), id));

    if (clip == nullptr || clip->getIsReversed() == reversed)
        return;

    owner.beginUndoStep ("Reverse Region");

    // Tracktion does the rest when the property changes, in the same undo
    // step: it turns the region's offset (and loop) round, so it shows the
    // same stretch of the recording from the other end, and makes the
    // reversed copy of the file on a background thread. Until that's ready
    // the region is silent.
    clip->setIsReversed (reversed);
}

void RegionEditor::setRegionTranspose (RegionId id, int semitones)
{
    auto* clip = dynamic_cast<te::WaveAudioClip*> (findClip (owner.getEdit(), id));
    semitones = juce::jlimit (-maxTransposeSemitones, maxTransposeSemitones, semitones);

    if (clip == nullptr || semitones == userTransposeOf (*clip))
        return;

    owner.beginUndoStep ("Transpose Region");

    // See userTransposeOf for why there are two ways.
    if (clip->getAutoPitch())
    {
        clip->setTranspose (semitones);
    }
    else
    {
        // Pitch shifting needs a time stretcher (as for loops: see importAudioFiles).
        if (clip->getTimeStretchMode() == te::TimeStretcher::disabled)
            clip->setTimeStretchMode (te::TimeStretcher::soundtouchBetter);

        clip->setPitchChange ((float) semitones);
    }
}

bool RegionEditor::getWaveform (RegionId id, double fromBeat, double toBeat, int numColumns,
                                std::vector<WaveformRange>& columns)
{
    columns.clear();
    auto* edit = owner.getEdit();
    auto* clip = dynamic_cast<te::WaveAudioClip*> (findClip (edit, id));

    // A recording that's still being written is read once it's finished.
    if (clip == nullptr || numColumns <= 0 || toBeat <= fromBeat || edit->getTransport().isRecording())
        return false;

    // A reversed region's waveform comes from the original file, turned round
    // below (Tracktion's reversed copy may not be made yet).
    auto& engine = owner.getTeEngine();
    const auto file = clip->getOriginalFile();
    const auto overview = waveforms.getOverview (file, engine.getAudioFileFormatManager().readFormatManager);

    if (overview == nullptr)
        return false;   // still reading it

    const double fileLength = overview->getLengthInSeconds();
    const bool reversed = clip->getIsReversed();
    const bool looping = clip->isLooping();
    const float gain = clip->getGain();
    const auto pos = clip->getPosition();
    const double startBeat = clip->getStartBeat().inBeats();
    const double offsetSeconds = pos.getOffset().inSeconds();
    const double speed = clip->getSpeedRatio();

    // Library loops (auto-tempo) are stretched to the session's tempo, so
    // they're measured in beats of their own recording.
    const bool beatBased = clip->getAutoTempo();
    const double fileBeatsPerSecond = beatBased ? clip->getLoopInfo().getBeatsPerSecond (te::AudioFile (engine, file).getInfo()) : 0.0;
    const double loopLength = beatBased ? clip->getLoopLengthBeats().inBeats() : clip->getLoopLength().inSeconds();
    const bool canLoop = looping && loopLength > 0.0;

    // x wrapped into 0..length: the same place in every repeat of a loop.
    auto wrap = [] (double x, double length) { return x - std::floor (x / length) * length; };

    // Where a beat on the timeline falls in the file, in seconds. Mirrors
    // what Tracktion's AudioClipBase::clipTimeToSourceFileTime does, but
    // carries on past the region's edges (in both directions).
    auto secondsAt = [&] (double beat)
    {
        double seconds = 0.0;

        if (beatBased && fileBeatsPerSecond > 0.0)
        {
            const double fileBeat = canLoop ? clip->getLoopStartBeats().inBeats() + wrap (beat - startBeat + loopOffsetBeats (*clip), loopLength)
                                            : beat - clip->getContentStartBeat().inBeats();
            seconds = fileBeat / fileBeatsPerSecond;
        }
        else
        {
            const double t = (beatToTime (*edit, beat) - pos.getStart()).inSeconds();
            seconds = (canLoop ? clip->getLoopStart().inSeconds() + wrap (t + offsetSeconds, loopLength)
                               : t + offsetSeconds) * speed;
        }

        // The reversed copy's first second is the original's last.
        return reversed ? fileLength - seconds : seconds;
    };

    const double beatsPerColumn = (toBeat - fromBeat) / numColumns;
    columns.reserve ((size_t) numColumns);

    for (int i = 0; i < numColumns; ++i)
    {
        // How much of the file one column covers, around the file position
        // under its middle (simpler than its two edges, which can be a
        // loop apart).
        const double from = fromBeat + i * beatsPerColumn;
        const double secondsWide = beatBased && fileBeatsPerSecond > 0.0
                                     ? beatsPerColumn / fileBeatsPerSecond
                                     : (beatToTime (*edit, from + beatsPerColumn) - beatToTime (*edit, from)).inSeconds() * speed;
        const double middle = secondsAt (from + beatsPerColumn * 0.5);

        auto range = overview->getRange (middle - secondsWide * 0.5, middle + secondsWide * 0.5);
        columns.push_back ({ juce::jlimit (-1.0f, 1.0f, range.min * gain), juce::jlimit (-1.0f, 1.0f, range.max * gain) });
    }

    return true;
}

void RegionEditor::copyRegions (const std::vector<RegionId>& ids)
{
    auto* edit = owner.getEdit();
    auto tracks = getUserTracks (*edit);

    struct Found { te::Clip* clip; te::AudioTrack* track; };
    std::vector<Found> found;
    int topTrack = std::numeric_limits<int>::max();
    double first = std::numeric_limits<double>::max();

    for (auto id : ids)
    {
        if (auto* clip = findClip (edit, id))
        {
            if (auto* track = dynamic_cast<te::AudioTrack*> (clip->getClipTrack()))
            {
                found.push_back ({ clip, track });
                topTrack = juce::jmin (topTrack, tracks.indexOf (track));
                first = juce::jmin (first, clip->getStartBeat().inBeats());
            }
        }
    }

    if (found.empty())
        return;

    clipboard.clear();

    for (auto& f : found)
    {
        f.clip->flushStateToValueTree();   // make sure the state holds the latest settings
        clipboard.push_back ({ f.clip->state.createCopy(),
                                     f.track->itemID.getRawID(),
                                     tracks.indexOf (f.track) - topTrack,
                                     f.clip->getStartBeat().inBeats() - first,
                                     isAudioRegion (*f.clip) });
    }
}

bool RegionEditor::canPaste() const
{
    return ! clipboard.empty();
}

std::vector<RegionId> RegionEditor::pasteRegions (TrackId targetId, double beat)
{
    auto* edit = owner.getEdit();

    if (clipboard.empty())
        return {};

    auto tracks = getUserTracks (*edit);
    const int targetIndex = tracks.indexOf (findTrack (edit, targetId));

    owner.beginUndoStep ("Paste");
    std::vector<te::Clip*> pasted;

    for (auto& copied : clipboard)
    {
        // Keep the copied layout, starting from the target track (juce::Array's
        // [] gives nullptr past the end)...
        auto* track = targetIndex >= 0 ? tracks[targetIndex + copied.trackOffset] : nullptr;

        // ...unless that's the wrong kind of track: then back where it came from.
        if (track == nullptr || isAudioTrack (*track) != copied.isAudio)
            track = findTrack (edit, copied.sourceTrack);

        if (track == nullptr || isAudioTrack (*track) != copied.isAudio)
            continue;

        // Every paste needs fresh IDs for the clip and anything inside it.
        auto state = copied.state.createCopy();
        te::EditItemID::remapIDs (state, nullptr, *edit);

        if (auto* clip = te::insertClipCopy (*track, te::ClipCopy::fromClipboardState (state, false)))
        {
            clip->setStart (beatToTime (*edit, juce::jmax (0.0, beat) + copied.beatOffset), false, true);
            pasted.push_back (clip);
        }
    }

    clearSpaceForAll (pasted);
    return idsOf (pasted);
}

//==============================================================================
// Audio files
bool RegionEditor::isSupportedAudioFile (const juce::File& file) const
{
    auto& formats = owner.getTeEngine().getAudioFileFormatManager().readFormatManager;
    return formats.findFormatForFileExtension (file.getFileExtension()) != nullptr;
}

std::vector<RegionId> RegionEditor::importAudioFiles (TrackId trackId, const juce::StringArray& paths, double beat)
{
    auto* edit = owner.getEdit();
    auto& engine = owner.getTeEngine();
    std::vector<juce::File> files;

    for (auto& path : paths)
    {
        juce::File file (path);

        // te::AudioFile reads the header, so this also skips broken files.
        if (file.existsAsFile() && isSupportedAudioFile (file) && te::AudioFile (engine, file).isValid())
            files.push_back (file);
    }

    if (files.empty())
        return {};

    owner.beginUndoStep (files.size() == 1 ? "Import Audio File" : "Import Audio Files");

    auto* track = findTrack (edit, trackId);

    if (track == nullptr || ! isAudioTrack (*track))
        track = findTrack (edit, createAudioTrack (files.front().getFileNameWithoutExtension()));

    if (track == nullptr)
        return {};

    std::vector<te::Clip*> added;
    auto start = beatToTime (*edit, juce::jmax (0.0, beat));

    for (auto& file : files)
    {
        const auto fileSeconds = te::AudioFile (engine, file).getLength();
        auto length = te::TimeDuration::fromSeconds (fileSeconds);

        // A loop from the library knows its tempo: it lasts that many beats
        // at the session's tempo, stretched to fit (and keeps following the
        // tempo if it changes). If it knows its key, it follows the
        // session's key too (see LoopKey.h).
        const auto* loop = owner.getSounds().findLoop (file);
        const auto loopBpm = loop != nullptr ? loop->bpm : 0.0;
        const auto loopBeats = fileSeconds * loopBpm / 60.0;
        const bool hasKey = loop != nullptr && ! loop->unpitched && loop->keyRoot >= 0;

        if (loopBpm > 0.0)
            length = beatToTime (*edit, timeToBeat (*edit, start) + loopBeats) - start;

        // ClipPosition = { time range on the timeline, offset into the file }.
        if (auto clip = track->insertWaveClip (file.getFileNameWithoutExtension(), file, { { start, start + length }, {} }, false))
        {
            if (loopBpm > 0.0)
            {
                clip->getLoopInfo().setBpm (loopBpm, clip->getAudioFile().getInfo());
                clip->setTimeStretchMode (te::TimeStretcher::soundtouchBetter);
                clip->setAutoTempo (true);
            }

            if (hasKey)
            {
                // Pitch shifting needs a time stretcher, even at the loop's own tempo.
                clip->setTimeStretchMode (te::TimeStretcher::soundtouchBetter);
                setLoopKey (*clip, loop->keyRoot, loop->keyMinor);
            }

            ensureEdgeFades (*clip);
            added.push_back (clip.get());
            start = start + length;
        }
    }

    clearSpaceForAll (added);
    return idsOf (added);
}

} // namespace ikaros
