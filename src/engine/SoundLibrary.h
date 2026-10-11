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
      - sampled instruments from sound packs ("sampler:<pack>/<instrument>")
      - third-party plug-in instruments, listed by PluginLibrary ("plugin:<id>")

    A sound pack is a folder (so new instrument sets can be added by copying a
    folder in, like GarageBand's downloadable sound packs):

        <Pack name>/
            Instruments/<Instrument name>/instrument.json   + its samples
            Loops/loops.json                                 + the loop files

    instrument.json, either with simple key zones (played by Tracktion's
    sampler):
        { "category": "Keyboards", "oneShot": false,
          "samples": [ { "file": "C3.wav", "root": 48, "low": 0, "high": 53, "gain": 0 } ] }
    or naming an SFZ file in the same folder (played by sfizz, see SfzPlugin),
    for velocity layers, round-robins and big sample sets:
        { "category": "Keyboards", "sfz": "Grand Piano.sfz" }

    loops.json:
        [ { "file": "Beat.wav", "name": "Electro Beat", "bpm": 120, "key": "C" } ]

    Packs are read from the folder installed with the app and from the user's
    Documents/Ikaros Studio/Sounds folder.

    This class only reads files; it doesn't touch Tracktion.
*/
class SoundLibrary
{
public:
    SoundLibrary();

    /** Re-reads the sound packs (e.g. after the user added one). */
    void rescan();

    /** Also reads the packs in this folder (each subfolder is a pack), and
        rescans. For the smoke test's test pack. */
    void addPackFolder (const juce::File& folderOfPacks);

    /** Every sound from the built-in patches and the sound packs, grouped
        by category in library order. (Plug-in instruments come from PluginLibrary.) */
    const std::vector<SoundInfo>& getSounds() const noexcept    { return sounds; }
    std::optional<SoundInfo> findSound (const juce::String& id) const;

    /** Loops from the sound packs. */
    const std::vector<LibraryFile>& getLoops() const noexcept   { return loops; }

    /** The tempo a loop was made at, if it's one of ours (0 if unknown). */
    double getLoopBpm (const juce::File&) const;

    /** The sound new instrument tracks start with. */
    static const juce::String defaultSoundId;

    /** Where users can put their own sound packs: Documents/Ikaros Studio/Sounds. */
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

    struct SampledInstrument
    {
        SoundInfo info;
        juce::File sfzFile;            // set for an SFZ instrument (then zones is empty)
        std::vector<SampleZone> zones;
        bool oneShot = false;          // samples play to the end (drums) instead of stopping at key-up
    };

    const SampledInstrument* findSampledInstrument (const juce::String& id) const;

private:
    std::vector<SoundInfo> sounds;
    std::vector<SampledInstrument> sampled;
    std::vector<LibraryFile> loops;
    std::vector<juce::File> extraPackFolders;

    void readPack (const juce::File& packFolder);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SoundLibrary)
};

} // namespace ikaros
