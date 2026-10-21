#include "engine/EffectsController.h"
#include "engine/SoundControls.h"
#include "engine/AudioEngine.h"
#include "engine/BuiltInEffects.h"
#include "engine/PortedPedals.h"
#include "engine/AmpPlugin.h"
#include "engine/PedalStyles.h"
#include "engine/PluginLibrary.h"
#include "engine/SfzPlugin.h"
#include "engine/SoundLibrary.h"
#include "engine/TrackHelpers.h"
#include <algorithm>
#include <array>

namespace te = tracktion;

namespace ikaros
{

using namespace detail;

namespace
{
    // The built-in effects offered in the "+ Add effect" menu, with the names
    // and groups the user sees. Order here = order in the menu.
    const std::vector<EffectType>& builtInEffects()
    {
        static const std::vector<EffectType> effects {
            { te::CompressorPlugin::xmlTypeName, "Compressor",            "Dynamics" },
            { LimiterPlugin::xmlTypeName,        "Limiter",               "Dynamics" },
            { NoiseGatePlugin::xmlTypeName,      "Noise Gate",            "Dynamics" },
            { SlowSwellPlugin::xmlTypeName,      SlowSwellPlugin::getPluginName(),   "Dynamics" },
            { te::EqualiserPlugin::xmlTypeName,  "EQ",                    "EQ & Filter" },
            { te::LowPassPlugin::xmlTypeName,    "Low/High-Pass Filter",  "EQ & Filter" },
            { EnvelopeWahPlugin::xmlTypeName,    EnvelopeWahPlugin::getPluginName(), "EQ & Filter" },
            { DistortionPlugin::xmlTypeName,     "Distortion",            "Distortion" },
            { GoldenBoostPlugin::xmlTypeName,    GoldenBoostPlugin::getPluginName(), "Distortion" },
            { YellowDrivePlugin::xmlTypeName,    YellowDrivePlugin::getPluginName(), "Distortion" },
            { MellowDrivePlugin::xmlTypeName,    MellowDrivePlugin::getPluginName(), "Distortion" },
            { TubeDrivePlugin::xmlTypeName,      TubeDrivePlugin::getPluginName(),   "Distortion" },
            { HotPlatePlugin::xmlTypeName,       HotPlatePlugin::getPluginName(),    "Distortion" },
            { StackDrivePlugin::xmlTypeName,     StackDrivePlugin::getPluginName(),  "Distortion" },
            { SunFuzzPlugin::xmlTypeName,        SunFuzzPlugin::getPluginName(),     "Distortion" },
            { VintageFuzzPlugin::xmlTypeName,    VintageFuzzPlugin::getPluginName(), "Distortion" },
            { SuperFuzzPlugin::xmlTypeName,      SuperFuzzPlugin::getPluginName(),   "Distortion" },
            { AmpPlugin::xmlTypeName,            AmpPlugin::getPluginName(),         "Distortion" },
            { te::ReverbPlugin::xmlTypeName,     "Reverb",                "Space" },
            { te::DelayPlugin::xmlTypeName,      "Echo",                  "Space" },
            { SpringTankPlugin::xmlTypeName,     SpringTankPlugin::getPluginName(),  "Space" },
            { te::ChorusPlugin::xmlTypeName,     "Chorus",                "Modulation" },
            { te::PhaserPlugin::xmlTypeName,     "Phaser",                "Modulation" },
            { JetFlangerPlugin::xmlTypeName,     JetFlangerPlugin::getPluginName(),  "Modulation" },
            { TremoloPlugin::xmlTypeName,        "Tremolo",               "Modulation" },
            { te::PitchShiftPlugin::xmlTypeName, "Pitch Shift",           "Pitch" },
        };
        return effects;
    }

    juce::String displayNameFor (te::Plugin& p)
    {
        for (auto& e : builtInEffects())
            if (e.type == p.getPluginType())
                return e.name;

        if (dynamic_cast<te::FourOscPlugin*> (&p) != nullptr)
            return "4OSC Synth";

        if (dynamic_cast<te::SamplerPlugin*> (&p) != nullptr || dynamic_cast<SfzPlugin*> (&p) != nullptr)
            return "Sampler";

        return p.getName();
    }

    // Effects are every plugin on the track except the instrument, the
    // track's own volume/meter plugins (which Tracktion keeps at the end),
    // the sends to the shared buses (see SendBuses), the EQ tab's
    // equaliser (see EqController) and the Sound tab's hidden plugins.
    bool isEffect (te::Plugin& p)
    {
        return ! isInstrumentPlugin (p)
            && ! isEqPlugin (p)
            && ! isSoundShapingPlugin (p)
            && dynamic_cast<te::VolumeAndPanPlugin*> (&p) == nullptr
            && dynamic_cast<te::LevelMeterPlugin*> (&p) == nullptr
            && dynamic_cast<te::AuxSendPlugin*> (&p) == nullptr
            && dynamic_cast<te::AuxReturnPlugin*> (&p) == nullptr;
    }

    // A track's effects, or the master's (see findPluginList).
    std::vector<te::Plugin*> effectsOn (te::PluginList& list)
    {
        std::vector<te::Plugin*> result;

        for (auto* p : list)
            if (isEffect (*p))
                result.push_back (p);

        return result;
    }

    std::vector<te::Plugin*> instrumentsOn (te::AudioTrack& t)
    {
        std::vector<te::Plugin*> result;

        for (auto* p : t.pluginList)
            if (isInstrumentPlugin (*p))
                result.push_back (p);

        return result;
    }

    std::vector<te::Plugin*> soundShapingPluginsOn (te::AudioTrack& t)
    {
        std::vector<te::Plugin*> result;

        for (auto* p : t.pluginList)
            if (isSoundShapingPlugin (*p))
                result.push_back (p);

        return result;
    }

    te::Plugin* findEffect (te::PluginList& list, PluginId id)
    {
        for (auto* p : effectsOn (list))
            if (p->itemID.getRawID() == id)
                return p;

        return nullptr;
    }

    //==============================================================================
    // Pedals first, then the Amp (see EffectsController.h).
    bool isPedal (te::Plugin& p)    { return findPedalStyle (getPluginTypeString (p)) != nullptr; }
    bool isAmp (te::Plugin& p)      { return dynamic_cast<AmpPlugin*> (&p) != nullptr; }

