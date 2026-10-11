#include "engine/SoundLibrary.h"

namespace ikaros
{

namespace
{
    // The library's categories, in the order the sidebar shows them.
    const char* const categoryOrder[] { "Keyboards", "Drums", "Bass", "Synths", "Guitar", "Strings" };

    int categoryIndex (const juce::String& category)
    {
        for (int i = 0; i < juce::numElementsInArray (categoryOrder); ++i)
            if (category == categoryOrder[i])
                return i;

        return juce::numElementsInArray (categoryOrder);
    }

    // 4OSC settings, as the synth stores them. Oscillator settings end in the
    // oscillator's number (1-4). Wave shapes: 1 sine, 2 square, 3 saw,
    // 4 triangle, 5 noise (0 = oscillator off; only osc 1 is on by default).
    // filterType 1 = low-pass; filterFreq is a MIDI note number (69 = 440 Hz).
    // Levels are in dB, times in seconds, sustain in percent, delay in beats.
    const std::vector<SoundLibrary::SynthPatch>& synthPatches()
    {
        static const std::vector<SoundLibrary::SynthPatch> patches {
            // The original starting sound: a sine with a little room.
            { "Basic Synth", "Synths", {
                { "filterType", 1 }, { "filterFreq", 101.25f }, { "ampRelease", 0.3f },
                { "reverbOn", 1 }, { "reverbMix", 0.2f } } },

            { "Warm Pad", "Synths", {
                { "waveShape1", 3 }, { "voices1", 3 }, { "detune1", 0.15f }, { "spread1", 60 },
                { "waveShape2", 3 }, { "tune2", -12 }, { "level2", -8 }, { "voices2", 2 }, { "detune2", 0.1f },
                { "ampAttack", 0.8f }, { "ampDecay", 1.0f }, { "ampSustain", 85 }, { "ampRelease", 1.5f },
                { "filterType", 1 }, { "filterFreq", 88 }, { "filterResonance", 10 },
                { "chorusOn", 1 }, { "chorusMix", 0.3f },
                { "reverbOn", 1 }, { "reverbSize", 0.8f }, { "reverbMix", 0.35f },
                { "masterLevel", -6 } } },

            { "Bright Lead", "Synths", {
                { "waveShape1", 3 },
                { "waveShape2", 2 }, { "fineTune2", 7 }, { "level2", -6 },
                { "voiceMode", 1 }, { "legato", 40 },
                { "ampAttack", 0.005f }, { "ampDecay", 0.3f }, { "ampSustain", 80 }, { "ampRelease", 0.15f },
                { "filterType", 1 }, { "filterFreq", 110 }, { "filterResonance", 25 },
                { "filterAmount", 0.3f }, { "filterDecay", 0.3f }, { "filterSustain", 40 },
                { "delayOn", 1 }, { "delay", 0.75f }, { "delayMix", 0.2f }, { "delayFeedback", -12 },
                { "reverbOn", 1 }, { "reverbMix", 0.15f },
                { "masterLevel", -6 } } },

            { "Glass Pluck", "Synths", {
                { "waveShape1", 4 },
                { "waveShape2", 1 }, { "tune2", 12 }, { "level2", -6 },
                { "waveShape3", 1 }, { "tune3", 19 }, { "level3", -14 },
                { "ampAttack", 0.001f }, { "ampDecay", 0.6f }, { "ampSustain", 0 }, { "ampRelease", 0.4f },
                { "filterType", 1 }, { "filterFreq", 100 },
                { "filterAmount", 0.4f }, { "filterDecay", 0.25f }, { "filterSustain", 0 },
                { "delayOn", 1 }, { "delay", 0.5f }, { "delayMix", 0.15f },
                { "reverbOn", 1 }, { "reverbSize", 0.6f }, { "reverbMix", 0.25f } } },

            { "Vintage Organ", "Keyboards", {
                { "waveShape1", 1 }, { "level1", -3 },
                { "waveShape2", 1 }, { "tune2", 12 }, { "level2", -6 },
                { "waveShape3", 1 }, { "tune3", 19 }, { "level3", -12 },
                { "waveShape4", 1 }, { "tune4", 24 }, { "level4", -14 },
                { "ampAttack", 0.005f }, { "ampDecay", 0.1f }, { "ampSustain", 100 }, { "ampRelease", 0.06f },
                { "chorusOn", 1 }, { "chorusSpeed", 6 }, { "chorusDepth", 3 }, { "chorusMix", 0.35f },
                { "distortionOn", 1 }, { "distortion", 0.1f },
                { "reverbOn", 1 }, { "reverbMix", 0.15f },
                { "masterLevel", -6 } } },

            { "Soft Keys", "Keyboards", {
                { "waveShape1", 4 },
                { "waveShape2", 1 }, { "tune2", 12 }, { "level2", -12 },
                { "ampAttack", 0.003f }, { "ampDecay", 1.6f }, { "ampSustain", 20 }, { "ampRelease", 0.5f },
                { "filterType", 1 }, { "filterFreq", 95 },
                { "filterAmount", 0.25f }, { "filterDecay", 0.8f }, { "filterSustain", 10 },
                { "chorusOn", 1 }, { "chorusMix", 0.2f },
                { "reverbOn", 1 }, { "reverbMix", 0.25f } } },

            { "Synth Bass", "Bass", {
                { "waveShape1", 3 },
                { "waveShape2", 2 }, { "tune2", -12 }, { "level2", -6 },
                { "voiceMode", 1 }, { "legato", 20 },
                { "ampAttack", 0.002f }, { "ampDecay", 0.4f }, { "ampSustain", 70 }, { "ampRelease", 0.08f },
                { "filterType", 1 }, { "filterFreq", 70 }, { "filterResonance", 30 },
                { "filterAmount", 0.5f }, { "filterDecay", 0.25f }, { "filterSustain", 20 },
                { "masterLevel", -4 } } },

            { "Sub Bass", "Bass", {
                { "waveShape1", 1 },
                { "waveShape2", 4 }, { "level2", -14 },
                { "voiceMode", 1 },
                { "ampAttack", 0.004f }, { "ampDecay", 0.2f }, { "ampSustain", 100 }, { "ampRelease", 0.1f } } },

            { "Chamber Strings", "Strings", {
                { "waveShape1", 3 }, { "voices1", 4 }, { "detune1", 0.12f }, { "spread1", 70 },
                { "waveShape2", 3 }, { "tune2", 12 }, { "level2", -12 }, { "voices2", 2 }, { "detune2", 0.1f },
                { "ampAttack", 0.35f }, { "ampDecay", 0.5f }, { "ampSustain", 90 }, { "ampRelease", 0.6f },
                { "filterType", 1 }, { "filterFreq", 92 }, { "filterResonance", 5 }, { "filterKey", 50 },
                { "chorusOn", 1 }, { "chorusMix", 0.25f },
                { "reverbOn", 1 }, { "reverbSize", 0.75f }, { "reverbMix", 0.3f },
                { "masterLevel", -8 } } },
        };

        return patches;
    }

