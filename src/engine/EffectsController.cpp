#include "engine/EffectsController.h"
#include "engine/SoundControls.h"
#include "engine/AudioEngine.h"
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
            { te::EqualiserPlugin::xmlTypeName,  "EQ",                    "EQ & Filter" },
            { te::LowPassPlugin::xmlTypeName,    "Low/High-Pass Filter",  "EQ & Filter" },
            { te::ReverbPlugin::xmlTypeName,     "Reverb",                "Space" },
            { te::DelayPlugin::xmlTypeName,      "Echo",                  "Space" },
            { te::ChorusPlugin::xmlTypeName,     "Chorus",                "Modulation" },
            { te::PhaserPlugin::xmlTypeName,     "Phaser",                "Modulation" },
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

    // A few of Tracktion's built-in effects (Chorus, Phaser) keep their
    // settings as plain properties on their state rather than as
    // parameters, so they'd show no knobs. These are their settings, with
    // the ranges the knobs cover and the defaults Tracktion uses.
    struct StoredSetting
    {
        const char* pluginType;
        juce::Identifier property;
        const char* name;
        float minimum, maximum, defaultValue;
        const char* unit;   // "%" shows 0..1 as a percentage
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
        }
        else if (knob.control == "tune")
        {
            auto* transpose = createMissing ? findOrAddTranspose (t, *instrument)
                                            : t.pluginList.findFirstPluginOfType<TransposePlugin>();

            if (transpose != nullptr)
                c.parameter = transpose->getSemitonesParameter();

            c.toParameter = knobToSemitones;
            c.fromParameter = semitonesToKnob;
        }
        else
        {
            auto* shaper = createMissing ? findOrAddShaper (t, *instrument)
                                         : t.pluginList.findFirstPluginOfType<SoundShaperPlugin>();

            if (shaper != nullptr)
                c.parameter = shaper->getParameterFor (knob.control);
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
        t->setName (name);
        t->state.setProperty (autoNameProp, name, um);
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

    // A third-party plug-in can take a moment to load, so do it before the
    // undo step starts.
    auto plugin = createPlugin (*edit, type);

    if (plugin == nullptr)
        return 0;

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

    auto plugin = createPlugin (*edit, newType);

    if (plugin == nullptr)
        return 0;

    // Remove + insert in one undo step, at the same position.
    owner.beginUndoStep ("Replace Effect");
    const auto index = list->indexOf (old);
    old->removeFromParent();
    list->insertPlugin (plugin, index, nullptr);
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
                                        param->getCurrentValueAsStringWithLabel() });

            // Settings kept as properties come after the real parameters.
            for (auto* setting : storedSettingsFor (*p))
            {
                const auto value = (float) p->state.getProperty (setting->property, setting->defaultValue);
                result.push_back ({ setting->name,
                                    juce::jmap (value, setting->minimum, setting->maximum, 0.0f, 1.0f),
                                    describeStored (*setting, value) });
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
                setParameterUndoably (*p, *param, param->paramID, param->valueRange.convertFrom0to1 (value));
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
                    p->state.setProperty (setting->property, juce::jmap (value, setting->minimum, setting->maximum),
                                          &owner.getEdit()->getUndoManager());
                }
            }
        }
    }
}

} // namespace ikaros