    // Where an effect belongs in an audio track's chain: 0 = with the
    // pedals, 1 = the Amp, 2 = with the other effects.
    int chainRank (te::Plugin& p)   { return isPedal (p) ? 0 : isAmp (p) ? 1 : 2; }

    AmpPlugin* findAmp (te::PluginList& list)
    {
        for (auto* p : list)
            if (auto* amp = dynamic_cast<AmpPlugin*> (p))
                return amp;

        return nullptr;
    }

    // Only audio tracks keep their pedals first: they're the ones with a
    // pedalboard. (findTrack is null for the master.)
    bool keepsPedalsFirst (te::Edit* edit, TrackId id)
    {
        auto* t = findTrack (edit, id);
        return t != nullptr && isAudioTrack (*t);
    }

    // How many effects at the start of the chain rank no higher than
    // `rank` (0: the leading pedals; 1: those and the Amp after them).
    int countLeading (const std::vector<te::Plugin*>& effects, int rank)
    {
        int n = 0;

        while (n < (int) effects.size() && chainRank (*effects[(size_t) n]) <= rank)
            ++n;

        return n;
    }

    // Where an effect may go among `others` (the chain without it) when
    // `position` is asked for (-1 = the end): a pedal no further than the
    // end of the pedals, the Amp just after them, any other effect after
    // both. (A pedal added "at the end" goes after the last pedal.)
    int positionKeepingPedalsFirst (te::Plugin& effect, const std::vector<te::Plugin*>& others, int position)
    {
        const int rank = chainRank (effect);
        const int earliest = rank == 0 ? 0 : countLeading (others, rank - 1);

        if (rank == 2)
            return position < 0 ? -1 : juce::jmax (position, earliest);

        const int latest = countLeading (others, rank);
        return position < 0 ? latest : juce::jlimit (earliest, latest, position);
    }

    PluginSlot makeSlot (te::Plugin& p)
    {
        PluginSlot slot { p.itemID.getRawID(), getPluginTypeString (p), displayNameFor (p), p.isEnabled() };

        if (auto* external = dynamic_cast<te::ExternalPlugin*> (&p))
        {
            slot.isThirdParty = true;
            slot.missing = external->isMissing() || external->isDisabled();
            slot.manufacturer = external->getVendor();

            // A plug-in that never loaded still has the name it was saved with.
            if (slot.name.isEmpty())
                slot.name = p.state.getProperty (te::IDs::name).toString();
        }

        return slot;
    }

    // A 4OSC synth with a patch's settings. They go into its state before it's
    // created, so building it isn't a pile of separate undo steps.
    te::Plugin::Ptr createSynth (te::Edit& edit, const SoundLibrary::SynthPatch& patch)
    {
        auto state = te::createValueTree (te::IDs::PLUGIN, te::IDs::type, te::FourOscPlugin::xmlTypeName);

        for (auto& [property, value] : patch.settings)
        {
            // Whole numbers (wave shapes, on/off switches) are stored as ints,
            // the way the synth stores them itself.
            if (value == std::floor (value))
                state.setProperty (property, (int) value, nullptr);
            else
                state.setProperty (property, value, nullptr);
        }

        return edit.getPluginCache().createNewPlugin (state);
    }

    // Tracktion's sampler, with one SOUND per sample zone.
    te::Plugin::Ptr createSampler (te::Edit& edit, const SoundLibrary::SampledInstrument& instrument)
    {
        auto state = te::createValueTree (te::IDs::PLUGIN, te::IDs::type, te::SamplerPlugin::xmlTypeName);

        for (auto& zone : instrument.zones)
            state.appendChild (te::createValueTree (te::IDs::SOUND,
                                                    te::IDs::source, zone.file.getFullPathName(),
                                                    te::IDs::name, zone.file.getFileNameWithoutExtension(),
                                                    te::IDs::startTime, 0.0,
                                                    te::IDs::length, 0.0,
                                                    te::IDs::keyNote, zone.root,
                                                    te::IDs::minNote, zone.low,
                                                    te::IDs::maxNote, zone.high,
                                                    te::IDs::gainDb, zone.gainDb,
                                                    te::IDs::pan, 0.0f,
                                                    te::IDs::openEnded, instrument.oneShot),
                               nullptr);

        auto plugin = edit.getPluginCache().createNewPlugin (state);

        // The sampler loads its samples on the next message-loop turn; load them now.
        if (auto* sampler = dynamic_cast<te::SamplerPlugin*> (plugin.get()))
            sampler->flushPendingUpdates();

        return plugin;
    }

    // Our sfizz instrument, playing an SFZ file (see SfzPlugin), with the
    // controllers its "cc" knobs set at their defaults. It loads the file in
    // the background, so this returns straight away.
    te::Plugin::Ptr createSfzInstrument (te::Edit& edit, const juce::File& sfzFile,
                                         const std::vector<SoundLibrary::KnobMapping>& knobs)
    {
        auto state = te::createValueTree (te::IDs::PLUGIN, te::IDs::type, SfzPlugin::xmlTypeName);
        state.setProperty (SfzPlugin::sfzFileProp, sfzFile.getFullPathName(), nullptr);

        for (auto& k : knobs)
            if (k.control == "cc")
                state.setProperty (SfzPlugin::ccProperty (k.cc), k.defaultValue, nullptr);

        return edit.getPluginCache().createNewPlugin (state);
    }

    // Where in the plugin list effect number `position` should go: before
    // the existing effect at that position, or (for "the end") just after
    // the last effect, or else before the EQ and the volume plugin.
    int pluginListIndexForPosition (te::PluginList& list, int position)
    {
        auto effects = effectsOn (list);

        if (juce::isPositiveAndBelow (position, (int) effects.size()))
            return list.indexOf (effects[(size_t) position]);

        if (! effects.empty())
            return list.indexOf (effects.back()) + 1;

        for (int i = 0; i < list.size(); ++i)
            if (isEqPlugin (*list[i]) || dynamic_cast<te::VolumeAndPanPlugin*> (list[i]) != nullptr)
                return i;

        return list.size();
    }

    juce::String percent (float v)    { return juce::String (juce::roundToInt (v * 100.0f)) + "%"; }

