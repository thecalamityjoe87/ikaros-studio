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
        points.push_back ({ (double) marker[te::IDs::sourceTime], (double) marker[te::IDs::warpTime] });

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

// The clip's WARPTIME tree, made if it hasn't got one.
static void ensureWarpState (te::WaveAudioClip& clip)
{
    // The clip's WarpTimeManager is made the first time it's asked for, and
    // then lives as long as the clip (it's reference-counted, and the clip
    // holds on to it). The first time, it adds the WARPTIME tree with a
    // marker at each end of the recording (in the open undo step) and starts
    // looking for the hits in the background.
    auto& manager = clip.getWarpTimeManager();

    // An undo can take the tree out of the clip again (it was added in the
    // undone step), but the manager keeps referring to that same tree. Put
    // it back, rather than making a new one the manager wouldn't know about.
    if (! manager.state.getParent().isValid())
        clip.state.addChild (manager.state, -1, &clip.edit.getUndoManager());
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

    for (int i = 0; i < common; ++i)
    {
        setIfDifferent (list.getChild (i), te::IDs::sourceTime, points[(size_t) i].source);
        setIfDifferent (list.getChild (i), te::IDs::warpTime, points[(size_t) i].warp);
    }

    while (list.getNumChildren() > (int) points.size())
        list.removeChild (list.getNumChildren() - 1, um);

    for (size_t i = (size_t) common; i < points.size(); ++i)
        list.addChild (te::createValueTree (te::IDs::WARPMARKER,
                                            te::IDs::sourceTime, points[i].source,
                                            te::IDs::warpTime, points[i].warp), -1, um);

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
        layout.markerBeats.push_back (beatOf (p.warp));

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

    if (wave == nullptr)
        return;

    auto points = getWarpPoints (*wave);

    if (points.size() != layout.markerBeats.size())
        return;   // the markers changed in between: nothing to put back

    auto& edit = clip.edit;
    auto* um = &edit.getUndoManager();
    const double contentStartBeat = layout.contentStartBeat + shiftBeats;
    const auto contentStart = beatToTime (edit, contentStartBeat);

    // Seconds into the content of a beat (counted from the content start).
    auto secondsAt = [&] (double beatsFromContentStart)
    {
        return (beatToTime (edit, contentStartBeat + beatsFromContentStart) - contentStart).inSeconds();
    };

    // The markers first: they decide how long the content is.
    for (size_t i = 0; i < points.size(); ++i)
        points[i].warp = secondsAt (layout.markerBeats[i]);

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
        points.push_back ({ (double) m[te::IDs::sourceTime], (double) m[te::IDs::warpTime] });

    if (points.size() < 2)
    {
        const double length = recordingLengthOf (clip);
        points = { { 0.0, 0.0 }, { length, length } };
    }

    setWarpPoints (clip, points);
}

} // namespace detail

//==============================================================================
// FlexEditor
//==============================================================================
using namespace detail;

FlexEditor::FlexEditor (AudioEngine& o) : owner (o)
{
}

void FlexEditor::sessionChanged()
{
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
    auto* um = &track->edit.getUndoManager();

    // Off = no property at all, as in sessions from before Flex.
    if (shouldBeOn)
        track->state.setProperty (flexProp, true, um);
    else
        track->state.removeProperty (flexProp, um);

    for (auto* clip : track->getClips())
        applyTrackFlex (*clip);
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

    info.hasEdits = points.size() > 2 || flexmap::isWarped (points);
    const auto contentStart = contentStartOf (*wave);

    auto beatOf = [&] (double source)
    {
        return timeToBeat (*edit, contentStart + te::TimeDuration::fromSeconds (flexmap::toWarp (points, source)));
    };

    for (auto& p : points)
        info.markers.push_back ({ p.source, beatOf (p.source) });

    // The hits: only while Flex is on (and the clip's warp state is in place,
    // so asking for the WarpTimeManager doesn't make one).
    if (! info.on || ! markersTreeOf (*wave).isValid())
        return info;

    auto& manager = wave->getWarpTimeManager();
    auto [found, times] = manager.getTransientTimes();

    // Hit-finding jobs have no output file, so they're the render jobs for
    // an empty AudioFile. (Several regions may be analysed at once; this
    // shows how far the first one has got.)
    auto& engine = owner.getTeEngine();
    const auto jobs = engine.getRenderManager().getRenderJobsWithoutCreating (te::AudioFile (engine));

    if (! jobs.isEmpty())
        info.analysisProgress = jobs.getFirst()->getCurrentTaskProgress();

    // Another take chosen: find the hits in its recording.
    const auto file = wave->getOriginalFile().getFullPathName();
    auto& analysed = analysedFiles[id];

    if (analysed.isNotEmpty() && analysed != file)
    {
        manager.editFinishedLoading();   // starts looking again, in the clip's current file

        std::vector<double> old;

        for (auto t : times)
            old.push_back (t.inSeconds());

        staleTransients[id] = std::move (old);
    }

    analysed = file;
    std::vector<double> sources;

    for (auto t : times)
        sources.push_back (t.inSeconds());

    // Until the new search finishes, the hits found are the old file's.
    if (auto stale = staleTransients.find (id); stale != staleTransients.end())
    {
        if (stale->second == sources && ! jobs.isEmpty())
            found = false;
        else
            staleTransients.erase (stale);
    }

    info.analysing = ! found;

    if (found)
    {
        std::sort (sources.begin(), sources.end());
        const double length = recordingLengthOf (*wave);

        for (auto s : sources)
            if (s >= 0.0 && s < length)
                info.transients.push_back ({ s, beatOf (s) });
    }

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
        points.push_back ({ m.sourceSeconds, m.beat });

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
        result.push_back ({ p.source, p.warp });

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

    if (index <= 0 || index >= (int) points.size() - 1)
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

} // namespace ikaros
