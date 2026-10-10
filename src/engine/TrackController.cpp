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
    const juce::Identifier kindProp      ("ikarosKind");      // "audio" for audio tracks
    const juce::Identifier inputProp     ("ikarosInput");     // audio input device ID
    const juce::Identifier monitorProp   ("ikarosMonitor");   // hear the input live

    bool isAudioTrack (const te::AudioTrack& t)   { return t.state.getProperty (kindProp).toString() == "audio"; }

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

        return p.getName();
    }

    // Effects are every plugin on the track except the instrument and the
    // track's own volume/meter plugins (which Tracktion keeps at the end).
    bool isEffect (te::Plugin& p)
    {
        return ! p.isSynth()
            && dynamic_cast<te::VolumeAndPanPlugin*> (&p) == nullptr
            && dynamic_cast<te::LevelMeterPlugin*> (&p) == nullptr;
    }

    std::vector<te::Plugin*> effectsOn (te::AudioTrack& t)
    {
        std::vector<te::Plugin*> result;

        for (auto* p : t.pluginList)
            if (isEffect (*p))
                result.push_back (p);

        return result;
    }

    te::Plugin* findEffect (te::AudioTrack& t, PluginId id)
    {
        for (auto* p : effectsOn (t))
            if (p->itemID.getRawID() == id)
                return p;

        return nullptr;
    }

    PluginSlot makeSlot (te::Plugin& p)
    {
        return { p.itemID.getRawID(), p.getPluginType(), displayNameFor (p), p.isEnabled() };
    }

    // Where in the track's plugin list effect number `position` should go:
    // before the existing effect at that position, or (for "the end") just
    // before the volume plugin.
    int pluginListIndexForPosition (te::AudioTrack& t, int position)
    {
        auto effects = effectsOn (t);

        if (juce::isPositiveAndBelow (position, (int) effects.size()))
            return t.pluginList.indexOf (effects[(size_t) position]);

        if (! effects.empty())
            return t.pluginList.indexOf (effects.back()) + 1;

        if (auto* vol = t.getVolumePlugin())
            return t.pluginList.indexOf (vol);

        return t.pluginList.size();
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

    // The on-screen keyboard plays through a Tracktion "virtual" MIDI input,
    // so its notes go through the same path as a hardware keyboard and can be
    // recorded. Created once and remembered in Tracktion's settings.
    std::shared_ptr<te::MidiInputDevice> keyboardDevice;
    te::MPESourceID keyboardSource = te::createUniqueMPESourceID();

    std::array<bool, 128> heldNotes {};   // keys currently down on the on-screen keyboard

    // Input meter for the Recording section. WeakReference: input devices
    // can be rebuilt when the audio device changes.
    juce::WeakReference<te::LevelMeasurer> inputMeasurer;
    te::LevelMeasurer::Client inputClient;

    // Waveform peaks per recorded file and clip range, so audio files are
    // read once, not on every refresh.
    std::map<juce::String, std::vector<float>> peakCache;

    void detachInputMeter()
    {
        if (auto* m = inputMeasurer.get())
            m->removeClient (inputClient);

        inputMeasurer = nullptr;
    }

    ~Impl()   { detachInputMeter(); }
};

namespace
{
    const juce::String keyboardDeviceName ("Ikaros On-Screen Keyboard");

    // The MIDI inputs we route to tracks: hardware keyboards and our own
    // on-screen keyboard. (Tracktion's "All MIDI Ins" merges the hardware
    // ones, so routing it too would double every note.)
    bool isRoutedMidiInput (te::InputDevice& d)
    {
        return d.getDeviceType() == te::InputDevice::physicalMidiDevice
            || (d.getDeviceType() == te::InputDevice::virtualMidiDevice && d.getName() == keyboardDeviceName);
    }
}

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
    impl->heldNotes.fill (false);
    selectedTrack = 0;
    armedTrack = 0;
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
        info.kind = isAudioTrack (*t) ? TrackKind::audio : TrackKind::instrument;
        info.soundName = t->state.getProperty (soundNameProp, "Basic Synth").toString();
        info.category = t->state.getProperty (categoryProp, "Synths").toString();

        if (info.kind == TrackKind::audio)
        {
            // Show the input it records from, e.g. "Input 1".
            info.soundName = "No input";
            auto inputId = getRecordingSettings (info.id).inputId;

            for (auto& in : getAudioInputs())
                if (in.id == inputId)
                    info.soundName = in.name;
        }
        info.colour = t->getColour();
        info.muted = t->isMuted (false);
        info.solo = t->isSolo (false);
        info.silencedBySolo = anySolo && ! info.solo;
        info.armed = info.id == armedTrack;

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

    // No effects by default; the user adds them from the Plug-ins inspector.
    return track->itemID.getRawID();
}

