#include "engine/FlexEditor.h"
#include "engine/AudioEngine.h"
#include "engine/FlexHelpers.h"
#include "engine/TrackHelpers.h"

namespace te = tracktion;

namespace ikaros
{

//==============================================================================
// Flex on Tracktion clips (FlexHelpers.h)
//==============================================================================
namespace detail
{

// Changes smaller than this (in seconds) aren't worth writing: they'd only
// be rounding, and would make an edit that changes nothing look like one.
static constexpr double sameSeconds = 1.0e-9;

static te::WaveAudioClip* asWave (const te::Clip& clip)
{
    // const_cast: Tracktion's getters for a clip's warp markers aren't const.
    return dynamic_cast<te::WaveAudioClip*> (const_cast<te::Clip*> (&clip));
}

static juce::ValueTree markersTreeOf (const te::Clip& clip)
{
    return clip.state.getChildWithName (te::IDs::WARPTIME).getChildWithName (te::IDs::WARPMARKERS);
}

static double recordingLengthOf (const te::AudioClipBase& clip)
{
    return te::AudioFile (clip.edit.engine, clip.getOriginalFile()).getLength();
}

juce::String whyClipCantFlex (const te::Clip& clip)
{
    auto* wave = asWave (clip);

    if (wave == nullptr)
        return "Flex works on audio regions.";

    if (wave->getAutoTempo() || wave->getAutoPitch())
        return "This loop already follows the song's tempo and key by itself, so it can't use Flex. "
               "Flex works on recordings and audio files.";

    if (wave->getIsReversed())
        return "Flex doesn't work on a region that plays backwards.";

    if (recordingLengthOf (*wave) <= 0.0)
        return "The region's recording can't be found.";

    return {};
}

// Flexed: on a Flex track, and able to be.
static bool isFlexActive (const te::Clip& clip)
{
    auto* track = clip.getTrack();
    return track != nullptr && isFlexTrack (*track) && canFlex (clip);
}

std::vector<WarpPoint> getWarpPoints (const te::AudioClipBase& clip)
{
    std::vector<WarpPoint> points;

    for (auto marker : markersTreeOf (clip))
        points.push_back ({ (double) marker[te::IDs::sourceTime], (double) marker[te::IDs::warpTime],
                            (bool) marker.getProperty (tempoMarkerProp, false) });

    return points;
}

// Tracktion only warps a clip (renders a stretched copy of its recording)
// with its warpTime switch on. Ikaros keeps that switch on only while the
// markers actually move something: a Flex region nobody has touched plays
// its recording directly, with nothing to wait for and nothing stretched.
static void updateWarpSwitch (te::WaveAudioClip& clip)
{
    const bool warped = isFlexActive (clip) && flexmap::isWarped (getWarpPoints (clip));

    if (clip.getWarpTime() != warped)
        clip.setWarpTime (warped);   // a CachedValue bound to the state, with the undo manager
}

// A WARPTIME tree, ready to use: its WARPMARKERS list holds a marker at
// each end of a recording `length` seconds long, each in its own place.
static juce::ValueTree makeWarpState (double length)
{
    // Built outside the Edit, so none of this needs the undo manager.
    juce::ValueTree markers (te::IDs::WARPMARKERS);

    for (double t : { 0.0, length })
        markers.appendChild (te::createValueTree (te::IDs::WARPMARKER, te::IDs::sourceTime, t, te::IDs::warpTime, t), nullptr);

    juce::ValueTree warpState (te::IDs::WARPTIME);
    warpState.setProperty (te::IDs::warpEndMarkerTime, length, nullptr);
    warpState.appendChild (markers, nullptr);
    return warpState;
}

// The clip's WARPTIME tree, made if it hasn't got one.
static void ensureWarpState (te::WaveAudioClip& clip)
{
    auto* um = &clip.edit.getUndoManager();
    juce::ValueTree added;

    // Left to itself, the clip's WarpTimeManager (made the first time it's
    // asked for) would add the WARPTIME tree, then the WARPMARKERS list in
    // it, then each marker, as separate undo actions. Undoing that step
    // takes the list out of the tree as well as the tree out of the clip,
    // and the manager can't be given that list back. So the whole tree is
    // added here first, in one go (one undo action); the manager then finds
    // it there and adds nothing.
    if (! clip.state.getChildWithName (te::IDs::WARPTIME).isValid())
    {
        added = makeWarpState (recordingLengthOf (clip));
        clip.state.addChild (added, -1, um);
    }

    // The manager lives as long as the clip (it's reference-counted, and
    // the clip holds on to it). Once made, it starts looking for the hits
    // in the background.
    auto& manager = clip.getWarpTimeManager();

    // An undo can take the tree out of the clip again (it was added in the
    // undone step), but the manager keeps referring to that same tree. Put
    // it back, rather than a new one the manager wouldn't know about.
    // (ValueTree's == asks whether two are the same tree, not equal copies.)
    if (manager.state != clip.state.getChildWithName (te::IDs::WARPTIME))
    {
        if (added.isValid())
            clip.state.removeChild (added, um);

        clip.state.addChild (manager.state, -1, um);
    }
}

void applyTrackFlex (te::Clip& clip)
{
    auto* wave = asWave (clip);

    if (wave == nullptr)
        return;

    if (isFlexActive (clip))
        ensureWarpState (*wave);

    updateWarpSwitch (*wave);
}

void setWarpPoints (te::AudioClipBase& clip, const std::vector<WarpPoint>& points)
{
    auto list = markersTreeOf (clip);

    if (! list.isValid() || points.size() < 2)
        return;

    auto* um = &clip.edit.getUndoManager();

    auto setIfDifferent = [um] (juce::ValueTree tree, const juce::Identifier& id, double value)
    {
        if (! tree.hasProperty (id) || std::abs ((double) tree[id] - value) > sameSeconds)
            tree.setProperty (id, value, um);
    };

    // Reuse the markers that are there (in order), then add or remove the rest.
    const int common = juce::jmin (list.getNumChildren(), (int) points.size());

    // A tempo marker has the property; any other marker has none (as in
    // sessions from before tempo markers).
    auto setTempoMarker = [um] (juce::ValueTree tree, bool isTempoMarker)
    {
        if (isTempoMarker && ! tree.hasProperty (tempoMarkerProp))
            tree.setProperty (tempoMarkerProp, true, um);
        else if (! isTempoMarker && tree.hasProperty (tempoMarkerProp))
            tree.removeProperty (tempoMarkerProp, um);
    };

    for (int i = 0; i < common; ++i)
    {
        setIfDifferent (list.getChild (i), te::IDs::sourceTime, points[(size_t) i].source);
        setIfDifferent (list.getChild (i), te::IDs::warpTime, points[(size_t) i].warp);
        setTempoMarker (list.getChild (i), points[(size_t) i].tempoMarker);
    }

    while (list.getNumChildren() > (int) points.size())
        list.removeChild (list.getNumChildren() - 1, um);

    for (size_t i = (size_t) common; i < points.size(); ++i)
    {
        auto marker = te::createValueTree (te::IDs::WARPMARKER,
                                           te::IDs::sourceTime, points[i].source,
                                           te::IDs::warpTime, points[i].warp);
        setTempoMarker (marker, points[i].tempoMarker);   // not in the Edit yet: no undo either way
        list.addChild (marker, -1, um);
    }

    // How long the warped copy of the recording is: up to the last marker.
    setIfDifferent (list.getParent(), te::IDs::warpEndMarkerTime, points.back().warp);

    if (auto* wave = asWave (clip))
        updateWarpSwitch (*wave);
}

te::TimePosition contentStartOf (const te::Clip& clip)
{
    const auto pos = clip.getPosition();
    auto start = pos.getStart() - pos.getOffset();

    if (clip.isLooping())
        start = start - te::toDuration (clip.getLoopStart());

    return start;
}

te::TimeDuration contentLengthOf (const te::AudioClipBase& clip)
{
    const auto points = getWarpPoints (clip);

    if (isFlexActive (clip) && points.size() >= 2)
        return te::TimeDuration::fromSeconds (points.back().warp);

    return te::TimeDuration::fromSeconds (recordingLengthOf (clip));
}

//==============================================================================
// The beats where the tempo changes: where a beat starts lasting a
// different time. (The hidden tempo steps that keep beats the same length
// where the time signature changes don't count, see AudioEngine.)
static std::vector<double> tempoChangeBeats (te::Edit& edit)
{
    std::vector<double> beats;
    auto& tempos = edit.tempoSequence;

    for (auto* tempo : tempos.getTempos())
    {
        const double beat = tempo->getStartBeat().inBeats();

        if (beat <= 0.0)
            continue;

        // How long the beat before and the beat after it last.
        auto timeAt = [&tempos] (double b) { return tempos.toTime (te::BeatPosition::fromBeats (b)).inSeconds(); };
        const double before = timeAt (beat) - timeAt (beat - 0.001);
        const double after = timeAt (beat + 0.001) - timeAt (beat);

        if (std::abs (before - after) > 1.0e-9)
            beats.push_back (beat);
    }

    return beats;
}

// Adds a tempo marker to `markers` (sorted by source, beats from the content
// start) at `beat`, if it's between the first and last and there's no
// marker there already. Its place in the recording comes from the beats of
// the markers either side: they're in step with the audio's beats (see FlexLayout).
static void addTempoMarkerAt (std::vector<FlexMarker>& markers, double beat)
{
    constexpr double sameBeat = 1.0e-6;

    if (markers.size() < 2 || beat <= markers.front().beat + sameBeat || beat >= markers.back().beat - sameBeat)
        return;

    auto next = std::find_if (markers.begin(), markers.end(), [beat] (const FlexMarker& m) { return m.beat >= beat; });

    if (std::abs (next->beat - beat) < sameBeat || std::abs ((next - 1)->beat - beat) < sameBeat)
        return;

    const auto& prev = *(next - 1);
    const double source = prev.sourceSeconds + (beat - prev.beat) / (next->beat - prev.beat) * (next->sourceSeconds - prev.sourceSeconds);
    markers.insert (next, { source, beat, true });
}

std::optional<FlexLayout> captureFlexLayout (const te::Clip& clip)
{
    auto* wave = asWave (clip);

    if (wave == nullptr || ! isFlexActive (clip))
        return std::nullopt;

    const auto points = getWarpPoints (*wave);

    if (points.size() < 2)
        return std::nullopt;

    auto& edit = clip.edit;
    const auto contentStart = contentStartOf (clip);

    FlexLayout layout;
    layout.startBeat = clip.getStartBeat().inBeats();
    layout.endBeat = clip.getEndBeat().inBeats();
    layout.contentStartBeat = timeToBeat (edit, contentStart);

    auto beatOf = [&] (double contentSeconds)
    {
        return timeToBeat (edit, contentStart + te::TimeDuration::fromSeconds (contentSeconds)) - layout.contentStartBeat;
    };

    for (auto& p : points)
        layout.markers.push_back ({ p.source, beatOf (p.warp), p.tempoMarker });

    // Where the tempo changes now, between two markers, the audio plays
    // evenly in seconds but not in beats. A point there (where the audio
    // plays now, from the markers in seconds) keeps that bend in the layout.
    for (double beat : tempoChangeBeats (edit))
    {
        const double warp = (beatToTime (edit, beat) - contentStart).inSeconds();

        if (warp > points.front().warp && warp < points.back().warp && flexmap::indexOf (points, flexmap::toSource (points, warp)) < 0)
        {
            const FlexMarker m { flexmap::toSource (points, warp), beat - layout.contentStartBeat, true };
            auto at = std::upper_bound (layout.markers.begin(), layout.markers.end(), m.sourceSeconds,
                                        [] (double s, const FlexMarker& other) { return s < other.sourceSeconds; });
            layout.markers.insert (at, m);
        }
    }

    if (clip.isLooping())
    {
        layout.looped = true;
        layout.loopStartBeats = beatOf (wave->getLoopStart().inSeconds());
        layout.loopEndBeats = beatOf ((wave->getLoopStart() + wave->getLoopLength()).inSeconds());
    }

    return layout;
}

void restoreFlexLayout (te::Clip& clip, const FlexLayout& layout, double shiftBeats)
{
    auto* wave = asWave (clip);

    if (wave == nullptr || layout.markers.size() < 2)
        return;

    auto& edit = clip.edit;
    auto* um = &edit.getUndoManager();
    const double contentStartBeat = layout.contentStartBeat + shiftBeats;
    const auto contentStart = beatToTime (edit, contentStartBeat);

    // Tempo markers where the tempo changes now (in beats from the content start)...
    auto markers = layout.markers;
    std::vector<double> changes;

    for (double beat : tempoChangeBeats (edit))
    {
        changes.push_back (beat - contentStartBeat);
        addTempoMarkerAt (markers, changes.back());
    }

    // ...and none where it doesn't any more, unless the audio bends there
    // (its beats aren't in line either side: the tempo used to change there
    // while the markers either side were further apart, say).
    for (size_t i = 1; i + 1 < markers.size();)
    {
        const auto& prev = markers[i - 1];
        const auto& here = markers[i];
        const auto& next = markers[i + 1];
        const double inLine = prev.beat + (here.sourceSeconds - prev.sourceSeconds) / (next.sourceSeconds - prev.sourceSeconds) * (next.beat - prev.beat);
        const bool atChange = std::any_of (changes.begin(), changes.end(), [&here] (double c) { return std::abs (c - here.beat) < 1.0e-6; });

        if (here.tempoMarker && ! atChange && std::abs (inLine - here.beat) < 1.0e-6)
            markers.erase (markers.begin() + (std::ptrdiff_t) i);
        else
            ++i;
    }

    // Seconds into the content of a beat (counted from the content start).
    auto secondsAt = [&] (double beatsFromContentStart)
    {
        return (beatToTime (edit, contentStartBeat + beatsFromContentStart) - contentStart).inSeconds();
    };

    // The markers first: they decide how long the content is.
    std::vector<WarpPoint> points;

    for (auto& m : markers)
        points.push_back ({ m.sourceSeconds, secondsAt (m.beat), m.tempoMarker });

    setWarpPoints (*wave, points);

    // The loop (in the content's seconds). Written straight to the state:
    // setLoopRange clamps to the warped recording's length, which is unknown
    // until its new stretched copy has been made.
    double loopStart = 0.0;

    if (layout.looped)
    {
        loopStart = secondsAt (layout.loopStartBeats);
        const double loopLength = secondsAt (layout.loopEndBeats) - loopStart;

        for (auto [id, value] : { std::pair { te::IDs::loopStart, loopStart }, std::pair { te::IDs::loopLength, loopLength } })
            if (std::abs ((double) clip.state[id] - value) > sameSeconds)
                clip.state.setProperty (id, value, um);
    }

    // Then the region: its edges on their beats, and the content under it.
    const auto start = beatToTime (edit, layout.startBeat + shiftBeats);
    const auto end = beatToTime (edit, layout.endBeat + shiftBeats);
    const auto offset = start - contentStart - te::TimeDuration::fromSeconds (loopStart);
    const auto pos = clip.getPosition();

    if (std::abs ((pos.getStart() - start).inSeconds()) > sameSeconds
         || std::abs ((pos.getEnd() - end).inSeconds()) > sameSeconds
         || std::abs ((pos.getOffset() - offset).inSeconds()) > sameSeconds)
        clip.setPosition ({ { start, end }, offset });
}

void FlexBeatKeeper::capture (te::Edit& edit)
{
    clips.clear();

    for (auto* track : te::getAudioTracks (edit))
        for (auto* clip : track->getClips())
            if (auto layout = captureFlexLayout (*clip))
                clips.emplace_back (clip, std::move (*layout));
}

void FlexBeatKeeper::restore()
{
    for (auto& [clip, layout] : clips)
        restoreFlexLayout (*clip, layout);

    clips.clear();
}

//==============================================================================
void switchTakeWarp (te::WaveAudioClip& clip, juce::ValueTree fromTake, juce::ValueTree toTake)
{
    auto markers = markersTreeOf (clip);

    if (! markers.isValid())
        return;   // never flexed: nothing to swap

    auto* um = &clip.edit.getUndoManager();

    // The take that was playing keeps its markers (a copy, on its TAKE).
    if (fromTake.isValid())
    {
        if (auto old = fromTake.getChildWithName (takeWarpMarkersType); old.isValid())
            fromTake.removeChild (old, um);

        auto saved = markers.createCopy();
        juce::ValueTree holder (takeWarpMarkersType);
        holder.addChild (saved, -1, nullptr);   // not in the Edit yet: no undo needed
        fromTake.addChild (holder, -1, um);
    }

    // The new one gets its own back, or plain ones if it never had any.
    std::vector<WarpPoint> points;

    for (auto m : toTake.getChildWithName (takeWarpMarkersType).getChildWithName (te::IDs::WARPMARKERS))
        points.push_back ({ (double) m[te::IDs::sourceTime], (double) m[te::IDs::warpTime], (bool) m.getProperty (tempoMarkerProp, false) });

    if (points.size() < 2)
    {
        const double length = recordingLengthOf (clip);
        points = { { 0.0, 0.0 }, { length, length } };
    }

    setWarpPoints (clip, points);
}

//==============================================================================
// Turns Flex on or off for a track (in the open undo step).
static void setTrackFlex (te::Track& track, bool shouldBeOn)
{
    auto* um = &track.edit.getUndoManager();

    // Off = no property at all, as in sessions from before Flex.
    if (shouldBeOn)
        track.state.setProperty (flexProp, true, um);
    else
        track.state.removeProperty (flexProp, um);

    if (auto* clipTrack = dynamic_cast<te::ClipTrack*> (&track))
        for (auto* clip : clipTrack->getClips())
            applyTrackFlex (*clip);
}

// Quantize's markers. Adds a marker (where it plays now) on each hit to be
// quantized, and on the hits that hold the rest of the region still, and
// returns where the quantized ones should go: (index in `points`, warp
// seconds), first to last. `hits` are sorted source times; `selection` is
// in beats on the timeline.
static std::vector<std::pair<int, double>> quantizeMoves (te::WaveAudioClip& clip, std::vector<WarpPoint>& points,
                                                          const std::vector<double>& hits, const QuantizeSettings& settings,
                                                          std::optional<juce::Range<double>> selection, const TempoMap& tempo)
{
    auto& edit = clip.edit;
    const auto pos = clip.getPosition();
    const double start = pos.getStart().inSeconds(), end = pos.getEnd().inSeconds();
    const double contentStart = contentStartOf (clip).inSeconds();
    const double loopStart = clip.isLooping() ? clip.getLoopStart().inSeconds() : 0.0;
    const double loopLength = clip.isLooping() ? clip.getLoopLength().inSeconds() : 0.0;
    const bool looped = loopLength > 0.0;

    auto beatAt = [&] (double seconds) { return timeToBeat (edit, te::TimePosition::fromSeconds (seconds)); };

    // A hit to quantize: where it's first played (in the selection, if
    // there is one), in seconds and beats, and its grid line.
    struct Hit
    {
        double source = 0.0, warp = 0.0, heardAt = 0.0, beat = 0.0, line = 0.0;
    };

    std::vector<Hit> chosen;
    std::vector<bool> isChosen (hits.size(), false);   // in the selection (moved or not), by index in `hits`

    for (size_t i = 0; i < hits.size(); ++i)
    {
        const double warp = flexmap::toWarp (points, hits[i]);

        // A looped region only plays its loop, over and over.
        if (looped && (warp < loopStart || warp >= loopStart + loopLength))
            continue;

        for (double t = contentStart + warp; t < end; t += loopLength)
        {
            if (t >= start && (! selection.has_value() || selection->contains (beatAt (t))))
            {
                const double beat = beatAt (t);
                chosen.push_back ({ hits[i], warp, t, beat, quantize::gridLine (beat, settings, tempo) });
                isChosen[i] = true;
                break;
            }

            if (! looped)
                break;
        }
    }

    // Two hits heading for the same grid line (a flam, or hits closer
    // together than the grid): only the nearer one goes there. The other
    // gets no marker of its own, and moves along with the audio around it.
    std::vector<Hit> kept;

    for (auto& hit : chosen)
    {
        if (! kept.empty() && std::abs (kept.back().line - hit.line) < 1.0e-9)
        {
            if (std::abs (hit.beat - hit.line) < std::abs (kept.back().beat - kept.back().line))
                kept.back() = hit;

            continue;
        }

        kept.push_back (hit);
    }

    // With a selection: the hits just outside it get a marker too (unless a
    // marker is already nearer), so only the audio up to them stretches and
    // the rest of the region stays where it is (as a drag does, see
    // flexmap::prepareDrag).
    std::vector<double> anchors;

    if (selection.has_value())
    {
        auto markerBetween = [&points] (double a, double b)
        {
            return std::any_of (points.begin(), points.end(), [&] (const WarpPoint& m)
            {
                return m.source > juce::jmin (a, b) + flexmap::sameMarkerSeconds && m.source < juce::jmax (a, b) - flexmap::sameMarkerSeconds;
            });
        };

        for (size_t i = 0; i < hits.size(); ++i)
        {
            if (isChosen[i])
                continue;

            const bool chosenBefore = i > 0 && isChosen[i - 1];
            const bool chosenAfter = i + 1 < hits.size() && isChosen[i + 1];

            if ((chosenBefore && ! markerBetween (hits[i - 1], hits[i]))
                 || (chosenAfter && ! markerBetween (hits[i], hits[i + 1])))
                anchors.push_back (hits[i]);
        }
    }

    // The markers go in where the audio plays now, so adding them changes
    // nothing by itself...
    for (auto& hit : kept)
        flexmap::insertAtCurrentPlace (points, hit.source);

    for (auto a : anchors)
        flexmap::insertAtCurrentPlace (points, a);

    // ...then each quantized one moves: by as many seconds as it takes to
    // get the hit (where it's heard) to its new beat. Working in beats at
    // the place it's heard is what keeps Quantize right through tempo and
    // time-signature changes.
    std::vector<std::pair<int, double>> moves;

    for (auto& hit : kept)
    {
        const double targetBeat = hit.beat + juce::jlimit (0.0, 1.0, settings.strength) * (hit.line - hit.beat);
        const double shift = beatToTime (edit, targetBeat).inSeconds() - hit.heardAt;
        const int index = flexmap::indexOf (points, hit.source);
        points[(size_t) index].tempoMarker = false;   // a hit on a tempo marker: it's the hit's marker now
        moves.emplace_back (index, hit.warp + shift);
    }

    // Sorted by index (the markers are sorted by source, and so are the hits).
    std::sort (moves.begin(), moves.end());
    return moves;
}

} // namespace detail

//==============================================================================
// FlexEditor
//==============================================================================
using namespace detail;

// A Quantize waiting for its region's hits to be found.
struct FlexEditor::PendingQuantize
{
    QuantizeSettings settings;
    std::optional<juce::Range<double>> selection;   // beats from the region's start: it may move meanwhile
    juce::String file;                              // the recording the hits are being found in

