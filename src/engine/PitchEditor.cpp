#include "engine/PitchEditor.h"
#include "engine/AudioEngine.h"
#include "engine/FlexEditor.h"
#include "engine/FlexMap.h"
#include "engine/PitchRender.h"
#include "engine/TakeHelpers.h"
#include "engine/TrackHelpers.h"
#include "engine/TunePlugin.h"
#include "engine/SessionBundle.h"
#include <tracktion_engine/tracktion_engine.h>

namespace te = tracktion;

namespace ikaros
{

using namespace detail;

// The corrections: an IKAROSPITCH child of the clip, with one NOTE child per
// corrected note (named by its start in the recording):
//   <IKAROSPITCH mode="1" transitions="0">
//     <NOTE start="1.2345" shift="0.3" snap="0.7" flatten="0"/>
//   </IKAROSPITCH>
// And on the clip itself, once it has played a render: the recording it
// was made from (a path like the clip's source). It's on the clip, not
// inside IKAROSPITCH, so undoing the IKAROSPITCH away never loses it.
const juce::Identifier PitchEditor::pitchStateType ("IKAROSPITCH");
const juce::Identifier PitchEditor::originalProp ("ikarosPitchOriginal");

namespace
{
    const juce::Identifier noteType ("NOTE");
    const juce::Identifier modeProp ("mode"), transitionsProp ("transitions");
    const juce::Identifier startProp ("start"), shiftProp ("shift"), snapProp ("snap"), flattenProp ("flatten");

    te::WaveAudioClip* waveOf (te::Edit* edit, RegionId id)
    {
        return dynamic_cast<te::WaveAudioClip*> (findClip (edit, id));
    }

    juce::ValueTree pitchTreeOf (const te::Clip& clip)
    {
        return clip.state.getChildWithName (PitchEditor::pitchStateType);
    }

    juce::String whyNoPitch (te::WaveAudioClip& clip)
    {
        if (clip.getAutoTempo() || clip.getAutoPitch())
            return "This loop follows the song's tempo and key by itself, so Pitch mode can't change it. "
                   "Pitch mode works on recordings and audio files.";

        if (clip.getIsReversed())
            return "Pitch mode doesn't work on a region that plays backwards.";

        if (countTakes (clip) > 1)
            return "Pitch mode works on a region with one take. Right-click the region and choose "
                   "Flatten (Keep Only This Take) first.";

        return {};
    }

    // Two analyses' notes are the same note if they start within half a frame.
    bool sameNote (double a, double b)   { return std::abs (a - b) < 0.003; }

    NoteCorrection correctionFrom (const juce::ValueTree& note)
    {
        return { (float) note.getProperty (shiftProp, 0.0f), (float) note.getProperty (snapProp, 0.0f),
                 (float) note.getProperty (flattenProp, 0.0f) };
    }

    // The FNV-1a hash again (see PitchAnalysis.cpp): stable on every system.
    juce::uint64 hashOf (const void* data, size_t size, juce::uint64 hash = 0xcbf29ce484222325ULL)
    {
        auto* bytes = static_cast<const juce::uint8*> (data);

        for (size_t i = 0; i < size; ++i)
            hash = (hash ^ bytes[i]) * 0x100000001b3ULL;

        return hash;
    }
}

//==============================================================================
// A recording's analysis, made (or read from its cache) on a pool thread.
// The message thread reads `result` only once `finished` is set; atomics
// order the two, so no lock is needed.
struct PitchEditor::AnalysisJob  : public juce::ThreadPoolJob
{
    AnalysisJob (te::Engine& e, const juce::File& r, const juce::File& c)
        : juce::ThreadPoolJob ("Pitch analysis"), engine (e), recording (r), cache (c) {}

    JobStatus runJob() override
    {
        const auto fingerprint = pitchanalysis::fingerprintOf (recording);

        if (auto cached = pitchanalysis::readCache (cache, fingerprint))
        {
            result = std::make_shared<const PitchAnalysis> (std::move (*cached));
            fromCache = true;
        }
        else if (std::unique_ptr<juce::AudioFormatReader> reader { engine.getAudioFileFormatManager().readFormatManager.createReaderFor (recording) })
        {
            if (auto analysed = pitchanalysis::analyse (*reader, &progress, &cancel))
            {
                pitchanalysis::writeCache (cache, *analysed, fingerprint);
                result = std::make_shared<const PitchAnalysis> (std::move (*analysed));
            }
        }

        this->fingerprint = fingerprint;
        finished = true;
        return jobHasFinished;
    }