    const juce::String synthPrefix ("synth:"), samplerPrefix ("sampler:");
}

const juce::String SoundLibrary::defaultSoundId ("synth:Basic Synth");

//==============================================================================
SoundLibrary::SoundLibrary()
{
    rescan();
}

juce::File SoundLibrary::getUserSoundsFolder()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
               .getChildFile ("Ikaros Studio").getChildFile ("Sounds");
}

juce::File SoundLibrary::getBundledSoundsFolder()
{
    // Tests (or a developer) can point at another folder.
    auto overridePath = juce::SystemStats::getEnvironmentVariable ("IKAROS_SOUNDS_DIR", {});

    if (overridePath.isNotEmpty())
        return juce::File (overridePath);

    // Where the build and the installers put the packs, relative to the program:
    //   next to it (build folder, Windows)
    //   ../Resources/Sounds (inside a macOS .app)
    //   ../share/ikaros-studio/Sounds (Linux install and Flatpak)
    auto exe = juce::File::getSpecialLocation (juce::File::currentExecutableFile);
    auto dir = exe.getParentDirectory();

    for (auto candidate : { dir.getChildFile ("Sounds"),
                            dir.getSiblingFile ("Resources").getChildFile ("Sounds"),
                            dir.getSiblingFile ("share").getChildFile ("ikaros-studio").getChildFile ("Sounds") })
        if (candidate.isDirectory())
            return candidate;

    return {};
}

void SoundLibrary::addPackFolder (const juce::File& folder)
{
    extraPackFolders.push_back (folder);
    rescan();
}