    // Turns a built-in effect's knob to 0..1 of its range.
    // Like setParameterUndoably, but we don't know which state property
    // backs an arbitrary effect's parameter: the ID isn't it (the Delay's
    // "mix proportion" lives in "mix", plug-in IDs are numbers), and writing
    // the ID as a property made saved sessions invalid XML. Instead, setting
    // the parameter to its current value makes Tracktion store it in the
    // right property, outside the undo history (it writes its attached
    // CachedValue with no UndoManager on every notifying set).
    void setEffectKnob (te::AutomatableParameter& param, float normalised0to1)
    {
        param.setParameter (param.getCurrentValue(), juce::sendNotification);
        param.setParameter (param.valueRange.convertFrom0to1 (normalised0to1), juce::sendNotification);
    }

    // The track inspector's Noise Gate section works on the first noise
    // gate in the chain.
    NoiseGatePlugin* findNoiseGate (te::PluginList& list)
    {
        for (auto* p : effectsOn (list))
            if (auto* gate = dynamic_cast<NoiseGatePlugin*> (p))
                return gate;

        return nullptr;
    }

    // A few of Tracktion's built-in effects (Chorus, Phaser, the Echo's
    // time) keep their settings as plain properties on their state rather
    // than as parameters, so they'd show no knobs. These are their settings,
    // with the ranges the knobs cover and the defaults Tracktion uses.
    struct StoredSetting
    {
        const char* pluginType;
        juce::Identifier property;
        const char* name;
        float minimum, maximum, defaultValue;
        const char* unit;           // "%" shows 0..1 as a percentage
        bool wholeNumber = false;   // the effect reads it as an int (stored rounded)
    };

    const std::vector<StoredSetting>& storedSettings()
    {
        static const std::vector<StoredSetting> settings {
            { te::ChorusPlugin::xmlTypeName, te::IDs::depthMs,       "Depth",    0.5f, 10.0f, 3.0f, "ms" },
            { te::ChorusPlugin::xmlTypeName, te::IDs::speedHz,       "Speed",    0.1f,  5.0f, 1.0f, "Hz" },
            { te::ChorusPlugin::xmlTypeName, te::IDs::width,         "Width",    0.0f,  1.0f, 0.5f, "%" },
            { te::ChorusPlugin::xmlTypeName, te::IDs::mixProportion, "Mix",      0.0f,  1.0f, 0.5f, "%" },
            { te::PhaserPlugin::xmlTypeName, te::IDs::depth,         "Depth",    0.0f, 10.0f, 5.0f, "" },
            { te::PhaserPlugin::xmlTypeName, te::IDs::rate,          "Rate",     0.05f, 4.0f, 0.4f, "Hz" },
            { te::PhaserPlugin::xmlTypeName, te::IDs::feedback,      "Feedback", 0.0f,  0.95f, 0.7f, "%" },
            // Up to 2 s: the Echo makes room for that much when playback
            // starts (external/patches/tracktion-delay-preallocate.patch), so
            // turning it while playing never allocates on the audio thread.
            { te::DelayPlugin::xmlTypeName,  te::IDs::length,        "Time",     20.0f, 2000.0f, 150.0f, "ms", true },
        };
        return settings;
    }

    std::vector<const StoredSetting*> storedSettingsFor (te::Plugin& p)
    {
        std::vector<const StoredSetting*> result;

        for (auto& setting : storedSettings())
            if (p.getPluginType() == setting.pluginType)
                result.push_back (&setting);

        return result;
    }

    juce::String describeStored (const StoredSetting& s, float value)
    {
        if (juce::String (s.unit) == "%")
            return percent (value);

        if (s.wholeNumber)
            return juce::String (juce::roundToInt (value)) + " " + juce::String (s.unit);

        return juce::String (value, 1) + (juce::String (s.unit).isEmpty() ? "" : " " + juce::String (s.unit));
    }

    juce::String seconds (float s)
    {
        return s < 1.0f ? juce::String (juce::roundToInt (s * 1000.0f)) + " ms"
                        : juce::String (s, 1) + " s";
    }

    // The knobs on the Sound tab, mapped onto 4OSC parameters.
    struct KnobMapping { const char* label; const char* paramID; };
    const std::array<KnobMapping, 5> knobMappings {{
        { "Brightness", "filterFreq" },
        { "Resonance",  "filterResonance" },
        { "Attack",     "ampAttack" },
        { "Release",    "ampRelease" },
        { "Room",       "reverbMix" },
    }};

    //==============================================================================
    // Sampled instruments' knobs (see SoundLibrary::KnobMapping). They work
    // three ways:
    //   - "tone", "drive", "width", "room": parameters of a hidden
    //     SoundShaperPlugin right after the instrument
    //   - "tune": a hidden TransposePlugin just before it
    //   - "cc": a MIDI controller the SFZ file maps, stored on the SfzPlugin
    // Old sessions (and knobs nobody has turned) may not have the hidden
    // plugins yet; they're added the first time a knob needs them.
    using Knob = SoundLibrary::KnobMapping;

    bool isShaperControl (const juce::String& control)    { return control != "tune" && control != "cc"; }

    te::Plugin* sampledInstrumentOn (te::AudioTrack& t)
    {
        for (auto* p : t.pluginList)
            if (dynamic_cast<te::SamplerPlugin*> (p) != nullptr || dynamic_cast<SfzPlugin*> (p) != nullptr)
                return p;

        return nullptr;
    }

    // The pack a track's sampled sound needs, if it isn't installed (or the
    // pack no longer has that instrument). Empty when nothing's missing.
    juce::String missingPackFor (te::AudioTrack& t, const SoundLibrary& sounds)
    {
        const auto soundId = t.state.getProperty (soundIdProp).toString();
        const auto packId = SoundLibrary::getPackIdOf (soundId);

        if (packId.isEmpty() || sounds.findSampledInstrument (soundId) != nullptr)
            return {};

        return packId;
    }

    // The knobs a track's Sound tab shows, or none if it doesn't play a
    // sampled instrument. A sound whose pack isn't installed has none
    // either: the Sound tab offers to download the pack instead. (Its
    // settings stay in the session, ready for when it's back.)
    std::vector<Knob> sampledKnobsFor (te::AudioTrack& t, const SoundLibrary& sounds)
    {
        if (sampledInstrumentOn (t) == nullptr || missingPackFor (t, sounds).isNotEmpty())
            return {};

        if (auto* instrument = sounds.findSampledInstrument (t.state.getProperty (soundIdProp).toString()))
            return SoundLibrary::getKnobsFor (*instrument);

        // A sampler without a library sound (shouldn't happen): general knobs.
        return SoundLibrary::getDefaultKnobs (t.state.getProperty (categoryProp).toString());
    }

