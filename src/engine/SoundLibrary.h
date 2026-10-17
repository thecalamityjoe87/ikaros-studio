#pragma once

#include "engine/EngineTypes.h"
#include <optional>
#include <utility>

namespace ikaros
{

//==============================================================================
/**
    The sounds an instrument track can use, and the loops in the library.

    Three kinds of sound:
      - built-in synth patches: settings for Tracktion's 4OSC synth, defined
        in SoundLibrary.cpp ("synth:<name>")
      - sampled instruments from sound packs ("sampler:<pack id>/<instrument folder>")
      - third-party plug-in instruments, listed by PluginLibrary ("plugin:<id>")

    A sound pack is a folder (so new instrument sets can be added by copying a
    folder in, like GarageBand's downloadable sound packs):

        <Pack name>/
            pack.json                                        about the pack
            Instruments/<Instrument name>/instrument.json   + its samples
            Loops/loops.json                                 + the loop files

    pack.json (format 2; every field but "id" is optional):
        { "format": 2, "id": "ikaros-studio.basics", "name": "Ikaros Basics",
          "version": "1.0.0", "description": "...", "author": "...",
          "license": "CC0-1.0", "licenseFile": "LICENSE.txt", "installedSize": 2951094,
          "credits": [ { "what": "...", "who": "...", "license": "CC0-1.0",
                         "url": "...", "attributionRequired": false } ] }

      "id" is lowercase letters, digits, "." and "-", and never changes:
      sessions and downloads refer to the pack by it. Older packs have no
      pack.json, or one without an id (format 1); they get the id
      "local.<folder name>" (made lowercase, spaces as "-") and version 0.0.0.

    instrument.json, either with simple key zones (played by Tracktion's
    sampler):
        { "name": "Soft Piano", "category": "Keyboards", "oneShot": false,
          "samples": [ { "file": "C3.wav", "root": 48, "low": 0, "high": 53, "gain": 0 } ] }
    or naming an SFZ file (played by sfizz, see SfzPlugin), for velocity
    layers, round-robins and big sample sets. The path is relative to the
    instrument's folder ("../" is allowed, so instruments can share samples):
        { "name": "Grand Piano", "category": "Keyboards", "sfz": "Programs/piano.sfz" }

      Optional: "icon" (a track icon key; otherwise the category's), and
      "knobs": the Sound tab's five knobs, exactly five of
        { "name": "Tone", "control": "tone", "default": 0.5 }
      where control is "tone", "room", "drive", "width", "tune", or (SFZ
      instruments) "cc" with "cc": <controller the SFZ maps, 0-127>.
      Without valid knobs, the category's defaults are used (see
      getDefaultKnobs). What each control does: see SoundShaperPlugin
      ("tune" moves notes by up to 12 semitones either way).

      An instrument is identified by its pack's id and its folder name, so
      folder names stay the same from one pack version to the next.

    loops.json:
        [ { "file": "Bass.wav", "name": "Pluck Bass", "bpm": 120, "key": "C minor",
            "tags": [ "bass", "electronic" ] },
          { "file": "Beat.wav", "name": "Electro Beat", "bpm": 120, "unpitched": true,
            "tags": [ "drums", "electronic" ] } ]

      "tags" is optional: lowercase words the loop browser's instrument,
      genre and mood filters look for (as well as the name).

      "key" is optional: the key the loop was made in, written "C", "C major",
      "F# minor", "Bbm" and so on (see parseKey). A loop with a key is moved
      to the session's key when it's placed, and follows it when it changes.
      "unpitched": true marks loops that never change key (drums). Without
      it, loops tagged as drums or percussion count as unpitched too.

    Packs are read from the folder installed with the app and from the user's
    Documents/Ikaros Studio/Sounds folder.

    This class only reads files; it doesn't touch Tracktion.
*/
class SoundLibrary
{
public:
    /** `userFolder`: where the user's own and downloaded packs are (see
        getUserSoundsFolder for the default). */
    explicit SoundLibrary (const juce::File& userFolder = getUserSoundsFolder());

    /** Where the user's own and downloaded packs are looked for. Call
        rescan() after changing it. */
    void setUserFolder (const juce::File& folder)       { userFolder = folder; }
    juce::File getUserFolder() const                    { return userFolder; }

    /** Re-reads the sound packs (e.g. after the user added one). */
    void rescan();

    /** Also reads the packs in this folder (each subfolder is a pack), and
        rescans. For the smoke test's test pack. */
    void addPackFolder (const juce::File& folderOfPacks);