    te::Engine& engine;
    const juce::File recording, cache;
    std::shared_ptr<const PitchAnalysis> result;   // empty if it failed
    juce::uint64 fingerprint = 0;
    bool fromCache = false;
    std::atomic<float> progress { 0.0f };
    std::atomic<bool> finished { false }, cancel { false };
};

// A render (see PitchRender.h), on a pool thread.
struct PitchEditor::RenderJob  : public juce::ThreadPoolJob
{
    explicit RenderJob (PitchRenderRequest r) : juce::ThreadPoolJob ("Pitch render"), request (std::move (r)) {}

    JobStatus runJob() override
    {
        ok = pitchrender::render (request, &progress, &cancel);
        finished = true;
        return jobHasFinished;
    }

    PitchRenderRequest request;
    RegionId region = 0;   // the region it's for
    std::atomic<float> progress { 0.0f };
    std::atomic<bool> finished { false }, ok { false }, cancel { false };
};

//==============================================================================
PitchEditor::PitchEditor (AudioEngine& o, FlexEditor& f)
    : owner (o), flex (f),
      pool (std::make_unique<juce::ThreadPool> (juce::ThreadPoolOptions().withThreadName ("Pitch").withNumberOfThreads (2)))
{
    startTimerHz (10);
}

PitchEditor::~PitchEditor()
{
    sessionChanged();
}

void PitchEditor::sessionChanged()
{
    // Stop the jobs (they check `cancel` as they go) and wait for them.
    for (auto& [path, job] : analyses)
        job->cancel = true;

    for (auto& [path, job] : renders)
        job->cancel = true;

    pool->removeAllJobs (true, 10000);
    analyses.clear();
    renders.clear();
    renderedCurves.clear();
    failedRenders.clear();
    settling.clear();
}

juce::File PitchEditor::cacheFileFor (const juce::File& audioFolder, const juce::File& recording)
{
    return audioFolder.getChildFile (recording.getFileName() + ".ikpitch");
}

std::shared_ptr<PitchEditor::AnalysisJob> PitchEditor::analysisFor (const juce::File& recording, bool start) const
{
    const auto key = recording.getFullPathName();

    if (auto found = analyses.find (key); found != analyses.end())
    {
        // A failed analysis (the file was missing then) is tried again.
        if (! found->second->finished || found->second->result != nullptr || ! start)
            return found->second;

        analyses.erase (found);
    }

    if (! start || ! recording.existsAsFile())
        return nullptr;

    auto audioFolder = owner.getWorkingCopyFolder().getChildFile (SessionBundle::audioFolder);
    audioFolder.createDirectory();
    auto job = std::make_shared<AnalysisJob> (owner.getTeEngine(), recording, cacheFileFor (audioFolder, recording));
    analyses[key] = job;

    // The pool doesn't own the job (false): the map does, so it outlives the run.
    pool->addJob (job.get(), false);
    return job;
}

//==============================================================================
// Reading
namespace
{
    juce::File originalOf (te::WaveAudioClip& clip)
    {
        if (clip.state.hasProperty (PitchEditor::originalProp))
            return te::SourceFileReference::findFileFromString (clip.edit, clip.state[PitchEditor::originalProp].toString());

        return clip.getSourceFileReference().getFile();
    }

    int transposeOf (te::WaveAudioClip& clip)   { return juce::roundToInt (clip.getPitchChange()); }

    std::pair<int, bool> songKeyOf (te::Edit& edit)
    {
        if (auto* pitch = edit.pitchSequence.getPitch (0))
            return { ((pitch->getPitch() % 12) + 12) % 12,
                     pitch->getScale() == te::Scale::minor || pitch->getScale() == te::Scale::aeolian };

        return { 0, false };
    }

    // Every note's correction, in the analysis's order.
    std::vector<NoteCorrection> correctionsFor (const PitchAnalysis& a, const juce::ValueTree& tree)
    {
        std::vector<NoteCorrection> result (a.notes.size());

        for (auto note : tree)
        {
            if (! note.hasType (noteType))
                continue;

            const double start = note[startProp];

            for (size_t i = 0; i < a.notes.size(); ++i)
                if (sameNote (a.notes[i].start, start))
                    result[i] = correctionFrom (note);
        }

        return result;
    }

