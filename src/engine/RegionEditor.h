#pragma once

#include "engine/EngineTypes.h"
#include "engine/WaveformCache.h"
#include <juce_data_structures/juce_data_structures.h>
#include <functional>
#include <optional>

namespace ikaros
{

class AudioEngine;

//==============================================================================
/**
    Regions (Tracktion clips): reading them for the UI, creating, moving,
    trimming, splitting, duplicating, the region clipboard, and importing
    audio files.

    Every method runs on the message thread. Mutating methods record into the
    session's UndoManager.
*/
class RegionEditor
{
public:
    /** Importing audio onto an instrument track (or none) needs a new audio
        track, but creating tracks is TrackController's job (naming, colour,
        default input). It passes in this function, which adds an audio track
        as part of the current undo step and returns its ID, so RegionEditor
        doesn't depend on TrackController. */
    using CreateAudioTrackFn = std::function<TrackId (const juce::String& name)>;

    RegionEditor (AudioEngine&, CreateAudioTrackFn);

    //==============================================================================
    // Regions (Tracktion clips)
    std::vector<RegionInfo> getRegions (TrackId) const;
    std::optional<RegionInfo> getRegion (RegionId) const;

    /** Creates a 4-bar MIDI region with a simple chord progression, starting at
        the bar that contains startBeat. Returns 0 if the track has no room. */
    RegionId createChordRegion (TrackId, double startBeat);
    void deleteRegion (RegionId);
    void deleteRegions (const std::vector<RegionId>&);

    // Region editing. A region that is moved, trimmed, pasted or duplicated
    // onto another one wins: the part of the other region it covers is cut away.

    /** Moves regions in one undo step. MIDI regions only go on instrument
        tracks and audio regions only on audio tracks; if any placement breaks
        that rule, nothing moves. */
    void moveRegions (const std::vector<RegionPlacement>&);

    /** The shortest a region can be trimmed to, in beats. */
    static constexpr double minRegionBeats = 0.25;

    /** Moves a region's edges. Clamped to RegionInfo::minStartBeat/maxEndBeat. */
    void trimRegion (RegionId, double newStartBeat, double newEndBeat);

    /** Repeats a region's content up to newEndBeat, as one "Loop Region"
        undo step. The first time, whatever the region shows now becomes the
        part that repeats. A looped region stays looped when it's made shorter
        again. Returns false if nothing changed. */
    bool loopRegion (RegionId, double newEndBeat);

    /** Splits each region that crosses `beat` in two. Returns the new right-hand halves. */
    std::vector<RegionId> splitRegions (const std::vector<RegionId>&, double beat);

    /** Copies the regions to just after the last one. Returns the copies. */
    std::vector<RegionId> duplicateRegions (const std::vector<RegionId>&);

    void renameRegion (RegionId, const juce::String& newName);

    /** Sets how long an audio region fades in (fadeIn) or out, in beats, as
        one "Fade In" or "Fade Out" undo step. Never shorter than the
        automatic edge fade, and never running into the other fade. */
    void setFade (RegionId, bool fadeIn, double lengthBeats);

    /** Region clipboard. Pasting puts the earliest copied region at `beat` on
        `track`, keeping the regions' spacing and track layout. A region whose
        target track is the wrong kind goes back to the track it came from.
        Returns the pasted regions. */
    void copyRegions (const std::vector<RegionId>&);
    bool canPaste() const;
    std::vector<RegionId> pasteRegions (TrackId track, double beat);

    //==============================================================================
    // Audio files

    /** True for files Tracktion can read (WAV, AIFF, FLAC, Ogg...). */
    bool isSupportedAudioFile (const juce::File&) const;

    /** Adds audio files as regions, one after another from `beat`, in one
        undo step. Pass 0 (or an instrument track) to put them on a new audio
        track named after the first file. Returns the new regions. */
    std::vector<RegionId> importAudioFiles (TrackId, const juce::StringArray& paths, double beat);

    /** @internal Called (via TrackController) after the Edit has been replaced. */
    void sessionChanged();

private:
    AudioEngine& owner;
    CreateAudioTrackFn createAudioTrack;

    // `mutable`: reading regions (a const operation) fills the cache.
    mutable WaveformCache waveforms;

    // The region clipboard (copyRegions / pasteRegions).
    struct CopiedRegion
    {
        juce::ValueTree state;     // a copy of the clip as it was when copied
        TrackId sourceTrack = 0;
        int trackOffset = 0;       // how many tracks below the topmost copied region
        double beatOffset = 0.0;   // how many beats after the earliest copied region
        bool isAudio = false;
    };

    std::vector<CopiedRegion> clipboard;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RegionEditor)
};

} // namespace ikaros