void SoundLibrary::rescan()
{
    sounds.clear();
    sampled.clear();
    loops.clear();

    for (auto& patch : synthPatches())
        sounds.push_back ({ synthPrefix + patch.name, patch.name, patch.category, "Synth" });

    std::vector<juce::File> roots { getBundledSoundsFolder(), getUserSoundsFolder() };
    roots.insert (roots.end(), extraPackFolders.begin(), extraPackFolders.end());

    for (auto& root : roots)
        if (root.isDirectory())
            for (auto& pack : root.findChildFiles (juce::File::findDirectories, false))
                readPack (pack);

    // Library order: by category, then by name. stable_sort keeps the
    // original order for equal elements.
    std::stable_sort (sounds.begin(), sounds.end(), [] (const SoundInfo& a, const SoundInfo& b)
    {
        const auto ca = categoryIndex (a.category), cb = categoryIndex (b.category);
        return ca != cb ? ca < cb : a.name.compareNatural (b.name) < 0;
    });

    std::stable_sort (loops.begin(), loops.end(), [] (const LibraryFile& a, const LibraryFile& b)
    {
        return a.name.compareNatural (b.name) < 0;
    });
}

void SoundLibrary::readPack (const juce::File& pack)
{
    const auto packName = pack.getFileName();

    // Sampled instruments: one folder each, with an instrument.json.
    for (auto& folder : pack.getChildFile ("Instruments").findChildFiles (juce::File::findDirectories, false))
    {
        auto json = juce::JSON::parse (folder.getChildFile ("instrument.json"));

        if (! json.isObject())
            continue;

        SampledInstrument instrument;
        instrument.info.id = samplerPrefix + packName + "/" + folder.getFileName();
        instrument.info.name = json.getProperty ("name", folder.getFileName()).toString();
        instrument.info.category = json.getProperty ("category", "Keyboards").toString();
        instrument.info.detail = "Sampled";
        instrument.oneShot = json.getProperty ("oneShot", false);

        if (auto sfz = json["sfz"].toString(); sfz.isNotEmpty() && folder.getChildFile (sfz).existsAsFile())
            instrument.sfzFile = folder.getChildFile (sfz);

        if (auto* samples = json["samples"].getArray())
        {
            for (auto& s : *samples)
            {
                SampleZone zone;
                zone.file = folder.getChildFile (s["file"].toString());
                zone.root = s.getProperty ("root", 60);
                zone.low = s.getProperty ("low", zone.root);
                zone.high = s.getProperty ("high", zone.root);
                zone.gainDb = (float) (double) s.getProperty ("gain", 0.0);

                if (zone.file.existsAsFile())
                    instrument.zones.push_back (zone);
            }
        }

        // Another pack may have an instrument with the same name; the first wins.
        if ((! instrument.zones.empty() || instrument.sfzFile.existsAsFile())
              && findSampledInstrument (instrument.info.id) == nullptr)
        {
            sounds.push_back (instrument.info);
            sampled.push_back (std::move (instrument));
        }
    }

    // Loops, described by loops.json.
    auto loopFolder = pack.getChildFile ("Loops");
    auto json = juce::JSON::parse (loopFolder.getChildFile ("loops.json"));

    if (auto* list = json.getArray())
    {
        for (auto& l : *list)
        {
            LibraryFile loop;
            loop.file = loopFolder.getChildFile (l["file"].toString());
            loop.name = l.getProperty ("name", loop.file.getFileNameWithoutExtension()).toString();
            loop.bpm = l.getProperty ("bpm", 0.0);

            juce::StringArray detail;

            if (loop.bpm > 0.0)
                detail.add (juce::String (juce::roundToInt (loop.bpm)));

            if (l["key"].toString().isNotEmpty())
                detail.add (l["key"].toString());

            loop.detail = detail.joinIntoString (juce::String::fromUTF8 (" \xc2\xb7 "));   // middle dot

            if (loop.file.existsAsFile())
                loops.push_back (loop);
        }
    }
}

std::optional<SoundInfo> SoundLibrary::findSound (const juce::String& id) const
{
    for (auto& s : sounds)
        if (s.id == id)
            return s;

    return std::nullopt;
}

double SoundLibrary::getLoopBpm (const juce::File& file) const
{
    for (auto& l : loops)
        if (l.file == file)
            return l.bpm;

    return 0.0;
}

const SoundLibrary::SynthPatch* SoundLibrary::findSynthPatch (const juce::String& id)
{
    for (auto& patch : synthPatches())
        if (synthPrefix + patch.name == id)
            return &patch;

    return nullptr;
}

const SoundLibrary::SampledInstrument* SoundLibrary::findSampledInstrument (const juce::String& id) const
{
    for (auto& s : sampled)
        if (s.info.id == id)
            return &s;

    return nullptr;
}

} // namespace ikaros