    // Finds the hits while the region has no warp state (see quantize).
    // Reference-counted (::Ptr): Tracktion's warp managers are shared
    // objects, freed when the last Ptr to them goes.
    te::WarpTimeManager::Ptr finder;
};

// What findHits found.
struct FlexEditor::HitSearch
{
    bool found = false;
    std::vector<double> sources;   // sorted, in seconds of the recording
    float progress = -1.0f;        // how far the search has got, 0..1 (-1 = can't tell)
};

FlexEditor::FlexEditor (AudioEngine& o) : owner (o)
{
}

// Defined here, where PendingQuantize is complete: std::unique_ptr has to
// see the whole type to delete it. (juce::Timer's destructor stops the timer.)
FlexEditor::~FlexEditor() = default;

void FlexEditor::sessionChanged()
{
    // The finders refer to the old Edit: they go before it does.
    stopTimer();
    pendingQuantizes.clear();
    hitsFoundForQuantize.clear();
    analysedFiles.clear();
    staleTransients.clear();
}

bool FlexEditor::isFlexOn (TrackId id) const
{
    auto* track = findTrack (owner.getEdit(), id);
    return track != nullptr && isAudioTrack (*track) && isFlexTrack (*track);
}

void FlexEditor::setFlexOn (TrackId id, bool shouldBeOn)
{
    auto* track = findTrack (owner.getEdit(), id);

    if (track == nullptr || ! isAudioTrack (*track) || isFlexTrack (*track) == shouldBeOn)
        return;

    owner.beginUndoStep (shouldBeOn ? "Flex On" : "Flex Off");
    setTrackFlex (*track, shouldBeOn);

    // The user has decided about Flex on this track: a Quantize still
    // waiting for its hits there would turn it back on, so it's dropped.
    for (auto it = pendingQuantizes.begin(); it != pendingQuantizes.end();)
    {
        auto* clip = findClip (owner.getEdit(), it->first);
        it = clip == nullptr || clip->getTrack() == track ? pendingQuantizes.erase (it) : std::next (it);
    }
}

FlexInfo FlexEditor::getFlex (RegionId id) const
{
    FlexInfo info;
    auto* edit = owner.getEdit();
    auto* wave = dynamic_cast<te::WaveAudioClip*> (findClip (edit, id));

    if (wave == nullptr)
        return info;

    auto* track = wave->getTrack();
    info.trackFlex = track != nullptr && isFlexTrack (*track);
    info.unavailableReason = whyClipCantFlex (*wave);
    info.available = info.unavailableReason.isEmpty();
    info.on = info.trackFlex && info.available;
    info.beatsPerSecond = edit->tempoSequence.getBeatsPerSecondAt (wave->getPosition().getStart());

    auto points = getWarpPoints (*wave);

    if (points.size() < 2)
    {
        const double length = recordingLengthOf (*wave);
        points = { { 0.0, 0.0 }, { length, length } };
    }

    info.hasEdits = flexmap::isWarped (points)
                     || std::any_of (points.begin() + 1, points.end() - 1, [] (const WarpPoint& p) { return ! p.tempoMarker; });
    const auto contentStart = contentStartOf (*wave);

    auto beatOf = [&] (double source)
    {
        return timeToBeat (*edit, contentStart + te::TimeDuration::fromSeconds (flexmap::toWarp (points, source)));
    };

    for (auto& p : points)
        info.markers.push_back ({ p.source, beatOf (p.source), p.tempoMarker });

    // The hits: while Flex is on (and the clip's warp state is in place,
    // so asking for the WarpTimeManager doesn't make one), or Quantize is
    // waiting for them.
    info.quantizePending = pendingQuantizes.count (id) > 0;

    if (! info.quantizePending && (! info.on || ! markersTreeOf (*wave).isValid()))
        return info;

    const auto hits = findHits (id);
    info.analysing = ! hits.found;
    info.analysisProgress = hits.progress;

    for (auto s : hits.sources)
        info.transients.push_back ({ s, beatOf (s) });

    auto& engine = owner.getTeEngine();

    if (! info.on)
        return info;

    // The stretched copy of the recording, while it's being made (the
    // region is silent until then). It's named after the markers (the
    // clip's hash), and Tracktion only starts on it a moment after they
    // change, so look for that file rather than at what the clip plays now.
    // Tracktion only says how far it's got in its message, e.g. "Warping: 45%".
    const auto stretched = te::RenderManager::getAudioFileForHash (engine, edit->getTempDirectory (false), wave->getHash());

    if (wave->getWarpTime() && ! (stretched.isValid() && wave->getAudioFile() == stretched))
    {
        const auto message = wave->getRenderMessage();
        info.stretchProgress = message.containsChar ('%')
                                 ? juce::jlimit (0.0f, 1.0f, message.fromLastOccurrenceOf (":", false, false).getFloatValue() / 100.0f)
                                 : 0.0f;
    }

    return info;
}

std::vector<FlexMarker> FlexEditor::previewMove (const FlexInfo& info, double sourceSeconds, double newBeat)
{
    // The same steps as moveMarker, in beats rather than seconds (the same,
    // unless the tempo changes inside the region).
    std::vector<WarpPoint> points;
    std::vector<double> transients;

    for (auto& m : info.markers)
        points.push_back ({ m.sourceSeconds, m.beat, m.tempoMarker });

    for (auto& t : info.transients)
        transients.push_back (t.sourceSeconds);

    if (points.size() >= 2)
    {
        const int index = flexmap::prepareDrag (points, transients, sourceSeconds);

        if (index > 0 && index < (int) points.size() - 1)
            points[(size_t) index].warp = flexmap::limitMove (points, index, newBeat, info.beatsPerSecond);
    }

    std::vector<FlexMarker> result;

    for (auto& p : points)
        result.push_back ({ p.source, p.warp, p.tempoMarker });

    return result;
}

bool FlexEditor::moveMarker (RegionId id, double sourceSeconds, double newBeat)
{
    auto* edit = owner.getEdit();
    auto* wave = dynamic_cast<te::WaveAudioClip*> (findClip (edit, id));

    if (wave == nullptr || ! isFlexActive (*wave) || ! markersTreeOf (*wave).isValid())
        return false;

    const auto info = getFlex (id);
    std::vector<double> transients;

    for (auto& t : info.transients)
        transients.push_back (t.sourceSeconds);

    const auto before = getWarpPoints (*wave);
    auto points = before;
    const int index = flexmap::prepareDrag (points, transients, sourceSeconds);

    if (index <= 0 || index >= (int) points.size() - 1)
        return false;   // the ends of the recording stay where they are

    const double wanted = (beatToTime (*edit, newBeat) - contentStartOf (*wave)).inSeconds();
    points[(size_t) index].warp = flexmap::limitMove (points, index, wanted);
    points[(size_t) index].tempoMarker = false;   // dragged: it's an ordinary marker now

    if (std::abs (points[(size_t) index].warp - flexmap::toWarp (before, sourceSeconds)) < flexmap::sameMarkerSeconds)
        return false;   // it didn't move (a click, or held against a neighbour)

    owner.beginUndoStep ("Move Flex Marker");
    setWarpPoints (*wave, points);
    return true;
}

bool FlexEditor::removeMarker (RegionId id, double sourceSeconds)
{
    auto* wave = dynamic_cast<te::WaveAudioClip*> (findClip (owner.getEdit(), id));

    if (wave == nullptr || ! isFlexActive (*wave))
        return false;

    auto points = getWarpPoints (*wave);
    const int index = flexmap::indexOf (points, sourceSeconds);

    // (A tempo marker isn't the user's to remove: it keeps the audio on its beats.)
    if (index <= 0 || index >= (int) points.size() - 1 || points[(size_t) index].tempoMarker)
        return false;

    points.erase (points.begin() + index);
    owner.beginUndoStep ("Remove Flex Marker");
    setWarpPoints (*wave, points);
    return true;
}

bool FlexEditor::resetFlex (RegionId id)
{
    auto* wave = dynamic_cast<te::WaveAudioClip*> (findClip (owner.getEdit(), id));

    if (wave == nullptr || ! markersTreeOf (*wave).isValid())
        return false;

    const auto points = getWarpPoints (*wave);

    if (points.size() <= 2 && ! flexmap::isWarped (points))
        return false;

    // Back to a marker at each end of the recording, each in its own place.
    const double length = recordingLengthOf (*wave);
    owner.beginUndoStep ("Reset Flex");
    setWarpPoints (*wave, { { 0.0, 0.0 }, { length, length } });
    return true;
}

//==============================================================================
// Finding the hits
//==============================================================================
FlexEditor::HitSearch FlexEditor::findHits (RegionId id) const
{
    HitSearch result;
    auto* wave = dynamic_cast<te::WaveAudioClip*> (findClip (owner.getEdit(), id));

    if (wave == nullptr)
        return result;

    // Hit-finding jobs have no output file, so they're the render jobs for
    // an empty AudioFile. (Several regions may be analysed at once; this
    // shows how far the first one has got.)
    auto& engine = owner.getTeEngine();
    const auto jobs = engine.getRenderManager().getRenderJobsWithoutCreating (te::AudioFile (engine));

    if (! jobs.isEmpty())
        result.progress = jobs.getFirst()->getCurrentTaskProgress();

    const auto file = wave->getOriginalFile().getFullPathName();
    std::pair<bool, juce::Array<te::TimePosition>> times;

    if (markersTreeOf (*wave).isValid())
    {
        // The clip's own WarpTimeManager (it has warp state, so asking for
        // it changes nothing in the session).
        auto& manager = wave->getWarpTimeManager();
        times = manager.getTransientTimes();

        // Another take chosen: find the hits in its recording.
        auto& analysed = analysedFiles[id];

        if (analysed.isNotEmpty() && analysed != file)
        {
            manager.editFinishedLoading();   // starts looking again, in the clip's current file

            std::vector<double> old;

            for (auto t : times.second)
                old.push_back (t.inSeconds());

            staleTransients[id] = std::move (old);
        }

        analysed = file;

        // Until the new search finishes, the hits found are the old file's.
        if (auto stale = staleTransients.find (id); stale != staleTransients.end())
        {
            std::vector<double> now;

            for (auto t : times.second)
                now.push_back (t.inSeconds());

            if (stale->second == now && ! jobs.isEmpty())
                times.first = false;
            else
                staleTransients.erase (stale);
        }
    }
    else if (auto pending = pendingQuantizes.find (id);
             pending != pendingQuantizes.end() && pending->second->finder != nullptr && pending->second->file == file)
    {
        times = pending->second->finder->getTransientTimes();
    }

    if (times.first)
    {
        for (auto t : times.second)
            result.sources.push_back (t.inSeconds());

        std::sort (result.sources.begin(), result.sources.end());
        const double length = recordingLengthOf (*wave);
        result.sources.erase (std::remove_if (result.sources.begin(), result.sources.end(),
                                              [length] (double s) { return s < 0.0 || s >= length; }),
                              result.sources.end());
        result.found = true;

        // Kept for when Flex comes on: then the clip's own manager looks
        // for the same hits again, and the region needn't wait for them.
        if (! markersTreeOf (*wave).isValid())
            hitsFoundForQuantize[id] = { file, result.sources };
        else
            hitsFoundForQuantize.erase (id);
    }
    else if (auto known = hitsFoundForQuantize.find (id); known != hitsFoundForQuantize.end() && known->second.first == file)
    {
        result.sources = known->second.second;
        result.found = true;
    }

    return result;
}

//==============================================================================
// Quantize
//==============================================================================
FlexQuantizeResult FlexEditor::quantize (RegionId id, const QuantizeSettings& settings, std::optional<juce::Range<double>> selection)
{
    auto* wave = dynamic_cast<te::WaveAudioClip*> (findClip (owner.getEdit(), id));

    if (wave == nullptr || whyClipCantFlex (*wave).isNotEmpty())
        return FlexQuantizeResult::unavailable;

    // Kept from the region's start, so it stays on the same audio if the
    // region is moved while the hits are being found.
    std::optional<juce::Range<double>> fromStart;

    if (selection.has_value())
        fromStart = selection->movedToStartAt (selection->getStart() - wave->getStartBeat().inBeats());

    const auto file = wave->getOriginalFile().getFullPathName();
    const auto hits = findHits (id);   // (a waiting request may have found them by now)

    // Asking again replaces a waiting request, but keeps its search going.
    te::WarpTimeManager::Ptr finder;

    if (auto waiting = pendingQuantizes.find (id); waiting != pendingQuantizes.end())
    {
        if (waiting->second->file == file)
            finder = waiting->second->finder;

        pendingQuantizes.erase (waiting);
    }

    if (hits.found)
        return applyQuantize (id, hits.sources, settings, fromStart);

    // The hits aren't known yet: Quantize when they are.
    auto request = std::make_unique<PendingQuantize>();
    request->settings = settings;
    request->selection = fromStart;
    request->file = file;
    request->finder = finder;

    if (! markersTreeOf (*wave).isValid() && finder == nullptr)
    {
        // The region has never been flexed, so it has no warp state, and
        // asking for its WarpTimeManager would add that state to the session
        // straight away, as an undo step of its own (finding the hits takes
        // a while, so it couldn't be part of the Quantize step). So the hits
        // are found by a WarpTimeManager of our own instead, kept outside
        // the session: it keeps its markers in a tree of its own, which
        // already holds the two it would otherwise add (through the undo
        // manager), so it never touches the session or its undo history.
        juce::ValueTree holder ("IKAROSHITFINDER");
        holder.appendChild (makeWarpState (recordingLengthOf (*wave)), nullptr);

        auto& engine = owner.getTeEngine();
        request->finder = new te::WarpTimeManager (wave->edit, te::AudioFile (engine, wave->getOriginalFile()), holder);
        request->finder->editFinishedLoading();   // starts looking, on a background thread
    }

    pendingQuantizes[id] = std::move (request);
    startTimer (100);   // checks how it's getting on (timerCallback)
    return FlexQuantizeResult::findingHits;
}

FlexQuantizeResult FlexEditor::applyQuantize (RegionId id, const std::vector<double>& hits, const QuantizeSettings& settings,
                                              std::optional<juce::Range<double>> selectionFromStart)
{
    auto* wave = dynamic_cast<te::WaveAudioClip*> (findClip (owner.getEdit(), id));

    if (wave == nullptr || whyClipCantFlex (*wave).isNotEmpty())
        return FlexQuantizeResult::unavailable;

    // The markers as they'll play once Flex is on: the region's own (kept
    // while Flex was off), or one at each end of the recording.
    auto points = getWarpPoints (*wave);

    if (points.size() < 2)
    {
        const double length = recordingLengthOf (*wave);
        points = { { 0.0, 0.0 }, { length, length } };
    }

    const auto before = points;
    std::optional<juce::Range<double>> selection;

    if (selectionFromStart.has_value())
        selection = selectionFromStart->movedToStartAt (selectionFromStart->getStart() + wave->getStartBeat().inBeats());

    const auto moves = quantizeMoves (*wave, points, hits, settings, selection, owner.getTempoMap());
    flexmap::moveMarkers (points, moves);

    // Worked out before the undo step starts, so a Quantize that changes
    // nothing leaves nothing to undo (and doesn't turn Flex on).
    const bool changes = std::any_of (points.begin(), points.end(), [&before] (const WarpPoint& m)
    {
        return std::abs (m.warp - flexmap::toWarp (before, m.source)) > flexmap::sameMarkerSeconds;
    });

    if (! changes)
        return FlexQuantizeResult::alreadyOnGrid;

    // One step: Flex on (if it was off), then the markers.
    owner.beginUndoStep ("Quantize");

    if (auto* track = wave->getTrack(); track != nullptr && ! isFlexTrack (*track))
        setTrackFlex (*track, true);

    applyTrackFlex (*wave);   // makes sure the region has its warp state
    setWarpPoints (*wave, points);
    return FlexQuantizeResult::quantized;
}

void FlexEditor::timerCallback()
{
    // Not while a mouse button is down or a take is recording: a drag (or
    // the take) has its own undo step open, and Quantize's would split it.
    if (juce::ModifierKeys::currentModifiers.isAnyMouseButtonDown() || owner.getTransportState().recording)
        return;

    for (auto it = pendingQuantizes.begin(); it != pendingQuantizes.end();)
    {
        const auto id = it->first;
        auto* wave = dynamic_cast<te::WaveAudioClip*> (findClip (owner.getEdit(), id));

        // The region has gone, can't be flexed any more, or plays another
        // take now: there's nothing left to quantize.
        if (wave == nullptr || whyClipCantFlex (*wave).isNotEmpty()
             || wave->getOriginalFile().getFullPathName() != it->second->file)
        {
            it = pendingQuantizes.erase (it);
            continue;
        }

        auto hits = findHits (id);

        if (! hits.found)
        {
            ++it;
            continue;
        }

        // Taken out of the list (finder and all) before the session changes.
        const auto request = std::move (it->second);
        it = pendingQuantizes.erase (it);
        applyQuantize (id, hits.sources, request->settings, request->selection);
    }

    if (pendingQuantizes.empty())
        stopTimer();
}

} // namespace ikaros