    /** Every sound from the built-in patches and the sound packs, grouped
        by category in library order. (Plug-in instruments come from PluginLibrary.) */
    const std::vector<SoundInfo>& getSounds() const noexcept    { return sounds; }
    std::optional<SoundInfo> findSound (const juce::String& id) const;

    /** The installed sound packs: the ones that come with the app first. */
    const std::vector<SoundPackInfo>& getPacks() const noexcept { return packs; }
    std::optional<SoundPackInfo> findPack (const juce::String& id) const;

    /** Compares two "major.minor.patch" versions: <0 if a is older, 0 if
        they're the same, >0 if a is newer. Missing parts count as 0. */
    static int compareVersions (const juce::String& a, const juce::String& b);

    /** True for a valid pack id (lowercase letters, digits, "." and "-"). */
    static bool isValidPackId (const juce::String&);

    /** Loops from the sound packs. */
    const std::vector<LibraryFile>& getLoops() const noexcept   { return loops; }

    /** The library's loop for this file, or nullptr if it isn't one of ours.
        The pointer is only good until the next rescan. */
    const LibraryFile* findLoop (const juce::File&) const;

    /** A musical key, e.g. "C", "C major", "F# minor", "Bbm" or "a minor".
        Returns false (and leaves root and minor alone) if it isn't one. */
    static bool parseKey (const juce::String& text, int& root, bool& minor);

    /** True if a loop with these tags is drums or percussion, which have no key. */
    static bool soundsUnpitched (const juce::StringArray& tags);

    /** The sound new instrument tracks start with. */
    static const juce::String defaultSoundId;

    /** The default place for users' own sound packs, and where downloaded
        ones go: Documents/Ikaros Studio/Sounds (or IKAROS_USER_SOUNDS_DIR, if
        set). The Sounds & Plug-ins preference can choose another. */
    static juce::File getUserSoundsFolder();

    /** The packs installed with the app (may not exist in odd setups). */
    static juce::File getBundledSoundsFolder();

    //==============================================================================
    // For the engine (EffectsController), to build the instrument.

    /** A built-in synth patch: 4OSC settings as (state property, value) pairs. */
    struct SynthPatch
    {
        const char* name;
        const char* category;
        std::vector<std::pair<const char*, float>> settings;
    };

    static const SynthPatch* findSynthPatch (const juce::String& id);

    /** A sampled instrument: which sample plays which keys. */
    struct SampleZone
    {
        juce::File file;
        int root = 60, low = 0, high = 127;
        float gainDb = 0.0f;
    };

    /** One of the Sound tab's knobs, from instrument.json (see above). */
    struct KnobMapping
    {
        juce::String name;             // what the user sees, e.g. "Tone"
        juce::String control;          // "tone", "room", "drive", "width", "tune" or "cc"
        int cc = -1;                   // for "cc": the MIDI controller number
        float defaultValue = 0.5f;     // 0..1
    };

    static constexpr int numKnobs = 5;

    struct SampledInstrument
    {
        SoundInfo info;
        std::vector<KnobMapping> knobs;    // exactly numKnobs, or empty for the category's defaults
        juce::File sfzFile;            // set for an SFZ instrument (then zones is empty)
        std::vector<SampleZone> zones;
        bool oneShot = false;          // samples play to the end (drums) instead of stopping at key-up
    };

    const SampledInstrument* findSampledInstrument (const juce::String& id) const;

    /** The knobs an instrument's Sound tab shows: its own from
        instrument.json, or else its category's defaults. */
    static std::vector<KnobMapping> getKnobsFor (const SampledInstrument&);

    /** A category's default knobs ("Drums", "Bass", ...; anything else gets
        a general set): five, or four for drum kits (no Pitch). They only use
        controls every sampled instrument has, never "cc". */
    static std::vector<KnobMapping> getDefaultKnobs (const juce::String& category);

    /** Like findSampledInstrument, but also understands ids saved by older
        versions ("sampler:<pack folder name>/<instrument>"). */
    const SampledInstrument* resolveSampledInstrument (const juce::String& storedId) const;

    /** The pack id in a sampled sound's id ("sampler:<pack id>/<instrument>"),
        or empty for other sounds. */
    static juce::String getPackIdOf (const juce::String& soundId);

private:
    juce::File userFolder;
    std::vector<SoundPackInfo> packs;
    std::vector<SoundInfo> sounds;
    std::vector<SampledInstrument> sampled;
    std::vector<LibraryFile> loops;
    std::vector<juce::File> extraPackFolders;

    void readPack (const juce::File& packFolder, bool bundled);
    static SoundPackInfo readPackInfo (const juce::File& packFolder, bool bundled);
    static std::vector<KnobMapping> readKnobs (const juce::var& json, bool isSfz);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SoundLibrary)
};

} // namespace ikaros
