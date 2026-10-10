#include "engine/TrackController.h"
#include "engine/AudioEngine.h"

#include <tracktion_engine/tracktion_engine.h>
#include <array>
#include <map>

namespace te = tracktion;

namespace ikaros
{

namespace
{
    // Extra properties we store on each track's ValueTree. Tracktion saves
    // unknown properties with the Edit, so these round-trip through
    // .tracktionedit files for free.
    const juce::Identifier soundNameProp ("ikarosSound");
    const juce::Identifier categoryProp  ("ikarosCategory");

    // Track colours from the design tokens: blue, orange, green, purple, yellow.
    // They live here (not in the UI tokens) because a track's colour is part of
    // the saved session.
    const std::array<juce::uint32, 5> trackPalette { 0xff5aa9ff, 0xffff8f5a, 0xff4fd18b, 0xffc78bff, 0xffffd25a };

    te::AudioTrack* asAudioTrack (te::Track* t)   { return dynamic_cast<te::AudioTrack*> (t); }

    te::FourOscPlugin* findSynth (te::AudioTrack& t)
    {
        // findFirstPluginOfType is a template: it walks the track's plugin list
        // and dynamic_casts each one to the requested type.
        return t.pluginList.findFirstPluginOfType<te::FourOscPlugin>();
    }

    te::CompressorPlugin* findCompressor (te::AudioTrack& t)
    {
        return t.pluginList.findFirstPluginOfType<te::CompressorPlugin>();
    }

    // Sets a plugin parameter so that Undo can restore it.
    //
    // Parameters are backed by a property on the plugin's state, which
    // Tracktion writes through the Edit's UndoManager. A parameter still at its
    // default often has no stored property yet, so undoing the first change
    // would delete the property rather than restore a value. Storing the
    // current value first (outside the undo history) keeps the saved session
    // explicit. AudioEngine::undo() then pushes restored values back into the
    // live parameters.
    void setParameterUndoably (te::Plugin& plugin, te::AutomatableParameter& param,
                               const juce::Identifier& stateProperty, float value)
    {
        if (! plugin.state.hasProperty (stateProperty))
            plugin.state.setProperty (stateProperty, param.getCurrentValue(), nullptr);

        param.setParameter (value, juce::sendNotification);
    }

    juce::String percent (float v)    { return juce::String (juce::roundToInt (v * 100.0f)) + "%"; }

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
}

//==============================================================================
// Holds a LevelMeasurer::Client registered with a track's level-meter plugin.
// The Client is filled in by the audio thread; we read-and-clear it from the UI.
struct MeterTap
{
    explicit MeterTap (te::LevelMeterPlugin& p) : plugin (&p)
    {
        p.measurer.addClient (client);
    }

    ~MeterTap()
    {
        if (auto* meter = dynamic_cast<te::LevelMeterPlugin*> (plugin.get()))
            meter->measurer.removeClient (client);
    }

    te::Plugin::Ptr plugin;          // ref-counted pointer keeps the plugin alive while we're attached
    te::LevelMeasurer::Client client;
};

struct TrackController::Impl
{
    std::map<TrackId, std::unique_ptr<MeterTap>> meters;

    // Identifies our on-screen keyboard as a MIDI source to Tracktion.
    te::MPESourceID keyboardSource = te::createUniqueMPESourceID();

