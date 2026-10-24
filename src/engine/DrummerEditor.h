#pragma once

#include "engine/DrummerEngine.h"
#include "engine/EngineTypes.h"
#include <juce_data_structures/juce_data_structures.h>
#include <optional>

// Forward declarations only, so the UI can include this header without
// seeing Tracktion (as in AudioEngine.h).
namespace tracktion { inline namespace engine { class Clip; class MidiClip; } }

namespace ikaros
{

class AudioEngine;
class EffectsController;
class SectionEditor;

//==============================================================================
/**
    Drummer tracks and regions: a virtual drummer that plays a part in a
    chosen style, shaped by a few settings, like GarageBand's Drummer.

    A Drummer region is an ordinary MIDI region (a Tracktion MidiClip) that
    also holds its settings (an IKAROSDRUMMER tree inside the clip's state).
    Its notes are made from those settings by DrummerEngine.h and stored in
    the region like any other notes, so:
      - playing, saving, undo, copying and splitting all work the way they
        do for MIDI regions;
      - opening a session never makes the notes again: a song sounds the
        same even after the Drummer's rules improve.

    The notes are made again when the settings change, or when what they
    were made for changes: the region is moved, resized, split or copied,
    or the section markers or time signatures around it change. The region
    remembers what its notes were made for (the settings, its bars and the
    markers, as a "key"); after every edit, refresh() remakes the notes of
    regions whose key no longer matches, as part of the same undo step.
    Tempo changes don't remake anything: the notes are in beats, so they
    just follow.

    A Drummer track is an instrument track (kind "drummer") playing a drum
    kit. It plays the kit its drummer comes with (see DrummerInfo::kit);
    until that kit's sound pack is installed, it plays the Ikaros Basics
    Electronic Kit instead (getMissingKitPack says which pack to offer), and
    switches over by itself once it's there. Each kit's pack says which key
    plays which drum piece (instrument.json's drumMap), so the notes follow
    the kit: every Drummer note remembers its piece.

    Every method runs on the message thread. Mutating methods record into
    the session's UndoManager, one undo step per user action.
*/
class DrummerEditor
{
public:
    /** It sets kits through EffectsController and reads the song's
        sections from SectionEditor (both TrackController's, which owns this). */
    DrummerEditor (AudioEngine&, EffectsController&, const SectionEditor&);
    ~DrummerEditor();

    /** The drummers to choose from, in the editor's order. */
    std::vector<DrummerInfo> getDrummers() const;

    /** The kit Drummer tracks play until theirs is installed. */
    static const juce::String fallbackKit;

    //==============================================================================
    // Tracks

    /** Makes a track a Drummer track playing a drummer's kit (or the
        fallback until it's installed), and gives it one Drummer region:
        over the song's sections, or eight bars from the start if there
        are none. Part of the current undo step (TrackController::addDrummerTrack
        starts it). Returns the region. */
    RegionId setUpDrummerTrack (TrackId, const DrummerSettings& = {});

    bool isDrummerTrack (TrackId) const;

    /** The sound pack a Drummer track's kit comes from, while it isn't
        installed (the track plays the fallback kit meanwhile); else empty.
        packName gets the kit's name for a download button. */
    juce::String getMissingKitPack (TrackId, juce::String* kitName = nullptr) const;

    /** Puts every Drummer track on the kit it should play: its own once its
        pack is installed, the fallback while it isn't; the notes move to the
        new kit's keys (see EffectsController::setSound). The engine calls it
        after opening a session (asUndoStep false: nothing goes in the undo
        history) and when packs are installed or removed (asUndoStep true:
        each switch is an undo step, "Use Red Zeppelin Kit", so Undo goes
        back to the kit it played before). */
    void updateKits (bool asUndoStep);

    //==============================================================================
    // Regions

    bool isDrummerRegion (RegionId) const;

    /** A new Drummer region on a Drummer track, from startBeat to endBeat,
        its notes made straight away. Part of the current undo step.
        Returns 0 if the track isn't a Drummer track. */
    RegionId addDrummerRegion (TrackId, double startBeat, double endBeat, const DrummerSettings& = {});

    /** "Add Drummer Region": eight bars from the start of the bar `beat` is
        in (shorter if another region starts sooner), with the settings of
        the track's nearest Drummer region before it. One undo step. Returns
        0 if the track isn't a Drummer track or there's less than a bar free. */
    RegionId addDrummerRegionAt (TrackId, double beat);

    std::optional<DrummerSettings> getSettings (RegionId) const;

    /** New settings, the notes made again, as one undo step called
        `undoName` (e.g. "Drummer Fills"). Does nothing if they're the same. */
    void setSettings (RegionId, const DrummerSettings&, const juce::String& undoName);

    /** While the XY pad is dragged: plays the settings straight away
        (changing the region's notes as the song plays), without undo
        steps. finishPreview then makes the whole drag one undo step;
        cancelPreview puts things back. */
    void previewSettings (RegionId, const DrummerSettings&);
    void finishPreview (const juce::String& undoName);
    void cancelPreview();

    /** "New Variation": the same settings with a new seed (one undo step). */
    void newVariation (RegionId);

    /** "Convert to MIDI Region": the region keeps its notes and becomes an
        ordinary MIDI region (one undo step), so they can be edited in the
        piano roll. They're never made again after that. */
    void convertToMidiRegion (RegionId);

    /** The bars of the region the Drummer leaves silent (not in 4/4), as
        the song's bar numbers. Empty when it plays them all. */
    std::vector<int> getSilentBars (RegionId) const;

    /** What the region's settings play where it is now, made afresh (with
        which bars have fills and crashes). Its notes are these, as its
        kit's keys, unless they were made before an edit that doesn't
        remake them (a tempo change, say). For the tests. */
    DrumPart makePart (RegionId) const;

    /** The key each drum piece is on, on a track's kit. */
    std::array<int, numDrumPieces> getDrumKeys (TrackId) const;

    //==============================================================================
    /** @internal After an edit (AudioEngine calls it before telling the UI
        about changes): remakes the notes of Drummer regions whose bars,
        markers or settings changed, in the same undo step. */
    void refresh (const ModelChanges&);

    /** Remakes any Drummer region whose key is out of date, now. */
    void refreshAll();

    /** @internal The session was replaced. */
    void sessionChanged();

private:
    AudioEngine& owner;
    EffectsController& effects;
    const SectionEditor& sections;

    // The XY pad drag in progress: the region, and how it was before.
    struct Preview
    {
        RegionId region = 0;
        juce::ValueTree settings, sequence;   // copies of the region's settings and notes
    };

    std::optional<Preview> preview;
    bool refreshing = false;    // notes are being written: their changes don't need another look

    /** Makes the clip's notes again from its settings and the song around
        it, into um's current undo step (or none). */
    void regenerate (tracktion::MidiClip&, juce::UndoManager*);

    /** Remakes the notes of those clips that are Drummer regions with an
        out-of-date key. */
    void refreshClips (const std::vector<tracktion::Clip*>&);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DrummerEditor)
};

} // namespace ikaros