TrackId TrackController::addAudioTrack()
{
    auto* edit = owner.getEdit();
    owner.beginUndoStep ("Add Audio Track");

    auto existing = te::getAudioTracks (*edit);
    te::Track* last = existing.isEmpty() ? nullptr : existing.getLast();
    auto track = edit->insertNewAudioTrack (te::TrackInsertPoint (nullptr, last), nullptr);

    if (track == nullptr)
        return 0;

    auto& um = edit->getUndoManager();
    int audioCount = 1;

    for (auto* t : existing)
        if (isAudioTrack (*t))
            ++audioCount;

    track->setName ("Audio " + juce::String (audioCount));
    track->setColour (juce::Colour (trackPalette[(size_t) existing.size() % trackPalette.size()]));
    track->state.setProperty (kindProp, "audio", &um);
    track->state.setProperty (categoryProp, "Audio Files", &um);

    // Record from the first input by default, without monitoring (a
    // microphone near the speakers would otherwise feed back).
    auto inputs = getAudioInputs();
    track->state.setProperty (inputProp, inputs.empty() ? juce::String() : inputs.front().id, &um);
    track->state.setProperty (monitorProp, false, &um);

    return track->itemID.getRawID();
}

void TrackController::deleteTrack (TrackId id)
{
    if (auto* track = findTrack (owner.getEdit(), id))
    {
        owner.beginUndoStep ("Delete Track");
        impl->meters.erase (id);

        if (armedTrack == id)
            armedTrack = 0;

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
    const auto newSelection = findTrack (owner.getEdit(), id) != nullptr ? id : TrackId {};

    // With nothing armed, the input follows the selection: release held keys
    // first so the previous track's synth doesn't keep sounding.
    if (armedTrack == 0 && newSelection != selectedTrack)
        allNotesOff();

    selectedTrack = newSelection;
    routeMidiInputs();
}

void TrackController::routeMidiInputs()
{
    auto* edit = owner.getEdit();

    if (edit == nullptr)
        return;

    auto& dm = owner.getTeEngine().getDeviceManager();

    // Our on-screen keyboard device: create it the first time the app runs.
    if (impl->keyboardDevice == nullptr)
    {
        auto findKeyboard = [&dm]
        {
            for (auto& d : dm.getMidiInDevices())
                if (d->getDeviceType() == te::InputDevice::virtualMidiDevice && d->getName() == keyboardDeviceName)
                    return d;

            return std::shared_ptr<te::MidiInputDevice>();
        };

        impl->keyboardDevice = findKeyboard();

        if (impl->keyboardDevice == nullptr && dm.createVirtualMidiDevice (keyboardDeviceName).wasOk())
            impl->keyboardDevice = findKeyboard();
    }

    // Enable every MIDI input and make it audible without arming
    // (MonitorMode::on), so playing always makes sound.
    for (auto& midiIn : dm.getMidiInDevices())
    {
        midiIn->setEnabled (true);
        midiIn->setMonitorMode (te::InputDevice::MonitorMode::on);
    }

    // The playback context creates an InputDeviceInstance per enabled device.
    // If a device was added after the context was built, rebuild it.
    edit->getTransport().ensureContextAllocated();

    auto hasInstanceFor = [edit] (te::InputDevice* device)
    {
        for (auto* instance : edit->getAllInputDevices())
            if (&instance->getInputDevice() == device)
                return true;

        return false;
    };

    if (impl->keyboardDevice != nullptr && ! hasInstanceFor (impl->keyboardDevice.get()))
        edit->getTransport().ensureContextAllocated (true);

    // MIDI goes to an instrument track: the armed one (so it records),
    // otherwise the selected one (so you can just play).
    auto* armed = findTrack (edit, armedTrack);
    auto* selected = findTrack (edit, selectedTrack);
    te::AudioTrack* track = nullptr;

    if (armed != nullptr && ! isAudioTrack (*armed))
        track = armed;
    else if (selected != nullptr && ! isAudioTrack (*selected))
        track = selected;

    for (auto* instance : edit->getAllInputDevices())
    {
        if (! isRoutedMidiInput (instance->getInputDevice()))
            continue;

        for (auto target : instance->getTargets())
            if (track == nullptr || target != track->itemID)
                (void) instance->removeTarget (target, nullptr);   // (void) = deliberately ignore the [[nodiscard]] result

        if (track != nullptr)
        {
            (void) instance->setTarget (track->itemID, true, nullptr);
            instance->setRecordingEnabled (track->itemID, track->itemID.getRawID() == armedTrack);
        }
    }

    routeAudioInputs();
}

void TrackController::routeAudioInputs()
{
    auto* edit = owner.getEdit();
    auto& dm = owner.getTeEngine().getDeviceManager();

    // Microphones and guitars are mono, so offer each input channel on its
    // own ("Input 1", "Input 2") rather than Tracktion's default stereo pairs.
    // TODO: offer stereo pairs for stereo sources (keyboards, mixers).
    bool devicesChanged = false;

    for (auto* in : dm.getWaveInputDevices())
    {
        if (in->getChannels().getNumChannels() != 1)
        {
            dm.setAllWaveInputsToNumChannels (1);
            devicesChanged = true;
            break;
        }
    }

    // Tracktion makes an input for every channel the driver advertises (with
    // PipeWire that can be dozens), but only the channels the audio device
    // actually opened carry audio. Switch the others off.
    int numOpenInputs = 0;

    if (auto* device = dm.deviceManager.getCurrentAudioDevice())
        numOpenInputs = device->getActiveInputChannels().countNumberOfSetBits();

    for (auto* in : dm.getWaveInputDevices())
    {
        if (in->getDeviceType() != te::InputDevice::waveDevice)
            continue;

        bool allChannelsOpen = true;

        for (auto& ci : in->getChannels())
            allChannelsOpen = allChannelsOpen && ci.indexInDevice < numOpenInputs;

        if (in->isEnabled() != allChannelsOpen)
        {
            in->setEnabled (allChannelsOpen);
            devicesChanged = true;
        }
    }

    if (devicesChanged)
        edit->getTransport().ensureContextAllocated (true);

    // Audio goes to an audio track: the armed one, otherwise the selected one.
    auto* armed = findTrack (edit, armedTrack);
    auto* selected = findTrack (edit, selectedTrack);
    te::AudioTrack* track = nullptr;

    if (armed != nullptr && isAudioTrack (*armed))
        track = armed;
    else if (selected != nullptr && isAudioTrack (*selected))
        track = selected;

    const auto settings = track != nullptr ? getRecordingSettings (track->itemID.getRawID()) : RecordingSettings {};

    for (auto* instance : edit->getAllInputDevices())
    {
        auto& device = instance->getInputDevice();

        if (device.getDeviceType() != te::InputDevice::waveDevice)
            continue;

        const bool isTrackInput = track != nullptr && device.getDeviceID() == settings.inputId;

        for (auto target : instance->getTargets())
            if (! isTrackInput || target != track->itemID)
                (void) instance->removeTarget (target, nullptr);

        if (isTrackInput)
        {
            (void) instance->setTarget (track->itemID, true, nullptr);
            instance->setRecordingEnabled (track->itemID, track->itemID.getRawID() == armedTrack);
            device.setMonitorMode (settings.monitoring ? te::InputDevice::MonitorMode::on
                                                       : te::InputDevice::MonitorMode::off);
        }
    }
}

//==============================================================================
// Recording
void TrackController::setArmed (TrackId id, bool shouldArm)
{
    // Only one armed track at a time keeps it simple: it's the one that records.
    const auto newArmed = shouldArm && findTrack (owner.getEdit(), id) != nullptr ? id : TrackId {};

    if (! shouldArm && armedTrack != id)
        return;

    if (newArmed != armedTrack)
    {
        allNotesOff();
        armedTrack = newArmed;
        routeMidiInputs();
        owner.notifyModelChanged();
    }
}

TrackId TrackController::armForRecording()
{
    if (armedTrack != 0)
        return 0;

    setArmed (selectedTrack, true);
    return armedTrack;
}

//==============================================================================
// Audio inputs
std::vector<AudioInput> TrackController::getAudioInputs() const
{
    std::vector<AudioInput> result;
    auto& dm = owner.getTeEngine().getDeviceManager();

    // Inputs for channels the audio device has open (routeAudioInputs()
    // switches the rest off).
    int index = 0;

    for (auto* in : dm.getWaveInputDevices())
        if (in->getDeviceType() == te::InputDevice::waveDevice && in->isEnabled())
            result.push_back ({ in->getDeviceID(), "Input " + juce::String (++index) });

    return result;
}

RecordingSettings TrackController::getRecordingSettings (TrackId id) const
{
    RecordingSettings r;

    if (auto* t = findTrack (owner.getEdit(), id))
    {
        r.inputId = t->state.getProperty (inputProp).toString();
        r.monitoring = (bool) t->state.getProperty (monitorProp, false);
    }

    return r;
}

void TrackController::setTrackInput (TrackId id, const juce::String& inputId)
{
    if (auto* t = findTrack (owner.getEdit(), id))
    {
        owner.beginUndoStep ("Change Input");
        t->state.setProperty (inputProp, inputId, &owner.getEdit()->getUndoManager());
        routeMidiInputs();
    }
}

void TrackController::setMonitoring (TrackId id, bool shouldMonitor)
{
    if (auto* t = findTrack (owner.getEdit(), id))
    {
        owner.beginUndoStep (shouldMonitor ? "Monitoring On" : "Monitoring Off");
        t->state.setProperty (monitorProp, shouldMonitor, &owner.getEdit()->getUndoManager());
        routeMidiInputs();
    }
}

StereoLevel TrackController::getInputLevel (TrackId id)
{
    auto inputId = getRecordingSettings (id).inputId;
    te::InputDevice* device = nullptr;

    for (auto* in : owner.getTeEngine().getDeviceManager().getWaveInputDevices())
        if (in->getDeviceID() == inputId)
            device = in;

    auto* measurer = device != nullptr ? &device->levelMeasurer : nullptr;

    if (measurer != impl->inputMeasurer.get())
    {
        impl->detachInputMeter();

        if (measurer != nullptr)
        {
            measurer->addClient (impl->inputClient);
            impl->inputMeasurer = measurer;
        }
    }

    if (impl->inputMeasurer == nullptr)
        return {};

    // Mono inputs report one channel; show it on both meter bars.
    auto left = impl->inputClient.getAndClearAudioLevel (0).dB;
    auto right = impl->inputClient.getNumChannelsUsed() > 1 ? impl->inputClient.getAndClearAudioLevel (1).dB : left;
    return { left, right };
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
// Waveform overview for an audio clip: the loudest sample in each of
// `numColumns` slices of the part of the file the clip plays.
static std::vector<float> computePeaks (te::WaveAudioClip& clip, int numColumns,
                                       std::map<juce::String, std::vector<float>>& cache)
{
    auto file = clip.getAudioFile().getFile();
    auto pos = clip.getPosition();
    auto key = file.getFullPathName() + ":" + juce::String (file.getSize())
             + ":" + juce::String (pos.offset.inSeconds()) + ":" + juce::String (pos.getLength().inSeconds());

    if (auto it = cache.find (key); it != cache.end())
        return it->second;

    std::vector<float> peaks;
    auto& formats = clip.edit.engine.getAudioFileFormatManager().readFormatManager;

    // unique_ptr: the reader is deleted automatically when we're done.
    if (std::unique_ptr<juce::AudioFormatReader> reader { formats.createReaderFor (file) })
    {
        const auto start = (juce::int64) (pos.offset.inSeconds() * reader->sampleRate);
        const auto length = (juce::int64) (pos.getLength().inSeconds() * reader->sampleRate);
        const auto step = juce::jmax ((juce::int64) 1, length / numColumns);
        std::vector<juce::Range<float>> levels ((size_t) reader->numChannels);

        for (int i = 0; i < numColumns; ++i)
        {
            reader->readMaxLevels (start + i * step, step, levels.data(), (int) reader->numChannels);
            float peak = 0.0f;

            for (auto& r : levels)
                peak = juce::jmax (peak, std::abs (r.getStart()), std::abs (r.getEnd()));

            peaks.push_back (juce::jmin (1.0f, peak));
        }

        // Don't cache a file that's still being written.
        if (! clip.edit.getTransport().isRecording())
            cache[key] = peaks;
    }

    return peaks;
}

static RegionInfo makeRegionInfo (te::Clip& clip, std::map<juce::String, std::vector<float>>& peakCache)
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

        if (auto* wave = dynamic_cast<te::WaveAudioClip*> (&clip))
            r.peaks = computePeaks (*wave, 256, peakCache);
    }

    return r;
}