    // Which track each held note went to, so a note-off always reaches the
    // same synth even if the selection changed while the key was down.
    std::array<TrackId, 128> heldNotes {};
};

//==============================================================================
TrackController::TrackController (AudioEngine& o)
    : impl (std::make_unique<Impl>()), owner (o)
{
}

TrackController::~TrackController() = default;

static te::AudioTrack* findTrack (te::Edit* edit, TrackId id)
{
    if (edit == nullptr || id == 0)
        return nullptr;

    return asAudioTrack (te::findTrackForID (*edit, te::EditItemID::fromRawID (id)));
}

void TrackController::sessionChanged()
{
    impl->meters.clear();
    impl->heldNotes.fill (0);
    selectedTrack = 0;
}

//==============================================================================
std::vector<TrackInfo> TrackController::getTracks() const
{
    std::vector<TrackInfo> result;
    auto* edit = owner.getEdit();

    if (edit == nullptr)
        return result;

    auto tracks = te::getAudioTracks (*edit);
    bool anySolo = false;

    for (auto* t : tracks)
        anySolo = anySolo || t->isSolo (false);

    int index = 0;

    for (auto* t : tracks)
    {
        TrackInfo info;
        info.id = t->itemID.getRawID();
        info.index = index++;
        info.name = t->getName();
        info.soundName = t->state.getProperty (soundNameProp, "Basic Synth").toString();
        info.category = t->state.getProperty (categoryProp, "Synths").toString();
        info.colour = t->getColour();
        info.muted = t->isMuted (false);
        info.solo = t->isSolo (false);
        info.silencedBySolo = anySolo && ! info.solo;

        if (auto* vol = t->getVolumePlugin())
        {
            info.volume = vol->getSliderPos();
            info.volumeDb = vol->getVolumeDb();
        }

        result.push_back (info);
    }

    return result;
}

std::optional<TrackInfo> TrackController::getTrack (TrackId id) const
{
    for (auto& t : getTracks())
        if (t.id == id)
            return t;

    return std::nullopt;
}

TrackId TrackController::addInstrumentTrack()
{
    auto* edit = owner.getEdit();
    owner.beginUndoStep ("Add Track");

    auto existing = te::getAudioTracks (*edit);
    te::Track* last = existing.isEmpty() ? nullptr : existing.getLast();

    // TrackInsertPoint (parent, preceding): nullptr parent = top level of the Edit.
    auto track = edit->insertNewAudioTrack (te::TrackInsertPoint (nullptr, last), nullptr);

    if (track == nullptr)
        return 0;

    auto& um = edit->getUndoManager();
    const int number = existing.size() + 1;
    track->setName ("Instrument " + juce::String (number));
    track->setColour (juce::Colour (trackPalette[(size_t) (number - 1) % trackPalette.size()]));
    track->state.setProperty (soundNameProp, "Basic Synth", &um);
    track->state.setProperty (categoryProp, "Synths", &um);

    // Built-in 4OSC synth as the instrument. createNewPlugin returns a
    // ref-counted Plugin::Ptr; dynamic_cast gives us the concrete type.
    if (auto synthPtr = edit->getPluginCache().createNewPlugin (te::FourOscPlugin::xmlTypeName, {}))
    {
        if (auto* synth = dynamic_cast<te::FourOscPlugin*> (synthPtr.get()))
        {
            // A friendlier starting sound than the raw init patch: low-pass
            // filter on (so "Brightness" does something) and a little room.
            synth->filterTypeValue = 1;
            synth->filterFreq->setNormalisedParameter (0.75f, juce::sendNotification);
            synth->ampRelease->setParameter (0.3f, juce::sendNotification);
            synth->reverbOnValue = true;
            synth->reverbMix->setParameter (0.2f, juce::sendNotification);
        }

        track->pluginList.insertPlugin (synthPtr, 0, nullptr);
    }

    // A compressor after the synth, switched off until the user turns it on
    // from the Effects list.
    if (auto comp = edit->getPluginCache().createNewPlugin (te::CompressorPlugin::xmlTypeName, {}))
    {
        track->pluginList.insertPlugin (comp, 1, nullptr);
        comp->setEnabled (false);
    }

    return track->itemID.getRawID();
}

void TrackController::deleteTrack (TrackId id)
{
    if (auto* track = findTrack (owner.getEdit(), id))
    {
        owner.beginUndoStep ("Delete Track");
        impl->meters.erase (id);
        owner.getEdit()->deleteTrack (track);

        if (selectedTrack == id)
        {
            auto remaining = getTracks();
            setSelectedTrack (remaining.empty() ? 0 : remaining.front().id);
        }
    }
}

void TrackController::setSelectedTrack (TrackId id)
{
    selectedTrack = findTrack (owner.getEdit(), id) != nullptr ? id : 0;
    routeMidiInputsToSelectedTrack();
}

void TrackController::routeMidiInputsToSelectedTrack()
{
    auto* edit = owner.getEdit();

    if (edit == nullptr)
        return;

    auto& dm = owner.getTeEngine().getDeviceManager();

    // Enable every hardware MIDI input and make it audible without arming
    // (MonitorMode::on). Recording is a later milestone.
    for (auto& midiIn : dm.getMidiInDevices())
    {
        midiIn->setEnabled (true);
        midiIn->setMonitorMode (te::InputDevice::MonitorMode::on);
    }

    // The playback context creates an InputDeviceInstance per enabled device.
    edit->getTransport().ensureContextAllocated();

    auto* track = findTrack (edit, selectedTrack);

    for (auto* instance : edit->getAllInputDevices())
    {
        if (instance->getInputDevice().getDeviceType() != te::InputDevice::physicalMidiDevice)
            continue;

        for (auto target : instance->getTargets())
            if (track == nullptr || target != track->itemID)
                (void) instance->removeTarget (target, nullptr);   // (void) = deliberately ignore the [[nodiscard]] result

        if (track != nullptr)
            (void) instance->setTarget (track->itemID, true, nullptr);
    }
}

void TrackController::setMute (TrackId id, bool mute)
{
    if (auto* t = findTrack (owner.getEdit(), id))
    {
        owner.beginUndoStep ("Mute");
        t->setMute (mute);
    }
}

void TrackController::setSolo (TrackId id, bool solo)
{
    if (auto* t = findTrack (owner.getEdit(), id))
    {
        owner.beginUndoStep ("Solo");
        t->setSolo (solo);
    }
}

void TrackController::setVolume (TrackId id, float pos)
{
    // No beginUndoStep() here: the UI calls AudioEngine::beginUndoStep() on
    // drag start, so a whole fader move is one undo step.
    if (auto* t = findTrack (owner.getEdit(), id))
        if (auto* vol = t->getVolumePlugin())
            setParameterUndoably (*vol, *vol->volParam, te::IDs::volume, juce::jlimit (0.0f, 1.0f, pos));
}

void TrackController::setSound (TrackId id, const juce::String& category, const juce::String& presetName)
{
    if (auto* t = findTrack (owner.getEdit(), id))
    {
        owner.beginUndoStep ("Change Sound");
        auto* um = &owner.getEdit()->getUndoManager();
        t->state.setProperty (soundNameProp, presetName, um);
        t->state.setProperty (categoryProp, category, um);
        t->setName (presetName);
        // TODO(milestone-3): swap the instrument (sampler / plugin preset) here.
    }
}

StereoLevel TrackController::getTrackLevel (TrackId id)
{
    auto* t = findTrack (owner.getEdit(), id);

    if (t == nullptr)
        return {};

    auto& tap = impl->meters[id];

    if (tap == nullptr)
    {
        if (auto* meter = t->getLevelMeterPlugin())
            tap = std::make_unique<MeterTap> (*meter);
        else
            return {};
    }

    return { tap->client.getAndClearAudioLevel (0).dB,
             tap->client.getAndClearAudioLevel (1).dB };
}

//==============================================================================
static RegionInfo makeRegionInfo (te::Clip& clip)
{
    RegionInfo r;
    r.id = clip.itemID.getRawID();
    r.name = clip.getName();
    r.startBeat = clip.getStartBeat().inBeats();
    r.lengthBeats = clip.getLengthInBeats().inBeats();

    if (auto* midi = dynamic_cast<te::MidiClip*> (&clip))
    {
        r.isMidi = true;

        // Note positions are relative to the clip's *content* start, which can
        // differ from the clip start if the clip was trimmed from the left.
        auto contentOffset = (clip.getContentStartBeat() - clip.getStartBeat()).inBeats();

        for (auto* n : midi->getSequence().getNotes())
            r.notes.push_back ({ n->getNoteNumber(),
                                 n->getStartBeat().inBeats() + contentOffset,
                                 n->getLengthBeats().inBeats(),
                                 n->getVelocity() });
    }
    else
    {
        r.isMidi = false;
        // TODO(milestone-2): audio regions — expose a waveform thumbnail.
    }

    return r;
}

std::vector<RegionInfo> TrackController::getRegions (TrackId id) const
{
    std::vector<RegionInfo> result;

    if (auto* t = findTrack (owner.getEdit(), id))
        for (auto* clip : t->getClips())
            result.push_back (makeRegionInfo (*clip));

    return result;
}

std::optional<RegionInfo> TrackController::getRegion (RegionId id) const
{
    if (auto* edit = owner.getEdit())
        if (auto* clip = te::findClipForID (*edit, te::EditItemID::fromRawID (id)))
            return makeRegionInfo (*clip);

    return std::nullopt;
}

RegionId TrackController::createChordRegion (TrackId id, double startBeat)
{
    auto* edit = owner.getEdit();
    auto* track = findTrack (edit, id);

    if (track == nullptr)
        return 0;

    const double bpb = owner.beatsPerBar();
    const double start = std::floor (juce::jmax (0.0, startBeat) / bpb) * bpb;
    double length = bpb * 4.0;

    // Don't stack regions on top of each other: shorten the new one to end at
    // the next region, and give up if there's less than a bar of space.
    for (auto* clip : track->getClips())
    {
        auto cs = clip->getStartBeat().inBeats();
        auto ce = cs + clip->getLengthInBeats().inBeats();

        if (start >= cs && start < ce)
            return 0;

        if (cs > start)
            length = juce::jmin (length, cs - start);
    }

    if (length < bpb)
        return 0;

    owner.beginUndoStep ("Add Region");

    auto& ts = edit->tempoSequence;
    te::TimeRange range (ts.toTime (te::BeatPosition::fromBeats (start)),
                         ts.toTime (te::BeatPosition::fromBeats (start + length)));

    auto* clip = dynamic_cast<te::MidiClip*> (track->insertNewClip (te::TrackItem::Type::midi, "Chords", range, nullptr));

    if (clip == nullptr)
        return 0;

    // I – V – vi – IV in C major: one chord per bar, played on beats 1 and 3,
    // with the root held in the bass.
    struct Chord { int bass; std::array<int, 3> notes; };
    const std::array<Chord, 4> progression {{
        { 48, { 60, 64, 67 } },   // C
        { 43, { 59, 62, 67 } },   // G
        { 45, { 60, 64, 69 } },   // Am
        { 41, { 60, 65, 69 } },   // F
    }};

    auto& seq = clip->getSequence();
    auto* um = &edit->getUndoManager();
    const int numBars = (int) std::round (length / bpb);

    for (int bar = 0; bar < numBars; ++bar)
    {
        auto& chord = progression[(size_t) bar % progression.size()];
        const double barStart = bar * bpb;
        const double half = bpb / 2.0;

        seq.addNote (chord.bass, te::BeatPosition::fromBeats (barStart),
                     te::BeatDuration::fromBeats (bpb - 0.1), 90, 0, um);

        for (double hit : { 0.0, half })
            for (int n : chord.notes)
                seq.addNote (n, te::BeatPosition::fromBeats (barStart + hit),
                             te::BeatDuration::fromBeats (half - 0.15), hit == 0.0 ? 92 : 76, 0, um);
    }

    return clip->itemID.getRawID();
}

void TrackController::deleteRegion (RegionId id)
{
    if (auto* edit = owner.getEdit())
    {
        if (auto* clip = te::findClipForID (*edit, te::EditItemID::fromRawID (id)))
        {
            owner.beginUndoStep ("Delete Region");
            clip->removeFromParent();
        }
    }
}

//==============================================================================
std::vector<SoundControl> TrackController::getSoundControls (TrackId id) const
{
    std::vector<SoundControl> result;
    auto* t = findTrack (owner.getEdit(), id);
    auto* synth = t != nullptr ? findSynth (*t) : nullptr;

    for (auto& k : knobMappings)
    {
        SoundControl c;
        c.name = k.label;

        if (synth != nullptr)
        {
            if (auto param = synth->getAutomatableParameterByID (k.paramID))
            {
                c.value = param->getCurrentNormalisedValue();
                auto raw = param->getCurrentValue();
                juce::String id (k.paramID);

                if (id == "ampAttack" || id == "ampRelease")  c.valueText = seconds (raw);
                else if (id == "filterResonance")             c.valueText = juce::String (juce::roundToInt (raw)) + "%";
                else                                          c.valueText = percent (c.value);
            }
        }
        else
        {
            c.valueText = juce::String::fromUTF8 ("\xe2\x80\x94");   // em dash
        }

        result.push_back (c);
    }

    return result;
}

void TrackController::setSoundControl (TrackId id, int index, float value)
{
    if (! juce::isPositiveAndBelow (index, (int) knobMappings.size()))
        return;

    if (auto* t = findTrack (owner.getEdit(), id))
        if (auto* synth = findSynth (*t))
        {
            if (auto param = synth->getAutomatableParameterByID (knobMappings[(size_t) index].paramID))
            {
                // valueRange maps 0..1 onto the parameter's own (possibly skewed) range.
                auto raw = param->valueRange.convertFrom0to1 (juce::jlimit (0.0f, 1.0f, value));
                setParameterUndoably (*synth, *param, knobMappings[(size_t) index].paramID, raw);
            }
        }
}

std::vector<EffectInfo> TrackController::getEffects (TrackId id) const
{
    auto* t = findTrack (owner.getEdit(), id);
    auto* synth = t != nullptr ? findSynth (*t) : nullptr;
    auto* comp = t != nullptr ? findCompressor (*t) : nullptr;

    return {
        { "Reverb",     synth != nullptr && synth->reverbOnValue.get() },
        { "Compressor", comp  != nullptr && comp->isEnabled() },
        { "Echo",       synth != nullptr && synth->delayOnValue.get() },
    };
}

void TrackController::setEffectEnabled (TrackId id, int index, bool enabled)
{
    auto* t = findTrack (owner.getEdit(), id);

    if (t == nullptr)
        return;

    owner.beginUndoStep ("Effect");

    // TODO(milestone-3): real per-track effect slots instead of 4OSC's built-ins.
    if (index == 1)
    {
        if (auto* comp = findCompressor (*t))
            comp->setEnabled (enabled);
    }
    else if (auto* synth = findSynth (*t))
    {
        if (index == 0)  synth->reverbOnValue = enabled;
        if (index == 2)  synth->delayOnValue = enabled;
    }
}

TrackDetails TrackController::getDetails (TrackId id) const
{
    TrackDetails d;
    auto* edit = owner.getEdit();
    auto* t = findTrack (edit, id);

    if (t == nullptr)
        return d;

    d.pluginName = findSynth (*t) != nullptr ? juce::String ("4OSC (built-in synth)") : juce::String ("None");
    d.outputName = t->getOutput().getOutputName();

    for (auto* instance : edit->getAllInputDevices())
        if (instance->getInputDevice().getDeviceType() == te::InputDevice::physicalMidiDevice)
            if (instance->getTargets().contains (t->itemID))
                d.midiInputs.add (instance->getInputDevice().getName());

    return d;
}

//==============================================================================
void TrackController::noteOn (int note, float velocity)
{
    if (! juce::isPositiveAndBelow (note, 128))
        return;

    if (auto* t = findTrack (owner.getEdit(), selectedTrack))
    {
        impl->heldNotes[(size_t) note] = selectedTrack;
        t->injectLiveMidiMessage (juce::MidiMessage::noteOn (1, note, velocity), impl->keyboardSource);
    }
}

void TrackController::noteOff (int note)
{
    if (! juce::isPositiveAndBelow (note, 128))
        return;

    auto id = std::exchange (impl->heldNotes[(size_t) note], TrackId {});

    if (auto* t = findTrack (owner.getEdit(), id))
        t->injectLiveMidiMessage (juce::MidiMessage::noteOff (1, note), impl->keyboardSource);
}

void TrackController::allNotesOff()
{
    for (int n = 0; n < 128; ++n)
        if (impl->heldNotes[(size_t) n] != 0)
            noteOff (n);
}

} // namespace ikaros