    // "tune" knobs move by whole semitones: 0..1 covers -12..+12, 0.5 = none.
    float knobToSemitones (float v)
    {
        return (float) juce::roundToInt ((v - 0.5f) * 2.0f * TransposePlugin::maxSemitones);
    }

    float semitonesToKnob (float semitones)    { return 0.5f + semitones / (2.0f * TransposePlugin::maxSemitones); }

    juce::String describeKnob (const Knob& knob, float v)
    {
        if (knob.control == "tune")
        {
            const auto semitones = juce::roundToInt (knobToSemitones (v));
            return semitones > 0 ? "+" + juce::String (semitones) : juce::String (semitones);
        }

        if (knob.control == "width")
            return percent (v * 2.0f);     // 100% = as recorded

        return percent (v);
    }

    float getKnobValue (te::AudioTrack& t, const Knob& knob)
    {
        if (knob.control == "cc")
        {
            if (auto* sfz = t.pluginList.findFirstPluginOfType<SfzPlugin>())
                return (float) sfz->state.getProperty (SfzPlugin::ccProperty (knob.cc), knob.defaultValue);

            return knob.defaultValue;
        }

        if (knob.control == "tune")
        {
            if (auto* transpose = t.pluginList.findFirstPluginOfType<TransposePlugin>())
                return semitonesToKnob (transpose->getSemitonesParameter()->getCurrentValue());

            return 0.5f;
        }

        if (auto* shaper = t.pluginList.findFirstPluginOfType<SoundShaperPlugin>())
            if (auto* param = shaper->getParameterFor (knob.control))
                return param->getCurrentValue();

        // No shaper yet: the sound is untouched, which is what the neutral value means.
        return SoundShaperPlugin::neutralValueOf (knob.control);
    }

    // The hidden plugins a set of knobs needs, set to the knobs' defaults.
    // The settings go into the state before the plugin is created, so they
    // aren't separate undo steps.
    te::Plugin::Ptr createShaper (te::Edit& edit, const std::vector<Knob>& knobs)
    {
        auto state = te::createValueTree (te::IDs::PLUGIN, te::IDs::type, SoundShaperPlugin::xmlTypeName);

        for (auto& k : knobs)
            if (isShaperControl (k.control))
                state.setProperty (juce::Identifier (k.control), k.defaultValue, nullptr);

        return edit.getPluginCache().createNewPlugin (state);
    }

    te::Plugin::Ptr createTranspose (te::Edit& edit, const std::vector<Knob>& knobs)
    {
        auto state = te::createValueTree (te::IDs::PLUGIN, te::IDs::type, TransposePlugin::xmlTypeName);

        for (auto& k : knobs)
            if (k.control == "tune")
                state.setProperty (TransposePlugin::semitonesID, knobToSemitones (k.defaultValue), nullptr);

        return edit.getPluginCache().createNewPlugin (state);
    }

    // The hidden plugins the knobs need, added the first time a knob needs
    // them (inside the undo step the UI started).
    TransposePlugin* findOrAddTranspose (te::AudioTrack& t, te::Plugin& instrument)
    {
        if (auto* transpose = t.pluginList.findFirstPluginOfType<TransposePlugin>())
            return transpose;

        auto created = createTranspose (t.edit, {});
        t.pluginList.insertPlugin (created, t.pluginList.indexOf (&instrument), nullptr);
        return dynamic_cast<TransposePlugin*> (created.get());
    }

    SoundShaperPlugin* findOrAddShaper (te::AudioTrack& t, te::Plugin& instrument)
    {
        if (auto* shaper = t.pluginList.findFirstPluginOfType<SoundShaperPlugin>())
            return shaper;

        auto created = createShaper (t.edit, {});
        t.pluginList.insertPlugin (created, t.pluginList.indexOf (&instrument) + 1, nullptr);
        return dynamic_cast<SoundShaperPlugin*> (created.get());
    }

    // Sets a knob, adding the hidden plugin it needs if it isn't there yet.
    void setKnobValue (te::AudioTrack& t, const Knob& knob, float v)
    {
        auto* instrument = sampledInstrumentOn (t);
        auto& edit = t.edit;

        if (instrument == nullptr)
            return;

        if (knob.control == "cc")
        {
            if (auto* sfz = dynamic_cast<SfzPlugin*> (instrument))
                sfz->state.setProperty (SfzPlugin::ccProperty (knob.cc), v, &edit.getUndoManager());
        }
        else if (knob.control == "tune")
        {
            if (auto* transpose = findOrAddTranspose (t, *instrument))
                setParameterUndoably (*transpose, *transpose->getSemitonesParameter(),
                                      TransposePlugin::semitonesID, knobToSemitones (v));
        }
        else if (auto* shaper = findOrAddShaper (t, *instrument))
        {
            if (auto* param = shaper->getParameterFor (knob.control))
                setParameterUndoably (*shaper, *param, juce::Identifier (knob.control), v);
        }
    }