std::vector<RegionInfo> TrackController::getRegions (TrackId id) const
{
    std::vector<RegionInfo> result;

    if (auto* t = findTrack (owner.getEdit(), id))
        for (auto* clip : t->getClips())
            result.push_back (makeRegionInfo (*clip, impl->peakCache));

    return result;
}

std::optional<RegionInfo> TrackController::getRegion (RegionId id) const
{
    if (auto* edit = owner.getEdit())
        if (auto* clip = te::findClipForID (*edit, te::EditItemID::fromRawID (id)))
            return makeRegionInfo (*clip, impl->peakCache);

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

    if (t != nullptr && isAudioTrack (*t))
        return result;   // audio tracks have no instrument to control
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

//==============================================================================
// Plug-ins
std::optional<PluginSlot> TrackController::getInstrument (TrackId id) const
{
    if (auto* t = findTrack (owner.getEdit(), id))
        for (auto* p : t->pluginList)
            if (p->isSynth())
                return makeSlot (*p);

    return std::nullopt;
}

std::vector<PluginSlot> TrackController::getEffectChain (TrackId id) const
{
    std::vector<PluginSlot> result;

    if (auto* t = findTrack (owner.getEdit(), id))
        for (auto* p : effectsOn (*t))
            result.push_back (makeSlot (*p));

    return result;
}

std::vector<EffectType> TrackController::getAvailableEffects()
{
    return builtInEffects();
}

PluginId TrackController::addEffect (TrackId id, const juce::String& type, int position)
{
    auto* edit = owner.getEdit();
    auto* t = findTrack (edit, id);

    if (t == nullptr || (int) effectsOn (*t).size() >= maxEffectsPerTrack)
        return 0;

    owner.beginUndoStep ("Add Effect");

    auto plugin = edit->getPluginCache().createNewPlugin (type, {});

    if (plugin == nullptr)
        return 0;

    t->pluginList.insertPlugin (plugin, pluginListIndexForPosition (*t, position), nullptr);
    return plugin->itemID.getRawID();
}

PluginId TrackController::replaceEffect (TrackId id, PluginId pluginId, const juce::String& newType)
{
    auto* edit = owner.getEdit();
    auto* t = findTrack (edit, id);
    auto* old = t != nullptr ? findEffect (*t, pluginId) : nullptr;

    if (old == nullptr)
        return 0;

    auto plugin = edit->getPluginCache().createNewPlugin (newType, {});

    if (plugin == nullptr)
        return 0;

    // Remove + insert in one undo step, at the same position.
    owner.beginUndoStep ("Replace Effect");
    const auto index = t->pluginList.indexOf (old);
    old->removeFromParent();
    t->pluginList.insertPlugin (plugin, index, nullptr);
    return plugin->itemID.getRawID();
}

void TrackController::removeEffect (TrackId id, PluginId pluginId)
{
    if (auto* t = findTrack (owner.getEdit(), id))
    {
        if (auto* p = findEffect (*t, pluginId))
        {
            owner.beginUndoStep ("Remove Effect");
            p->removeFromParent();
        }
    }
}

void TrackController::moveEffect (TrackId id, PluginId pluginId, int newPosition)
{
    auto* t = findTrack (owner.getEdit(), id);

    if (t == nullptr)
        return;

    // Keep a reference while it's briefly out of the list. (Plugin::Ptr is
    // ref-counted, so the plugin isn't deleted when it's removed.)
    te::Plugin::Ptr plugin = findEffect (*t, pluginId);

    if (plugin == nullptr)
        return;

    auto effects = effectsOn (*t);
    const auto oldPosition = (int) std::distance (effects.begin(), std::find (effects.begin(), effects.end(), plugin.get()));

    if (oldPosition == newPosition)
        return;

    owner.beginUndoStep ("Move Effect");
    plugin->removeFromParent();
    t->pluginList.insertPlugin (plugin, pluginListIndexForPosition (*t, newPosition), nullptr);
}

void TrackController::setEffectEnabled (TrackId id, PluginId pluginId, bool enabled)
{
    if (auto* t = findTrack (owner.getEdit(), id))
    {
        if (auto* p = findEffect (*t, pluginId))
        {
            owner.beginUndoStep (enabled ? "Turn Effect On" : "Turn Effect Off");
            p->setEnabled (enabled);
        }
    }
}

std::vector<ParameterInfo> TrackController::getEffectParameters (TrackId id, PluginId pluginId) const
{
    std::vector<ParameterInfo> result;

    if (auto* t = findTrack (owner.getEdit(), id))
        if (auto* p = findEffect (*t, pluginId))
            for (auto* param : p->getAutomatableParameters())
                result.push_back ({ param->getParameterName(),
                                    param->getCurrentNormalisedValue(),
                                    param->getCurrentValueAsStringWithLabel() });

    return result;
}

void TrackController::setEffectParameter (TrackId id, PluginId pluginId, int index, float value)
{
    // No beginUndoStep() here: the UI starts one on knob drag start.
    if (auto* t = findTrack (owner.getEdit(), id))
    {
        if (auto* p = findEffect (*t, pluginId))
        {
            auto params = p->getAutomatableParameters();

            if (auto* param = params[index])
                setParameterUndoably (*p, *param, param->paramID,
                                      param->valueRange.convertFrom0to1 (juce::jlimit (0.0f, 1.0f, value)));
        }
    }
}

TrackDetails TrackController::getDetails (TrackId id) const
{
    TrackDetails d;
    auto* edit = owner.getEdit();
    auto* t = findTrack (edit, id);

    if (t == nullptr)
        return d;

    d.outputName = t->getOutput().getOutputName();

    if (isAudioTrack (*t))
    {
        d.pluginName = "None (audio track)";
        auto inputId = getRecordingSettings (id).inputId;

        for (auto& in : getAudioInputs())
            if (in.id == inputId)
                d.midiInputs.add (in.name);   // shown as this track's input

        return d;
    }

    d.pluginName = findSynth (*t) != nullptr ? juce::String ("4OSC (built-in synth)") : juce::String ("None");

    for (auto* instance : edit->getAllInputDevices())
        if (isRoutedMidiInput (instance->getInputDevice()))
            if (instance->getTargets().contains (t->itemID))
                d.midiInputs.add (instance->getInputDevice().getName());

    return d;
}

//==============================================================================
void TrackController::noteOn (int note, float velocity)
{
    if (! juce::isPositiveAndBelow (note, 128) || impl->keyboardDevice == nullptr)
        return;

    impl->heldNotes[(size_t) note] = true;
    impl->keyboardDevice->handleIncomingMidiMessage (juce::MidiMessage::noteOn (1, note, velocity), impl->keyboardSource);
}

void TrackController::noteOff (int note)
{
    if (! juce::isPositiveAndBelow (note, 128) || impl->keyboardDevice == nullptr)
        return;

    impl->heldNotes[(size_t) note] = false;
    impl->keyboardDevice->handleIncomingMidiMessage (juce::MidiMessage::noteOff (1, note), impl->keyboardSource);
}

void TrackController::allNotesOff()
{
    // Release held keys before the input moves to another track, so the old
    // track's synth doesn't keep sounding.
    for (int n = 0; n < 128; ++n)
        if (impl->heldNotes[(size_t) n])
            noteOff (n);
}

} // namespace ikaros
