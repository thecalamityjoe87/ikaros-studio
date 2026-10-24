#pragma once

#include "engine/EngineTypes.h"
#include "engine/PitchCurve.h"
#include <map>
#include <set>
#include <memory>

namespace ikaros
{

class AudioEngine;
class FlexEditor;

//==============================================================================
/** One sung note, as the audio editor's Pitch mode shows it. Pitches are
    fractional MIDI notes as heard (the region's own transposition included). */
struct PitchNoteInfo
{
    double sourceStart = 0.0, sourceEnd = 0.0;   // seconds into the recording; sourceStart names the note
    double startBeat = 0.0, endBeat = 0.0;       // where it plays (the first time round, for a looped region)
    float sung = 0.0f;                           // its centre, as sung
    float corrected = 0.0f;                      // its centre after the corrections
    float vibratoCents = 0.0f;
    NoteCorrection correction;

    // The measured pitch, frame by frame: beats on the timeline and pitches as sung.
    std::vector<std::pair<double, float>> curve;
};

/** A region's pitch state, for the audio editor and the Region inspector. */
struct PitchInfo
{
    bool available = false;            // the region can use Pitch mode (see unavailableReason)
    juce::String unavailableReason;    // why not, for a tooltip
    bool on = false;                   // Pitch mode is on for the region

    bool analysing = false;            // the recording is being analysed (in the background)
    float analysisProgress = 0.0f;     // ...0..1
    bool analysed = false;             // the analysis is ready (notes below)
    bool singleVoice = true;           // false: it doesn't look like one voice or instrument
    bool fromCache = false;            // the analysis was read from the session's cache, not done again

    bool rendering = false;            // the corrected audio is being made (the original plays meanwhile)
    float renderProgress = 0.0f;
    bool hasCorrections = false;       // something to Reset

    int keyRoot = 0;                   // the song's key
    bool keyMinor = false;
    int keyNotes = 0xfff;              // its scale, bit 0 = C
    int transpose = 0;                 // the region's own transposition (whole semitones)
    NoteTransitions transitions = NoteTransitions::natural;

    std::vector<PitchNoteInfo> notes;  // in time order
};

//==============================================================================
/**
    Pitch editing for audio regions: the audio editor's Pitch mode, where
    each sung note can be seen and corrected.

    A region's recording is analysed once, in the background, the first time
    it's shown in Pitch mode (PitchAnalysis.h). The analysis is cached in a
    small file beside the recording in the session's Audio folder, so it's
    never done again unless the recording itself changes.

    Corrections are stored per note on the region (an IKAROSPITCH child of
    the clip, so they're saved, split, copied and undone with it):
    a shift in semitones, a Snap to Key amount and a Vibrato amount, plus the
    region's Note transitions. Every edit is one undo step.

    From the corrections the engine makes a pitch curve (PitchCurve.h) and
    renders the recording along it with Rubber Band (PitchRender.h), in the
    background, into a new file in the session's Audio folder. Once that's
    ready the region plays it in place of the recording; until then it plays
    the recording. The render file is named after the curve, so going back
    to corrections made before (Undo) finds their render again straight
    away, and only the parts of the recording whose correction changed are
    rendered again. A render starts once the corrections have stayed the
    same for half a second (so a slider drag renders once, at the end), and
    a render the region no longer needs is stopped. The recording is never changed: Reset, and undoing every
    correction, point the region back at it exactly. Switching the region's
    file is housekeeping, not an edit: it isn't an undo step.

    Flex: the rendered file is the same length as the recording and lines up
    with it sample for sample, so a region's Flex markers (in seconds of the
    recording) apply to it unchanged. Pitch edits render from the original
    recording, and Flex then stretches the rendered file.

    Pitch mode is for recordings and audio files of a single voice or
    instrument. Loops that follow the song's tempo or key, reversed regions
    and regions with takes can't use it (PitchInfo::unavailableReason says why).

    Every method runs on the message thread.
*/
class PitchEditor  : private juce::Timer
{
public:
    PitchEditor (AudioEngine&, FlexEditor&);
    ~PitchEditor() override;

    /** The region's pitch state. Starts the analysis if Pitch mode is on and
        it hasn't been done. Polled by the editor while it's busy. */
    PitchInfo getPitch (RegionId) const;

    /** Pitch mode on or off for a region, as one "Pitch On"/"Pitch Off" undo step. */
    void setPitchMode (RegionId, bool on);
    bool isPitchModeOn (RegionId) const;

    // Edits. Notes are named by their PitchNoteInfo::sourceStart; an empty
    // list means every note. Each is one undo step, and returns false if
    // nothing changed.

    /** Moves notes up or down by `semitones` ("Move Note"/"Move Notes"). */
    bool moveNotes (RegionId, const std::vector<double>& notes, float semitones);

    /** Puts each note's centre on the nearest semitone, or the nearest key
        note for a note with Snap to Key on ("Center Pitch"). */
    bool centreNotes (RegionId, const std::vector<double>& notes);

    /** Snap to Key (0..1) and Vibrato (0 keep .. 1 flatten) for notes. These
        don't start an undo step: the inspector starts one when a slider
        drag begins, so a whole drag is one step. */
    bool setSnap (RegionId, const std::vector<double>& notes, float amount);
    bool setFlatten (RegionId, const std::vector<double>& notes, float amount);

    /** Snap to Key and Vibrato for every note, as one "Correct All" step. */
    bool correctAll (RegionId, float snap, float flatten);

    /** How quickly the pitch glides between corrected notes ("Note Transitions"). */
    void setTransitions (RegionId, NoteTransitions);

    /** Takes every correction away ("Reset Pitch"): the region plays its recording again. */
    bool resetPitch (RegionId);

    /** Blocks until every render the session needs is done and in place
        (export and the tests use it), or the timeout passes. Returns false
        on a timeout. */
    bool finishRendering (int timeoutMs = 60000);

    /** True while a render or an analysis is running. */
    bool isBusy() const;

    /** @internal Called (via TrackController) before the Edit is replaced. */
    void sessionChanged();

    /** @internal For the save: the clip trees that hold a recording's path
        in their "original" property, and the analysis cache file of each. */
    static const juce::Identifier pitchStateType, originalProp;
    static juce::File cacheFileFor (const juce::File& audioFolder, const juce::File& recording);

private:
    struct AnalysisJob;
    struct RenderJob;

    AudioEngine& owner;
    FlexEditor& flex;

    // unique_ptr/shared_ptr: the jobs are only declared here. `mutable`:
    // getPitch() (const) starts the analysis on first use.
    std::unique_ptr<juce::ThreadPool> pool;
    mutable std::map<juce::String, std::shared_ptr<AnalysisJob>> analyses;   // by recording path
    std::map<juce::String, std::shared_ptr<RenderJob>> renders;              // by output path
    std::map<juce::String, std::vector<float>> renderedCurves;               // each render's curve (for partial re-renders)
    std::set<juce::String> failedRenders;

    // Each region's latest curve (by its hash) and when it last changed: a
    // render starts once it has stayed the same for settleMs.
    std::map<RegionId, std::pair<juce::uint64, juce::uint32>> settling;
    static constexpr juce::uint32 settleMs = 500;

    /** Stops the region's renders, except the one making `except` (a render's path). */
    void cancelRendersFor (RegionId, const juce::String& except = {});

    std::shared_ptr<AnalysisJob> analysisFor (const juce::File& recording, bool start) const;
    void timerCallback() override;
    bool keepInStep (RegionId);   // true while it's waiting on a job

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PitchEditor)
};

} // namespace ikaros