    // How the built-in synth's knobs read, from the parameter's own value.
    juce::String describeSynthKnob (const juce::String& paramID, float raw, float normalised)
    {
        if (paramID == "ampAttack" || paramID == "ampRelease")  return seconds (raw);
        if (paramID == "filterResonance")                       return juce::String (juce::roundToInt (raw)) + "%";
        return percent (normalised);
    }
}

//==============================================================================
std::vector<SoundControlParameter> getSoundControlParameters (te::AudioTrack& t, const SoundLibrary& sounds, bool createMissing)
{
    std::vector<SoundControlParameter> result;

    if (auto* synth = findSynth (t))
    {
        for (auto& k : knobMappings)
        {
            SoundControlParameter c;
            c.name = k.label;
            c.parameter = synth->getAutomatableParameterByID (k.paramID).get();
            c.stateProperty = k.paramID;

            if (auto* param = c.parameter)
            {
                // valueRange maps 0..1 onto the parameter's own (possibly skewed) range.
                c.toParameter = [param] (float v) { return param->valueRange.convertFrom0to1 (v); };
                c.fromParameter = [param] (float raw) { return param->valueRange.convertTo0to1 (raw); };
                c.describe = [param, id = juce::String (k.paramID)] (float v)
                             { return describeSynthKnob (id, param->valueRange.convertFrom0to1 (v), v); };

                // Some patches have the synth's reverb switched off; automating "Room" needs it on.
                if (createMissing && param == synth->reverbMix.get() && ! synth->reverbOnValue.get())
                    synth->reverbOnValue = true;
            }

            result.push_back (std::move (c));
        }

        return result;
    }

    auto* instrument = sampledInstrumentOn (t);

    for (auto& knob : sampledKnobsFor (t, sounds))
    {
        SoundControlParameter c;
        c.name = knob.name;
        c.toParameter = c.fromParameter = [] (float v) { return v; };
        c.describe = [knob] (float v) { return describeKnob (knob, v); };
        c.valueWithoutParameter = knob.control == "cc" ? knob.defaultValue : getKnobValue (t, knob);

        if (knob.control == "cc")
        {
            if (auto* sfz = dynamic_cast<SfzPlugin*> (instrument))
                c.parameter = sfz->getCcParameter (knob.cc);

            c.stateProperty = SfzPlugin::ccProperty (knob.cc);
        }
        else if (knob.control == "tune")
        {
            auto* transpose = createMissing ? findOrAddTranspose (t, *instrument)
                                            : t.pluginList.findFirstPluginOfType<TransposePlugin>();

            if (transpose != nullptr)
                c.parameter = transpose->getSemitonesParameter();

            c.stateProperty = TransposePlugin::semitonesID;
            c.toParameter = knobToSemitones;
            c.fromParameter = semitonesToKnob;
        }
        else
        {
            auto* shaper = createMissing ? findOrAddShaper (t, *instrument)
                                         : t.pluginList.findFirstPluginOfType<SoundShaperPlugin>();

            if (shaper != nullptr)
                c.parameter = shaper->getParameterFor (knob.control);

            c.stateProperty = juce::Identifier (knob.control);
        }

        result.push_back (std::move (c));
    }

    return result;
}

//==============================================================================
EffectsController::EffectsController (AudioEngine& o) : owner (o)
{
}

//==============================================================================
std::vector<SoundControl> EffectsController::getSoundControls (TrackId id) const
{
    std::vector<SoundControl> result;
    auto* t = findTrack (owner.getEdit(), id);
    auto* synth = t != nullptr ? findSynth (*t) : nullptr;

    if (t == nullptr)
        return result;

    // A sampled instrument: the knobs its instrument.json (or its category) asks for.
    if (synth == nullptr)
    {
        for (auto& knob : sampledKnobsFor (*t, owner.getSounds()))
        {
            const auto value = juce::jlimit (0.0f, 1.0f, getKnobValue (*t, knob));
            result.push_back ({ knob.name, value, describeKnob (knob, value) });
        }

        // Audio tracks and plug-in instruments have none.
        return result;
    }

    for (auto& k : knobMappings)
    {
        SoundControl c;
        c.name = k.label;

        if (auto param = synth->getAutomatableParameterByID (k.paramID))
        {
            c.value = param->getCurrentNormalisedValue();
            auto raw = param->getCurrentValue();
            juce::String paramID (k.paramID);

            if (paramID == "reverbMix" && ! synth->reverbOnValue.get())  { c.value = 0.0f; c.valueText = "0%"; }
            else                                                         c.valueText = describeSynthKnob (paramID, raw, c.value);
        }

        result.push_back (c);
    }

    return result;
}

void EffectsController::setSoundControl (TrackId id, int index, float value)
{
    auto* t = findTrack (owner.getEdit(), id);

    if (t == nullptr)
        return;

    // A sampled instrument (or one with no knobs: then this does nothing).
    if (findSynth (*t) == nullptr)
    {
        auto knobs = sampledKnobsFor (*t, owner.getSounds());

        if (juce::isPositiveAndBelow (index, (int) knobs.size()))
            setKnobValue (*t, knobs[(size_t) index], juce::jlimit (0.0f, 1.0f, value));

        return;
    }

    if (! juce::isPositiveAndBelow (index, (int) knobMappings.size()))
        return;

    // The built-in synth.
    auto* synth = findSynth (*t);

    if (auto param = synth->getAutomatableParameterByID (knobMappings[(size_t) index].paramID))
    {
        // valueRange maps 0..1 onto the parameter's own (possibly skewed) range.
        auto raw = param->valueRange.convertFrom0to1 (juce::jlimit (0.0f, 1.0f, value));
        setParameterUndoably (*synth, *param, knobMappings[(size_t) index].paramID, raw);

        // Some patches have the synth's reverb switched off; "Room" switches it on.
        if (param == synth->reverbMix && raw > 0.0f && ! synth->reverbOnValue.get())
            synth->reverbOnValue = true;
    }
}

//==============================================================================
// The instrument
bool EffectsController::setSound (TrackId id, const juce::String& soundId, bool newUndoStep)
{
    auto* edit = owner.getEdit();
    auto* t = findTrack (edit, id);

    if (t == nullptr || isAudioTrack (*t))
        return false;

    // Make the new instrument first: if it can't be made, nothing changes.
    te::Plugin::Ptr instrument, shaper, transpose;
    juce::String name, category, icon, packName;

    if (auto* patch = SoundLibrary::findSynthPatch (soundId))
    {
        instrument = createSynth (*edit, *patch);
        name = patch->name;
        category = patch->category;
    }
    else if (auto* sampled = owner.getSounds().findSampledInstrument (soundId))
    {
        const auto knobs = SoundLibrary::getKnobsFor (*sampled);
        instrument = sampled->sfzFile.existsAsFile() ? createSfzInstrument (*edit, sampled->sfzFile, knobs)
                                                     : createSampler (*edit, *sampled);

        // The Sound tab's knobs, starting at their defaults.
        if (std::any_of (knobs.begin(), knobs.end(), [] (const Knob& k) { return isShaperControl (k.control); }))
            shaper = createShaper (*edit, knobs);

        if (std::any_of (knobs.begin(), knobs.end(), [] (const Knob& k) { return k.control == "tune"; }))
            transpose = createTranspose (*edit, knobs);

        name = sampled->info.name;
        category = sampled->info.category;
        icon = sampled->info.icon;

        if (auto pack = owner.getSounds().findPack (sampled->info.packId))
            packName = pack->name;
    }
    else if (auto info = owner.getPlugins().findPlugin (soundId.fromFirstOccurrenceOf (thirdPartyPrefix, false, false)))
    {
        if (soundId.startsWith (thirdPartyPrefix) && info->isInstrument)
        {
            instrument = createPlugin (*edit, soundId);
            name = info->name;
            category = "Plug-ins";
        }
    }

    if (instrument == nullptr)
        return false;

    instrument->state.setProperty (instrumentProp, true, nullptr);

    if (newUndoStep)
        owner.beginUndoStep ("Change Sound");

    auto* um = &edit->getUndoManager();

    // Keep a name the user typed. The name is the app's if it's the one we
    // last gave the track, or (older sessions, which didn't store that) the
    // old sound's name or "Instrument N".
    // (Renaming a track clears the stored name.)
    const auto currentName = t->getName();
    const bool nameIsAutomatic = t->state.hasProperty (autoNameProp)
                                    ? currentName == t->state.getProperty (autoNameProp).toString()
                                    : currentName == t->state.getProperty (soundNameProp).toString()
                                        || currentName.startsWith ("Instrument ");

    // Out with the old instrument and its hidden knob plugins...
    for (auto* old : instrumentsOn (*t))
        old->removeFromParent();

    for (auto* old : soundShapingPluginsOn (*t))
        old->removeFromParent();

    // ...and in with the new: [transpose] instrument [shaper], then the effects.
    t->pluginList.insertPlugin (instrument, 0, nullptr);

    if (transpose != nullptr)
        t->pluginList.insertPlugin (transpose, 0, nullptr);

    if (shaper != nullptr)
        t->pluginList.insertPlugin (shaper, t->pluginList.indexOf (instrument.get()) + 1, nullptr);
    t->state.setProperty (soundIdProp, soundId, um);
    t->state.setProperty (soundNameProp, name, um);
    t->state.setProperty (categoryProp, category, um);
    t->state.setProperty (soundIconProp, icon, um);
    t->state.setProperty (packNameProp, packName, um);

    if (nameIsAutomatic)
    {
        // A second track with the same sound gets "Solo Violin 2", and so on.
        const auto trackName = uniqueTrackName (*edit, name, t);
        t->setName (trackName);
        t->state.setProperty (autoNameProp, trackName, um);
    }

    return true;
}

juce::String EffectsController::getSoundId (TrackId id) const
{
    if (auto* t = findTrack (owner.getEdit(), id))
    {
        auto soundId = t->state.getProperty (soundIdProp).toString();

        // Sessions from before the sound library only stored the sound's name.
        if (soundId.isEmpty() && ! isAudioTrack (*t))
            for (auto& s : owner.getSounds().getSounds())
                if (s.name == t->state.getProperty (soundNameProp, "Basic Synth").toString())
                    return s.id;

        return soundId;
    }

    return {};
}

//==============================================================================
// Plug-ins
bool EffectsController::isInstrumentReady (TrackId id) const
{
    if (auto* t = findTrack (owner.getEdit(), id))
        for (auto* p : t->pluginList)
            if (auto* sfz = dynamic_cast<SfzPlugin*> (p))
                return sfz->isLoaded();

    return true;
}

std::optional<std::pair<int, int>> EffectsController::getInstrumentKeyRange (TrackId id) const
{
    if (auto* t = findTrack (owner.getEdit(), id))
        for (auto* p : t->pluginList)
            if (auto* sfz = dynamic_cast<SfzPlugin*> (p))
                if (int lowest = 0, highest = 0; sfz->getKeyRange (lowest, highest))
                    return std::make_pair (lowest, highest);

    return std::nullopt;
}

std::optional<PluginSlot> EffectsController::getInstrument (TrackId id) const
{
    if (auto* t = findTrack (owner.getEdit(), id))
    {
        for (auto* p : t->pluginList)
        {
            if (isInstrumentPlugin (*p))
            {
                auto slot = makeSlot (*p);

                // A sampled instrument is "missing" like an uninstalled
                // plug-in when its sound pack isn't on this computer.
                if (sampledInstrumentOn (*t) == p)
                {
                    slot.missingPackId = missingPackFor (*t, owner.getSounds());
                    slot.missing = slot.missingPackId.isNotEmpty();
                    slot.missingPackName = t->state.getProperty (packNameProp).toString();
                }

                return slot;
            }
        }
    }

    return std::nullopt;
}

std::vector<PluginSlot> EffectsController::getEffectChain (TrackId id) const
{
    std::vector<PluginSlot> result;

    if (auto* list = findPluginList (owner.getEdit(), id))
        for (auto* p : effectsOn (*list))
            result.push_back (makeSlot (*p));

    return result;
}

std::vector<EffectType> EffectsController::getAvailableEffects() const
{
    auto effects = builtInEffects();
    auto plugins = owner.getPlugins().getEffects();

    // Grouped by maker, then by name.
    std::stable_sort (plugins.begin(), plugins.end(), [] (const PluginInfo& a, const PluginInfo& b)
    {
        return a.manufacturer.compareNatural (b.manufacturer) < 0;
    });

    for (auto& p : plugins)
        effects.push_back ({ thirdPartyPrefix + p.id, p.name,
                             p.manufacturer.isNotEmpty() ? p.manufacturer : juce::String ("Plug-ins"), true });

    return effects;
}

// An audio track's pedalboard shown (true) or hidden (false) by the user;
// not there = the default (see hasPedalboard).
static const juce::Identifier pedalboardProp ("ikarosPedalboard");

bool EffectsController::hasPedalboard (TrackId id) const
{
    auto* t = findTrack (owner.getEdit(), id);

    if (t == nullptr || ! isAudioTrack (*t))
        return false;

    if (t->state.hasProperty (pedalboardProp))
        return (bool) t->state.getProperty (pedalboardProp);

    const auto icon = t->state.getProperty (iconProp).toString();

    if (icon == "guitar" || icon == "bass")
        return true;

    auto effects = effectsOn (t->pluginList);
    return std::any_of (effects.begin(), effects.end(), [] (te::Plugin* p) { return isPedal (*p) || isAmp (*p); });
}

void EffectsController::setPedalboardShown (TrackId id, bool shown)
{
    auto* t = findTrack (owner.getEdit(), id);

    if (t == nullptr || ! isAudioTrack (*t) || hasPedalboard (id) == shown)
        return;

    owner.beginUndoStep (shown ? "Show Pedalboard" : "Hide Pedalboard");
    t->state.setProperty (pedalboardProp, shown, &owner.getEdit()->getUndoManager());
}

void EffectsController::showPluginWindow (TrackId id, PluginId pluginId)
{
    if (auto* list = findPluginList (owner.getEdit(), id))
        for (auto* p : *list)
            if (p->itemID.getRawID() == pluginId)
                if (auto* external = dynamic_cast<te::ExternalPlugin*> (p))
                    if (! external->isMissing())
                        external->showWindowExplicitly();
}

PluginId EffectsController::addEffect (TrackId id, const juce::String& type, int position)
{
    auto* edit = owner.getEdit();
    auto* list = findPluginList (edit, id);

    if (list == nullptr || (int) effectsOn (*list).size() >= maxEffectsPerTrack)
        return 0;

    // At most one Amp per track.
    if (type == AmpPlugin::xmlTypeName && findAmp (*list) != nullptr)
        return 0;

    // A third-party plug-in can take a moment to load, so do it before the
    // undo step starts.
    auto plugin = createPlugin (*edit, type);

    if (plugin == nullptr)
        return 0;

    if (keepsPedalsFirst (edit, id))
        position = positionKeepingPedalsFirst (*plugin, effectsOn (*list), position);

    owner.beginUndoStep ("Add Effect");

    list->insertPlugin (plugin, pluginListIndexForPosition (*list, position), nullptr);
    return plugin->itemID.getRawID();
}

PluginId EffectsController::replaceEffect (TrackId id, PluginId pluginId, const juce::String& newType)
{
    auto* edit = owner.getEdit();
    auto* list = findPluginList (edit, id);
    auto* old = list != nullptr ? findEffect (*list, pluginId) : nullptr;

    if (old == nullptr)
        return 0;

    // At most one Amp per track (replacing the Amp with another is fine).
    if (newType == AmpPlugin::xmlTypeName && findAmp (*list) != nullptr && ! isAmp (*old))
        return 0;

    auto plugin = createPlugin (*edit, newType);

    if (plugin == nullptr)
        return 0;

    // Remove + insert in one undo step, at the same position (on an audio
    // track, a pedal replaced by another kind of effect moves after the
    // pedals, and the other way round).
    owner.beginUndoStep ("Replace Effect");

    if (keepsPedalsFirst (edit, id))
    {
        auto effects = effectsOn (*list);
        const auto oldPosition = (int) std::distance (effects.begin(), std::find (effects.begin(), effects.end(), old));
        effects.erase (effects.begin() + oldPosition);
        const auto position = positionKeepingPedalsFirst (*plugin, effects, oldPosition);
        old->removeFromParent();
        list->insertPlugin (plugin, pluginListIndexForPosition (*list, position), nullptr);
    }
    else
    {
        const auto index = list->indexOf (old);
        old->removeFromParent();
        list->insertPlugin (plugin, index, nullptr);
    }

    return plugin->itemID.getRawID();
}

void EffectsController::removeEffect (TrackId id, PluginId pluginId)
{
    if (auto* list = findPluginList (owner.getEdit(), id))
    {
        if (auto* p = findEffect (*list, pluginId))
        {
            owner.beginUndoStep ("Remove Effect");
            p->removeFromParent();
        }
    }
}

void EffectsController::moveEffect (TrackId id, PluginId pluginId, int newPosition)
{
    auto* list = findPluginList (owner.getEdit(), id);

    if (list == nullptr)
        return;

    // Keep a reference while it's briefly out of the list. (Plugin::Ptr is
    // ref-counted, so the plugin isn't deleted when it's removed.)
    te::Plugin::Ptr plugin = findEffect (*list, pluginId);

    if (plugin == nullptr)
        return;

    auto effects = effectsOn (*list);
    const auto oldPosition = (int) std::distance (effects.begin(), std::find (effects.begin(), effects.end(), plugin.get()));

    // newPosition is where it ends up: its index among the others.
    if (keepsPedalsFirst (owner.getEdit(), id))
    {
        auto others = effects;
        others.erase (others.begin() + oldPosition);
        newPosition = positionKeepingPedalsFirst (*plugin, others, newPosition);

        if (newPosition < 0)
            newPosition = (int) others.size();
    }

    if (oldPosition == newPosition)
        return;

    owner.beginUndoStep ("Move Effect");
    plugin->removeFromParent();
    list->insertPlugin (plugin, pluginListIndexForPosition (*list, newPosition), nullptr);
}

void EffectsController::setEffectEnabled (TrackId id, PluginId pluginId, bool enabled)
{
    if (auto* list = findPluginList (owner.getEdit(), id))
    {
        if (auto* p = findEffect (*list, pluginId))
        {
            owner.beginUndoStep (enabled ? "Turn Effect On" : "Turn Effect Off");
            p->setEnabled (enabled);
        }
    }
}

std::vector<ParameterInfo> EffectsController::getEffectParameters (TrackId id, PluginId pluginId) const
{
    std::vector<ParameterInfo> result;

    if (auto* list = findPluginList (owner.getEdit(), id))
    {
        if (auto* p = findEffect (*list, pluginId))
        {
            if (dynamic_cast<te::ExternalPlugin*> (p) == nullptr)
                for (auto* param : p->getAutomatableParameters())
                    result.push_back ({ param->getParameterName(),
                                        param->getCurrentNormalisedValue(),
                                        param->getCurrentValueAsStringWithLabel(),
                                        param->paramID });

            // Settings kept as properties come after the real parameters.
            for (auto* setting : storedSettingsFor (*p))
            {
                const auto value = (float) p->state.getProperty (setting->property, setting->defaultValue);
                result.push_back ({ setting->name,
                                    juce::jmap (value, setting->minimum, setting->maximum, 0.0f, 1.0f),
                                    describeStored (*setting, value),
                                    setting->property.toString() });
            }
        }
    }

    return result;
}

void EffectsController::setEffectParameter (TrackId id, PluginId pluginId, int index, float value)
{
    // No beginUndoStep() here: the UI starts one on knob drag start.
    if (auto* list = findPluginList (owner.getEdit(), id))
    {
        if (auto* p = findEffect (*list, pluginId))
        {
            auto params = p->getAutomatableParameters();
            value = juce::jlimit (0.0f, 1.0f, value);

            if (auto* param = params[index])
            {
                // The Amp's Voice brings the voice's other settings with it
                // (see AmpPlugin::setVoice).
                if (auto* amp = dynamic_cast<AmpPlugin*> (p); amp != nullptr && param->paramID == "voice")
                    amp->setVoice (juce::roundToInt (param->valueRange.convertFrom0to1 (value)));
                else
                    setEffectKnob (*param, value);
            }
            else
            {
                // A setting kept as a property (see storedSettings). Setting
                // it through the undo manager is all it takes: the effect
                // listens to its own state.
                auto stored = storedSettingsFor (*p);
                const auto storedIndex = index - params.size();

                if (juce::isPositiveAndBelow (storedIndex, (int) stored.size()))
                {
                    auto* setting = stored[(size_t) storedIndex];
                    const auto newValue = juce::jmap (value, setting->minimum, setting->maximum);

                    // A juce::var holding an int, for a setting the effect reads as one.
                    p->state.setProperty (setting->property,
                                          setting->wholeNumber ? juce::var (juce::roundToInt (newValue)) : juce::var (newValue),
                                          &owner.getEdit()->getUndoManager());
                }
            }
        }
    }
}

//==============================================================================
// The Amp
static AmpPlugin* ampOn (te::Edit* edit, TrackId id)
{
    auto* list = findPluginList (edit, id);
    return list != nullptr ? findAmp (*list) : nullptr;
}

EffectsController::AmpSettings EffectsController::getAmp (TrackId id) const
{
    AmpSettings result;
    auto* amp = ampOn (owner.getEdit(), id);

    if (amp == nullptr)
        return result;

    result.exists = true;
    result.pluginId = amp->itemID.getRawID();
    result.on = amp->isEnabled();
    result.voice = amp->getVoice();
    result.cab = amp->getCab();
    result.bright = amp->isBright();

    for (int i = 0; i < ampKnobCount; ++i)
    {
        auto& knob = amp->getKnob (i);
        result.knobs[(size_t) i] = knob.getCurrentNormalisedValue();
        result.knobText[(size_t) i] = knob.getCurrentValueAsString();
    }

    return result;
}

PluginId EffectsController::addAmp (TrackId id, int voice, bool newUndoStep)
{
    auto* edit = owner.getEdit();
    auto* list = findPluginList (edit, id);

    if (list == nullptr || findAmp (*list) != nullptr || (int) effectsOn (*list).size() >= maxEffectsPerTrack)
        return 0;

    if (voice < 0)
    {
        auto* track = findTrack (edit, id);
        voice = getDefaultAmpVoiceFor (track != nullptr ? track->state.getProperty (iconProp).toString() : juce::String());
    }

    // The voice's settings go into the state before the Amp is created, so
    // they aren't separate undo steps.
    auto plugin = edit->getPluginCache().createNewPlugin (AmpPlugin::createState (voice));

    if (plugin == nullptr)
        return 0;

    const auto position = keepsPedalsFirst (edit, id) ? positionKeepingPedalsFirst (*plugin, effectsOn (*list), -1) : -1;

    if (newUndoStep)
        owner.beginUndoStep ("Add Amp");

    list->insertPlugin (plugin, pluginListIndexForPosition (*list, position), nullptr);
    return plugin->itemID.getRawID();
}

void EffectsController::setAmpVoice (TrackId id, int voice)
{
    if (auto* amp = ampOn (owner.getEdit(), id))
    {
        owner.beginUndoStep ("Amp Voice");
        amp->setVoice (voice);
    }
}

void EffectsController::setAmpCab (TrackId id, int cab)
{
    if (auto* amp = ampOn (owner.getEdit(), id); amp != nullptr && amp->getCab() != cab)
    {
        owner.beginUndoStep ("Amp Cab");
        amp->setCab (cab);
    }
}

void EffectsController::setAmpBright (TrackId id, bool on)
{
    if (auto* amp = ampOn (owner.getEdit(), id); amp != nullptr && amp->isBright() != on)
    {
        owner.beginUndoStep (on ? "Amp Bright On" : "Amp Bright Off");
        amp->setBright (on);
    }
}

void EffectsController::setAmpKnob (TrackId id, int knob, float normalised0to1)
{
    // No beginUndoStep() here: the UI starts one on knob drag start.
    if (auto* amp = ampOn (owner.getEdit(), id); amp != nullptr && juce::isPositiveAndBelow (knob, ampKnobCount))
        setEffectKnob (amp->getKnob (knob), juce::jlimit (0.0f, 1.0f, normalised0to1));
}

//==============================================================================
// The track inspector's Noise Gate section
EffectsController::NoiseGateSettings EffectsController::getNoiseGate (TrackId id) const
{
    NoiseGateSettings result;
    auto* t = findTrack (owner.getEdit(), id);
    auto* gate = t != nullptr ? findNoiseGate (t->pluginList) : nullptr;

    if (gate == nullptr)
    {
        // What a new gate would start at.
        using Gate = NoiseGatePlugin;
        result.threshold = juce::jmap (Gate::defaultThresholdDb, Gate::minThresholdDb, Gate::maxThresholdDb, 0.0f, 1.0f);
        result.thresholdText = Gate::decibels (Gate::defaultThresholdDb);
        return result;
    }

    auto* param = gate->getThresholdParameter();
    result.exists = true;
    result.on = gate->isEnabled();
    result.pluginId = gate->itemID.getRawID();
    result.threshold = param->getCurrentNormalisedValue();
    result.thresholdText = param->getCurrentValueAsString();
    return result;
}

bool EffectsController::setNoiseGateOn (TrackId id, bool on)
{
    auto* t = findTrack (owner.getEdit(), id);

    if (t == nullptr)
        return false;

    if (auto* gate = findNoiseGate (t->pluginList))
    {
        // Turning it off keeps it (and its threshold) in the chain, bypassed.
        if (gate->isEnabled() != on)
        {
            owner.beginUndoStep (on ? "Turn Noise Gate On" : "Turn Noise Gate Off");
            gate->setEnabled (on);
        }

        return true;
    }

    if (! on)
        return true;

    // None yet: add one at the top of the chain, so it hears the input
    // before any other effect (a compressor would bring the noise up first).
    if ((int) effectsOn (t->pluginList).size() >= maxEffectsPerTrack)
        return false;

    auto plugin = createPlugin (t->edit, NoiseGatePlugin::xmlTypeName);

    if (plugin == nullptr)
        return false;

    owner.beginUndoStep ("Turn Noise Gate On");
    t->pluginList.insertPlugin (plugin, pluginListIndexForPosition (t->pluginList, 0), nullptr);
    return true;
}

void EffectsController::setNoiseGateThreshold (TrackId id, float normalised0to1)
{
    // No beginUndoStep() here: the UI starts one on drag start.
    if (auto* t = findTrack (owner.getEdit(), id))
        if (auto* gate = findNoiseGate (t->pluginList))
            setEffectKnob (*gate->getThresholdParameter(), juce::jlimit (0.0f, 1.0f, normalised0to1));
}

} // namespace ikaros
