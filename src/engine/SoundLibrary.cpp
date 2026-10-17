#include "engine/SoundLibrary.h"

namespace ikaros
{

namespace
{
    // The library's categories, in the order the sidebar shows them.
    const char* const categoryOrder[] { "Keyboards", "Drums", "Bass", "Synths", "Guitar", "Strings", "Woodwinds", "Brass" };

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
SoundLibrary::SoundLibrary (const juce::File& userSoundsFolder)
    : userFolder (userSoundsFolder)
{
    rescan();
}

juce::File SoundLibrary::getUserSoundsFolder()
{
    // The smoke test points this at a folder of its own, so it never reads
    // (or changes) the user's packs. Handy for trying the app with an empty
    // Sounds folder too.
    auto overridePath = juce::SystemStats::getEnvironmentVariable ("IKAROS_USER_SOUNDS_DIR", {});

    if (overridePath.isNotEmpty())
        return juce::File (overridePath);

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

   #if JUCE_LINUX
    // In an AppImage the program is started through the AppRun symlink in the
    // mount's root, and that's the path JUCE reports. /proc/self/exe is the
    // real file (<mount>/usr/bin/ikaros-studio).
    if (auto real = juce::File ("/proc/self/exe").getLinkedTarget(); real.existsAsFile())
        exe = real;
   #endif

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
    packs.clear();
    sounds.clear();
    sampled.clear();
    loops.clear();

    for (auto& patch : synthPatches())
        sounds.push_back ({ synthPrefix + patch.name, patch.name, patch.category, "Synth" });

    // The packs that come with the app first, so if a user's pack has the
    // same id as one of them, the app's own wins.
    const auto bundledRoot = getBundledSoundsFolder();
    std::vector<juce::File> roots { bundledRoot, userFolder };
    roots.insert (roots.end(), extraPackFolders.begin(), extraPackFolders.end());

    for (auto& root : roots)
    {
        if (! root.isDirectory())
            continue;

        // Sorted, so the library comes out the same on every computer.
        // Hidden folders are left out: PackStore unpacks downloads in them.
        auto folders = root.findChildFiles (juce::File::findDirectories | juce::File::ignoreHiddenFiles, false);
        folders.sort();

        for (auto& pack : folders)
            readPack (pack, root == bundledRoot);
    }

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

bool SoundLibrary::isValidPackId (const juce::String& id)
{
    return id.isNotEmpty() && id.containsOnly ("abcdefghijklmnopqrstuvwxyz0123456789.-");
}

int SoundLibrary::compareVersions (const juce::String& a, const juce::String& b)
{
    auto partsA = juce::StringArray::fromTokens (a, ".", {});
    auto partsB = juce::StringArray::fromTokens (b, ".", {});

    for (int i = 0; i < juce::jmax (partsA.size(), partsB.size()); ++i)
    {
        // A missing part (e.g. the patch number of "1.2") counts as 0.
        const int x = partsA[i].getIntValue(), y = partsB[i].getIntValue();

        if (x != y)
            return x < y ? -1 : 1;
    }

    return 0;
}

SoundPackInfo SoundLibrary::readPackInfo (const juce::File& folder, bool bundled)
{
    auto json = juce::JSON::parse (folder.getChildFile ("pack.json"));

    SoundPackInfo info;
    info.folder = folder;
    info.bundled = bundled;
    info.format = json.getProperty ("format", 1);
    info.id = json["id"].toString();
    info.name = json.getProperty ("name", folder.getFileName()).toString();
    info.version = json.getProperty ("version", "0.0.0").toString();
    info.description = json["description"].toString();
    info.author = json["author"].toString();
    info.license = json["license"].toString();
    info.installedSize = (juce::int64) json.getProperty ("installedSize", 0);

    if (auto licenseFile = json["licenseFile"].toString(); licenseFile.isNotEmpty())
        info.licenseFile = folder.getChildFile (licenseFile);

    // Older packs have no id: make one from the folder name, e.g.
    // "Osiris Piano" -> "local.osiris-piano". A malformed id is treated the
    // same way, so every pack ends up with a usable one.
    if (! isValidPackId (info.id))
        info.id = "local." + folder.getFileName().toLowerCase().replaceCharacter (' ', '-')
                                  .retainCharacters ("abcdefghijklmnopqrstuvwxyz0123456789.-");

    if (auto* credits = json["credits"].getArray())
    {
        for (auto& c : *credits)
            info.credits.push_back ({ c["what"].toString(), c["who"].toString(), c["license"].toString(),
                                      c["url"].toString(), (bool) c.getProperty ("attributionRequired", false) });
    }
    else if (json["credits"].isString())
    {
        // Format 1 had a single line of text.
        info.credits.push_back ({ info.name, json["credits"].toString(), info.license, {}, false });
    }

    return info;
}

std::vector<SoundLibrary::KnobMapping> SoundLibrary::readKnobs (const juce::var& json, bool isSfz)
{
    auto* list = json.getArray();

    if (list == nullptr || list->size() != numKnobs)
        return {};

    std::vector<KnobMapping> knobs;
    const juce::StringArray builtIn { "tone", "room", "drive", "width", "tune" };

    for (auto& k : *list)
    {
        KnobMapping knob;
        knob.name = k["name"].toString();
        knob.control = k["control"].toString();
        knob.cc = k.getProperty ("cc", -1);
        knob.defaultValue = juce::jlimit (0.0f, 1.0f, (float) (double) k.getProperty ("default", 0.5));

        const bool valid = knob.name.isNotEmpty()
                            && (builtIn.contains (knob.control)
                                 || (knob.control == "cc" && isSfz && juce::isPositiveAndBelow (knob.cc, 128)));

        // One bad knob and the instrument gets the category's defaults
        // instead: the Sound tab always shows five working knobs.
        if (! valid)
            return {};

        knobs.push_back (knob);
    }

    return knobs;
}

std::vector<SoundLibrary::KnobMapping> SoundLibrary::getKnobsFor (const SampledInstrument& instrument)
{
    return instrument.knobs.size() == numKnobs ? instrument.knobs : getDefaultKnobs (instrument.info.category);
}

std::vector<SoundLibrary::KnobMapping> SoundLibrary::getDefaultKnobs (const juce::String& category)
{
    // Each set uses the controls once, in the order that matters most for
    // that kind of instrument. The defaults are neutral (see
    // SoundShaperPlugin), apart from a little room.
    const KnobMapping tone  { "Tone",  "tone",  -1, 0.5f };
    const KnobMapping drive { "Drive", "drive", -1, 0.0f };
    const KnobMapping width { "Width", "width", -1, 0.5f };
    const KnobMapping pitch { "Pitch", "tune",  -1, 0.5f };

    auto room = [] (float amount) { return KnobMapping { "Room", "room", -1, amount }; };

    // No Pitch for drum kits: it moves notes, and on a kit each note is a
    // different drum, so it would swap drums instead of tuning them.
    if (category == "Drums")
        return { tone, { "Crunch", "drive", -1, 0.0f }, width, room (0.1f) };

    if (category == "Bass")
        return { tone, drive, pitch, width, room (0.0f) };

    if (category == "Guitar")
        return { tone, drive, width, room (0.2f), pitch };

    if (category == "Strings")
        return { tone, width, room (0.3f), pitch, drive };

    // Keyboards, and anything else.
    return { tone, width, room (0.2f), drive, pitch };
}

void SoundLibrary::readPack (const juce::File& pack, bool bundled)
{
    auto packInfo = readPackInfo (pack, bundled);

    // Two packs with the same id: the first one read wins.
    if (findPack (packInfo.id).has_value())
        return;

    packs.push_back (packInfo);

    // Sampled instruments: one folder each, with an instrument.json.
    for (auto& folder : pack.getChildFile ("Instruments").findChildFiles (juce::File::findDirectories, false))
    {
        auto json = juce::JSON::parse (folder.getChildFile ("instrument.json"));

        if (! json.isObject())
            continue;

        SampledInstrument instrument;
        // Sessions save sounds by pack id and folder name, so they find the
        // same instrument wherever (and on whichever system) the pack is installed.
        instrument.info.id = samplerPrefix + packInfo.id + "/" + folder.getFileName();
        instrument.info.name = json.getProperty ("name", folder.getFileName()).toString();
        instrument.info.category = json.getProperty ("category", "Keyboards").toString();
        instrument.info.detail = "Sampled";
        instrument.info.packId = packInfo.id;
        instrument.info.icon = json["icon"].toString();
        instrument.oneShot = json.getProperty ("oneShot", false);

        if (auto sfz = json["sfz"].toString(); sfz.isNotEmpty() && folder.getChildFile (sfz).existsAsFile())
            instrument.sfzFile = folder.getChildFile (sfz);

        instrument.knobs = readKnobs (json["knobs"], instrument.sfzFile != juce::File());

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

            if (auto* tags = l["tags"].getArray())
                for (auto& t : *tags)
                    loop.tags.add (t.toString().toLowerCase());

            // An explicit "unpitched" wins; otherwise drums and percussion are unpitched.
            loop.unpitched = l.hasProperty ("unpitched") ? (bool) l["unpitched"] : soundsUnpitched (loop.tags);

            if (! loop.unpitched)
                parseKey (l["key"].toString(), loop.keyRoot, loop.keyMinor);

            juce::StringArray detail;

            if (loop.bpm > 0.0)
                detail.add (juce::String (juce::roundToInt (loop.bpm)));

            if (loop.keyRoot >= 0)
                detail.add (getKeyName (loop.keyRoot, loop.keyMinor));

            loop.detail = detail.joinIntoString (juce::String::fromUTF8 (" \xc2\xb7 "));   // middle dot

            if (loop.file.existsAsFile())
                loops.push_back (loop);
        }
    }
}

std::optional<SoundPackInfo> SoundLibrary::findPack (const juce::String& id) const
{
    for (auto& p : packs)
        if (p.id == id)
            return p;

    return std::nullopt;
}

std::optional<SoundInfo> SoundLibrary::findSound (const juce::String& id) const
{
    for (auto& s : sounds)
        if (s.id == id)
            return s;

    return std::nullopt;
}

const LibraryFile* SoundLibrary::findLoop (const juce::File& file) const
{
    for (auto& l : loops)
        if (l.file == file)
            return &l;

    return nullptr;
}

bool SoundLibrary::parseKey (const juce::String& text, int& root, bool& minor)
{
    auto t = text.trim();

    if (t.isEmpty())
        return false;

    // The note letter, then an optional sharp or flat.
    static const int letterRoots[] { 9, 11, 0, 2, 4, 5, 7 };   // A B C D E F G
    const auto letter = juce::CharacterFunctions::toUpperCase (t[0]);

    if (letter < 'A' || letter > 'G')
        return false;

    int r = letterRoots[letter - 'A'];
    t = t.substring (1);

    // (juce::String indexes characters, not bytes, so the sign is one character too.)
    if (t.startsWithChar ('#') || t.startsWith (juce::String::fromUTF8 ("\xe2\x99\xaf")))       // # or the sharp sign
    {
        ++r;
        t = t.substring (1);
    }
    else if (t.startsWithChar ('b') || t.startsWith (juce::String::fromUTF8 ("\xe2\x99\xad")))  // b or the flat sign
    {
        --r;
        t = t.substring (1);
    }

    // Then the mode: nothing or "major"/"maj"/"M" for major, "minor"/"min"/"m" for minor.
    t = t.trim();
    bool isMinor;

    if (t.isEmpty() || t == "M" || t.equalsIgnoreCase ("maj") || t.equalsIgnoreCase ("major"))
        isMinor = false;
    else if (t == "m" || t.equalsIgnoreCase ("min") || t.equalsIgnoreCase ("minor"))
        isMinor = true;
    else
        return false;

    root = (r + 12) % 12;
    minor = isMinor;
    return true;
}

bool SoundLibrary::soundsUnpitched (const juce::StringArray& tags)
{
    for (auto word : { "drums", "drum", "drumloop", "drum-loop", "percussion", "beat", "beats",
                       "breakbeat", "hihat", "hi-hat", "hats", "kick", "snare" })
        if (tags.contains (word, true))
            return true;

    return false;
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

const SoundLibrary::SampledInstrument* SoundLibrary::resolveSampledInstrument (const juce::String& storedId) const
{
    if (auto* s = findSampledInstrument (storedId))
        return s;

    // Sessions saved before pack ids: "sampler:<pack folder name>/<instrument>".
    if (! storedId.startsWith (samplerPrefix))
        return nullptr;

    const auto rest = storedId.fromFirstOccurrenceOf (samplerPrefix, false, false);
    const auto folderName = rest.upToFirstOccurrenceOf ("/", false, false);
    const auto instrument = rest.fromFirstOccurrenceOf ("/", false, false);

    for (auto& p : packs)
        if (p.folder.getFileName() == folderName)
            return findSampledInstrument (samplerPrefix + p.id + "/" + instrument);

    return nullptr;
}

juce::String SoundLibrary::getPackIdOf (const juce::String& soundId)
{
    if (! soundId.startsWith (samplerPrefix))
        return {};

    return soundId.fromFirstOccurrenceOf (samplerPrefix, false, false).upToFirstOccurrenceOf ("/", false, false);
}

} // namespace ikaros