    bool hasAnyCorrection (const juce::ValueTree& tree)
    {
        for (auto note : tree)
            if (note.hasType (noteType) && ! correctionFrom (note).isUntouched())
                return true;

        return false;
    }
}

PitchInfo PitchEditor::getPitch (RegionId id) const
{
    PitchInfo info;
    auto* wave = waveOf (owner.getEdit(), id);

    if (wave == nullptr)
    {
        info.unavailableReason = "Pitch mode works on audio regions.";
        return info;
    }

    info.unavailableReason = whyNoPitch (*wave);
    const auto original = originalOf (*wave);

    if (info.unavailableReason.isEmpty() && ! original.existsAsFile())
        info.unavailableReason = "The region's recording can't be found.";

    info.available = info.unavailableReason.isEmpty();

    const auto tree = pitchTreeOf (*wave);
    info.on = info.available && (bool) tree.getProperty (modeProp, false);
    info.transitions = (NoteTransitions) juce::jlimit (0, 2, (int) tree.getProperty (transitionsProp, 0));
    info.hasCorrections = hasAnyCorrection (tree);
    info.transpose = transposeOf (*wave);
    std::tie (info.keyRoot, info.keyMinor) = songKeyOf (wave->edit);
    info.keyNotes = TunePlugin::scaleNotes (info.keyRoot, info.keyMinor);

    if (! info.available || ! (info.on || info.hasCorrections))
        return info;

    // The analysis: started the first time the region's shown in Pitch mode.
    auto job = analysisFor (original, info.on);

    if (job == nullptr)
        return info;

    if (! job->finished)
    {
        info.analysing = true;
        info.analysisProgress = job->progress;
        return info;
    }

    if (job->result == nullptr)
        return info;

    const auto& a = *job->result;
    info.analysed = true;
    info.singleVoice = a.singleVoice;
    info.fromCache = job->fromCache;

    // The render the region's waiting for, if any.
    for (auto& [path, render] : renders)
    {
        if (! render->finished && ! render->cancel && render->region == id)
        {
            info.rendering = true;
            info.renderProgress = render->progress;
        }
    }

    // Where each moment of the recording plays, through the region's Flex
    // markers (or none: then straight through).
    std::vector<WarpPoint> map;

    for (auto& m : flex.getFlex (id).markers)
        map.push_back ({ m.sourceSeconds, m.beat });

    auto beatOf = [&map] (double seconds) { return flexmap::toWarp (map, seconds); };

    const auto corrections = correctionsFor (a, tree);
    const auto transpose = (float) info.transpose;

    for (size_t i = 0; i < a.notes.size(); ++i)
    {
        const auto& n = a.notes[i];
        PitchNoteInfo note;
        note.sourceStart = n.start;
        note.sourceEnd = n.end;
        note.startBeat = beatOf (n.start);
        note.endBeat = beatOf (n.end);
        note.sung = n.centre + transpose;
        note.correction = corrections[i];
        note.corrected = pitchcurve::correctedCentre (n, corrections[i], info.keyNotes, info.transpose) + transpose;
        note.vibratoCents = n.vibratoCents;

        for (int f = n.firstFrame; f < n.firstFrame + n.numFrames; ++f)
            if (const auto p = a.pitch[(size_t) f]; p > 0.0f)
                note.curve.push_back ({ beatOf (a.frameTime (f)), p + transpose });

        info.notes.push_back (std::move (note));
    }

    return info;
}

bool PitchEditor::isPitchModeOn (RegionId id) const
{
    return getPitch (id).on;
}

bool PitchEditor::isBusy() const
{
    for (auto& [path, job] : analyses)
        if (! job->finished)
            return true;

    for (auto& [path, job] : renders)
        if (! job->finished)
            return true;

    return false;
}

//==============================================================================
// Edits
void PitchEditor::setPitchMode (RegionId id, bool on)
{
    auto* wave = waveOf (owner.getEdit(), id);

    if (wave == nullptr || ! whyNoPitch (*wave).isEmpty())
        return;

    auto* um = &wave->edit.getUndoManager();
    auto tree = pitchTreeOf (*wave);

    if ((bool) tree.getProperty (modeProp, false) == on)
        return;

    owner.beginUndoStep (on ? "Pitch On" : "Pitch Off");

    if (! tree.isValid())
    {
        tree = juce::ValueTree (pitchStateType);
        tree.setProperty (modeProp, on, nullptr);   // not in the clip yet: the append below is the undoable part
        wave->state.appendChild (tree, um);
    }
    else
    {
        tree.setProperty (modeProp, on, um);
    }

    if (on)
        analysisFor (originalOf (*wave), true);
}

// Changes some notes' corrections. `change` gets each chosen note's
// correction and changes it; returns true if anything did.
template <typename Change>
static bool changeNotes (AudioEngine& owner, te::WaveAudioClip& wave, const PitchAnalysis& a,
                         const std::vector<double>& chosen, const juce::String& undoName, Change&& change)
{
    auto* um = &wave.edit.getUndoManager();
    auto tree = pitchTreeOf (wave);
    auto corrections = correctionsFor (a, tree);
    bool begun = false, changed = false;

    for (size_t i = 0; i < a.notes.size(); ++i)
    {
        const auto& n = a.notes[i];

        if (! chosen.empty() && std::none_of (chosen.begin(), chosen.end(), [&] (double s) { return sameNote (s, n.start); }))
            continue;

        auto c = corrections[i];
        change (n, c);

        if (c == corrections[i])
            continue;

        if (! begun && undoName.isNotEmpty())
            owner.beginUndoStep (undoName);

        begun = true;
        changed = true;

        if (! tree.isValid())
        {
            tree = juce::ValueTree (PitchEditor::pitchStateType);
            wave.state.appendChild (tree, um);
        }

        juce::ValueTree note;

        for (auto child : tree)
            if (child.hasType (noteType) && sameNote ((double) child[startProp], n.start))
                note = child;

        if (! note.isValid())
        {
            note = juce::ValueTree (noteType);
            note.setProperty (startProp, n.start, nullptr);
            tree.appendChild (note, um);
        }

        note.setProperty (shiftProp, c.shift, um);
        note.setProperty (snapProp, c.snap, um);
        note.setProperty (flattenProp, c.flatten, um);
    }

    return changed;
}

bool PitchEditor::moveNotes (RegionId id, const std::vector<double>& notes, float semitones)
{
    auto* wave = waveOf (owner.getEdit(), id);
    auto job = wave != nullptr ? analysisFor (originalOf (*wave), false) : nullptr;

    if (job == nullptr || job->result == nullptr || semitones == 0.0f)
        return false;

    return changeNotes (owner, *wave, *job->result, notes, notes.size() == 1 ? "Move Note" : "Move Notes",
                        [semitones] (const PitchNote&, NoteCorrection& c) { c.shift += semitones; });
}

bool PitchEditor::centreNotes (RegionId id, const std::vector<double>& notes)
{
    auto* wave = waveOf (owner.getEdit(), id);
    auto job = wave != nullptr ? analysisFor (originalOf (*wave), false) : nullptr;

    if (job == nullptr || job->result == nullptr)
        return false;

    const auto [root, minor] = songKeyOf (wave->edit);
    const int keyNotes = TunePlugin::scaleNotes (root, minor);
    const int transpose = transposeOf (*wave);

    return changeNotes (owner, *wave, *job->result, notes, "Center Pitch", [&] (const PitchNote& n, NoteCorrection& c)
    {
        // The centring is the whole move: Snap to Key has nothing left to do.
        c.shift = pitchcurve::centringShift (n, c.snap > 0.0f, keyNotes, transpose);
    });
}

bool PitchEditor::setSnap (RegionId id, const std::vector<double>& notes, float amount)
{
    auto* wave = waveOf (owner.getEdit(), id);
    auto job = wave != nullptr ? analysisFor (originalOf (*wave), false) : nullptr;

    if (job == nullptr || job->result == nullptr)
        return false;

    amount = juce::jlimit (0.0f, 1.0f, amount);
    return changeNotes (owner, *wave, *job->result, notes, {}, [amount] (const PitchNote&, NoteCorrection& c) { c.snap = amount; });
}

bool PitchEditor::setFlatten (RegionId id, const std::vector<double>& notes, float amount)
{
    auto* wave = waveOf (owner.getEdit(), id);
    auto job = wave != nullptr ? analysisFor (originalOf (*wave), false) : nullptr;

    if (job == nullptr || job->result == nullptr)
        return false;

    amount = juce::jlimit (0.0f, 1.0f, amount);
    return changeNotes (owner, *wave, *job->result, notes, {}, [amount] (const PitchNote&, NoteCorrection& c) { c.flatten = amount; });
}

bool PitchEditor::correctAll (RegionId id, float snap, float flatten)
{
    auto* wave = waveOf (owner.getEdit(), id);
    auto job = wave != nullptr ? analysisFor (originalOf (*wave), false) : nullptr;

    if (job == nullptr || job->result == nullptr)
        return false;

    snap = juce::jlimit (0.0f, 1.0f, snap);
    flatten = juce::jlimit (0.0f, 1.0f, flatten);

    return changeNotes (owner, *wave, *job->result, {}, "Correct All", [=] (const PitchNote&, NoteCorrection& c)
    {
        c.snap = snap;
        c.flatten = flatten;
    });
}

void PitchEditor::setTransitions (RegionId id, NoteTransitions t)
{
    auto* wave = waveOf (owner.getEdit(), id);

    if (wave == nullptr)
        return;

    auto tree = pitchTreeOf (*wave);

    if (! tree.isValid() || (int) tree.getProperty (transitionsProp, 0) == (int) t)
        return;

    owner.beginUndoStep ("Note Transitions");
    tree.setProperty (transitionsProp, (int) t, &wave->edit.getUndoManager());
}

bool PitchEditor::resetPitch (RegionId id)
{
    auto* wave = waveOf (owner.getEdit(), id);

    if (wave == nullptr)
        return false;

    auto tree = pitchTreeOf (*wave);

    if (! hasAnyCorrection (tree))
        return false;

    owner.beginUndoStep ("Reset Pitch");
    auto* um = &wave->edit.getUndoManager();

    for (int i = tree.getNumChildren(); --i >= 0;)
        if (tree.getChild (i).hasType (noteType))
            tree.removeChild (i, um);

    // Back to the recording straight away (no need to wait for the timer).
    keepInStep (id);
    return true;
}

//==============================================================================
// Keeping each region playing what its corrections say
void PitchEditor::timerCallback()
{
    auto* edit = owner.getEdit();

    if (edit == nullptr)
        return;

    for (auto* track : te::getAudioTracks (*edit))
        for (auto* clip : track->getClips())
            if (clip->state.hasProperty (originalProp) || pitchTreeOf (*clip).isValid())
                keepInStep (clip->itemID.getRawID());

    // Finished renders: remember their curves, then let them go.
    for (auto it = renders.begin(); it != renders.end();)
    {
        if (it->second->finished)
        {
            if (it->second->ok)
                renderedCurves[it->first] = it->second->request.curve;
            else if (! it->second->cancel)
                failedRenders.insert (it->first);   // not tried again and again

            it = renders.erase (it);
        }
        else
        {
            ++it;
        }
    }
}

bool PitchEditor::keepInStep (RegionId id)
{
    auto* wave = waveOf (owner.getEdit(), id);

    if (wave == nullptr || ! whyNoPitch (*wave).isEmpty())
        return false;

    const auto original = originalOf (*wave);
    const auto playing = wave->getSourceFileReference().getFile();
    const auto workFolder = owner.getWorkingCopyFolder();
    const auto audioFolder = workFolder.getChildFile (SessionBundle::audioFolder);

    // Points the region at a file (the recording or a render). Housekeeping,
    // not an edit: no undo manager. The path is written the way saving
    // writes them (relative to the session, with "/").
    auto play = [&] (const juce::File& file)
    {
        if (file == playing)
            return;

        if (! wave->state.hasProperty (originalProp))
            wave->state.setProperty (originalProp, wave->state[te::IDs::source], nullptr);

        const auto path = file.isAChildOf (workFolder) ? file.getRelativePathFrom (workFolder).replaceCharacter ('\\', '/')
                                                        : file.getFullPathName();
        wave->state.setProperty (te::IDs::source, path, nullptr);
    };

    const auto tree = pitchTreeOf (*wave);

    // Back to the recording: no render is needed any more.
    auto playOriginal = [&]
    {
        cancelRendersFor (id);
        settling.erase (id);
        play (original);
        return false;
    };

    if (! hasAnyCorrection (tree))
        return playOriginal();

    auto job = analysisFor (original, true);

    if (job == nullptr || ! job->finished)
        return job != nullptr;

    if (job->result == nullptr || ! job->result->singleVoice)
        return false;

    const auto& a = *job->result;
    const auto [root, minor] = songKeyOf (wave->edit);
    const auto transitions = (NoteTransitions) juce::jlimit (0, 2, (int) tree.getProperty (transitionsProp, 0));
    const auto curve = pitchcurve::offsets (a, correctionsFor (a, tree), TunePlugin::scaleNotes (root, minor),
                                            transposeOf (*wave), transitions);

    if (std::all_of (curve.begin(), curve.end(), [] (float v) { return v == 0.0f; }))
        return playOriginal();

    // The render's name says which recording and which curve it's for, so
    // an existing one with the name is the right one.
    std::vector<juce::int16> quantised;

    for (auto v : curve)
        quantised.push_back ((juce::int16) juce::jlimit (-32000, 32000, juce::roundToInt (v * 1000.0f)));

    const auto hash = hashOf (quantised.data(), quantised.size() * sizeof (juce::int16), job->fingerprint);
    const auto rendered = audioFolder.getChildFile (original.getFileNameWithoutExtension() + " pitch "
                                                    + juce::String::toHexString ((juce::int64) hash) + ".wav");
    const auto key = rendered.getFullPathName();

    // Whatever else was rendering for this region isn't wanted now.
    cancelRendersFor (id, key);

    if (rendered.existsAsFile() && renders.count (key) == 0)
    {
        settling.erase (id);
        play (rendered);
        return false;
    }

    if (renders.count (key) > 0)
        return true;   // still rendering

    if (failedRenders.count (key) > 0)
        return false;  // it couldn't be made (the disk is full, say): the region plays what it did

    // Wait for the corrections to settle (a slider being dragged changes
    // them many times a second), so only the one that stays is rendered.
    const auto now = juce::Time::getMillisecondCounter();

    if (auto& s = settling[id]; s.first != hash)
    {
        s = { hash, now };
        return true;
    }
    else if (now - s.second < settleMs)
    {
        return true;
    }

    settling.erase (id);

    // A new render. Only what changed since the render playing now is
    // rendered again (if its curve is known); the rest is copied from it.
    PitchRenderRequest request;
    request.original = original;
    request.output = rendered;
    request.analysis = a;
    request.curve = curve;

    if (auto known = renderedCurves.find (playing.getFullPathName()); known != renderedCurves.end() && playing != original)
    {
        request.previous = playing;
        request.previousCurve = known->second;
    }

    auto& formats = owner.getTeEngine().getAudioFileFormatManager().readFormatManager;
    request.openReader = [&formats] (const juce::File& f) { return std::unique_ptr<juce::AudioFormatReader> (formats.createReaderFor (f)); };

    audioFolder.createDirectory();
    auto render = std::make_shared<RenderJob> (std::move (request));
    render->region = id;
    renders[key] = render;
    pool->addJob (render.get(), false);
    return true;
}

void PitchEditor::cancelRendersFor (RegionId id, const juce::String& except)
{
    for (auto it = renders.begin(); it != renders.end();)
    {
        auto& job = it->second;

        if (job->region != id || it->first == except || job->finished)
        {
            ++it;
            continue;
        }

        // A render still waiting its turn is taken out of the queue straight
        // away; one that's running stops at its next block, and the timer
        // lets it go once it has (the pool may still be using it till then).
        job->cancel = true;

        if (pool->removeJob (job.get(), false, 0))
            it = renders.erase (it);
        else
            ++it;
    }
}

bool PitchEditor::finishRendering (int timeoutMs)
{
    const auto until = juce::Time::getMillisecondCounter() + (juce::uint32) timeoutMs;

    for (;;)
    {
        // The same pass the timer makes, then: is anything still going?
        timerCallback();
        bool waiting = false;

        if (auto* edit = owner.getEdit())
            for (auto* track : te::getAudioTracks (*edit))
                for (auto* clip : track->getClips())
                    if (clip->state.hasProperty (originalProp) || pitchTreeOf (*clip).isValid())
                        waiting = keepInStep (clip->itemID.getRawID()) || waiting;

        if (! waiting)
            return true;

        if (juce::Time::getMillisecondCounter() > until)
            return false;

        juce::Thread::sleep (20);
    }
}

} // namespace ikaros
