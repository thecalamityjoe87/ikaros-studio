#include "engine/InputRouter.h"
#include "engine/AudioEngine.h"
#include "engine/TrackHelpers.h"
#include "engine/Tuner.h"
#include "engine/LiveWaveform.h"
#include "engine/MidiPorts.h"
#include <map>

namespace te = tracktion;

namespace ikaros
{

using namespace detail;

//==============================================================================
struct InputRouter::Impl
{
    // The on-screen keyboard plays through a Tracktion "virtual" MIDI input,
    // so its notes go through the same path as a hardware keyboard and can be
    // recorded. Created once and remembered in Tracktion's settings.
    std::shared_ptr<te::MidiInputDevice> keyboardDevice;
    te::MPESourceID keyboardSource = te::createUniqueMPESourceID();

    std::array<bool, 128> heldNotes {};   // keys currently down on the on-screen keyboard

    // The input meters, one per input that's shown (by input ID).
    // WeakReference: input devices can be rebuilt when the audio device changes.
    struct Meter
    {
        juce::WeakReference<te::LevelMeasurer> measurer;
        te::LevelMeasurer::Client client;
        InputMeter reading;
        double lastReadMs = 0.0;

        void attach (te::LevelMeasurer* m)
        {
            if (m == measurer.get())
                return;

            detach();

            if (m != nullptr)
                m->addClient (client);

            measurer = m;
        }

        void detach()
        {
            if (auto* m = measurer.get())
                m->removeClient (client);

            measurer = nullptr;
        }

        ~Meter()    { detach(); }
    };

    // unique_ptr: a Client can't be moved (the measurer points at it).
    std::map<juce::String, std::unique_ptr<Meter>> meters;
};

//==============================================================================
InputRouter::InputRouter (AudioEngine& o)
    : impl (std::make_unique<Impl>()), owner (o)
{
}

InputRouter::~InputRouter() = default;

void InputRouter::sessionChanged()
{
    impl->heldNotes.fill (false);
    selectedTrack = 0;
    armedTrack = 0;
}

void InputRouter::setSelectedTrack (TrackId id)
{
    const bool changed = id != selectedTrack;
    selectedTrack = id;

    // The arm follows the selection, but not mid-take: the take stays on the
    // track it started on. Only on a change, so a track the user disarmed
    // stays disarmed while it's still selected. Not at all if the user
    // prefers to arm tracks themselves.
    if (changed && ! owner.getTransportState().recording && owner.getPreferences().get (settings::autoArmTrack))
    {
        if (id != 0)
            setArmed (id, true);
        else
            setArmed (armedTrack, false);
    }

    routeMidiInputs();
}

void InputRouter::trackDeleted (TrackId id)
{
    if (armedTrack == id)
        armedTrack = 0;
}

void InputRouter::routeMidiInputs()
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
    // (MonitorMode::on), so playing always makes sound. Not a controller's
    // DAW-control ports, though (see isControlSurfacePort): disabled, they
    // get no input instance, so they can't reach a track.
    for (auto& midiIn : dm.getMidiInDevices())
    {
        const bool plays = midiIn->getDeviceType() != te::InputDevice::physicalMidiDevice
                        || ! isControlSurfacePort (midiIn->getName());

        midiIn->setEnabled (plays);

        if (plays)
            midiIn->setMonitorMode (te::InputDevice::MonitorMode::on);
    }

    // A Mackie Control port's output belongs to the ControlSurface (its
    // lights and motor faders). On Windows a port opens only once.
    for (int i = 0; i < dm.getNumMidiOutDevices(); ++i)
        if (auto* midiOut = dm.getMidiOutDevice (i); midiOut != nullptr && isMackiePort (midiOut->getName()))
            if (midiOut->isEnabled())
                midiOut->setEnabled (false);

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

std::bitset<128> InputRouter::getHeldControllerNotes() const
{
    std::bitset<128> held;

    // Tracktion keeps each MIDI input's held keys in a juce::MidiKeyboardState.
    // (Disabled inputs, such as a controller's DAW-control ports, play nothing.)
    for (auto& midiIn : owner.getTeEngine().getDeviceManager().getMidiInDevices())
        if (midiIn->getDeviceType() == te::InputDevice::physicalMidiDevice && midiIn->isEnabled())
            for (int note = 0; note < 128; ++note)
                if (midiIn->keyboardState.isNoteOnForChannels (0xffff, note))   // 0xffff = any of the 16 channels
                    held.set ((size_t) note);

    return held;
}

void InputRouter::routeAudioInputs()
{
    auto* edit = owner.getEdit();
    auto& dm = owner.getTeEngine().getDeviceManager();

    // The inputs themselves (each channel on its own, plus stereo pairs) are
    // laid out by IkarosEngineBehaviour::describeWaveDevices.
    bool devicesChanged = false;

    // Only the channels the audio device actually opened carry audio (with
    // PipeWire the driver can advertise dozens). Switch the others off.
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

    // The tuner listens to the selected audio track's input, straight from
    // the device's channels (see Tuner), and "Mute while tuning" turns that
    // input's monitoring off while the tuner is on.
    auto& tuner = owner.getTuner();
    const auto tunedInputId = selected != nullptr && isAudioTrack (*selected)
                                ? getRecordingSettings (selectedTrack).inputId : juce::String();
    int tunerChannels[2] = { -1, -1 };

    for (auto* in : dm.getWaveInputDevices())
    {
        if (tunedInputId.isNotEmpty() && in->getDeviceID() == tunedInputId && in->isEnabled())
        {
            int n = 0;

            for (auto& ci : in->getChannels())
                if (n < 2)
                    tunerChannels[n++] = ci.indexInDevice;
        }
    }

    tuner.setInputChannels (tunerChannels[0], tunerChannels[1]);

    // The live waveform (see LiveWaveform) listens to the input the takes
    // are recorded from: the armed (or else selected) audio track's.
    int takeChannels[2] = { -1, -1 };

    for (auto* in : dm.getWaveInputDevices())
    {
        if (settings.inputId.isNotEmpty() && in->getDeviceID() == settings.inputId && in->isEnabled())
        {
            int n = 0;

            for (auto& ci : in->getChannels())
                if (n < 2)
                    takeChannels[n++] = ci.indexInDevice;
        }
    }

    owner.getLiveWaveform().setInputChannels (takeChannels[0], takeChannels[1]);
    const bool tunerMutes = tuner.isActive() && owner.getPreferences().get (ikaros::settings::muteWhileTuning);   // ikaros:: because the local `settings` hides the namespace

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
            const bool mutedForTuner = tunerMutes && device.getDeviceID() == tunedInputId;
            device.setMonitorMode (settings.monitoring && ! mutedForTuner ? te::InputDevice::MonitorMode::on
                                                                          : te::InputDevice::MonitorMode::off);
        }
    }
}

//==============================================================================
// Recording
void InputRouter::setArmed (TrackId id, bool shouldArm)
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

        ModelChanges changes;
        changes.tracks = true;   // the Record buttons
        owner.notifyModelChanged (changes);
    }
}

TrackId InputRouter::armForRecording()
{
    if (armedTrack != 0)
        return 0;

    setArmed (selectedTrack, true);
    return armedTrack;
}

//==============================================================================
// Audio inputs
std::vector<AudioInput> InputRouter::getAudioInputs() const
{
    std::vector<AudioInput> result;
    auto& dm = owner.getTeEngine().getDeviceManager();

    // Inputs for channels the audio device has open (routeAudioInputs()
    // switches the rest off), named by channel: "Input 1", "Input 1+2 (stereo)".
    // Mono ones first, then the pairs.
    for (int pass = 0; pass < 2; ++pass)
    {
        for (auto* in : dm.getWaveInputDevices())
        {
            if (in->getDeviceType() != te::InputDevice::waveDevice || ! in->isEnabled())
                continue;

            auto& channels = in->getChannels();
            const bool stereo = channels.getNumChannels() > 1;

            if (stereo != (pass == 1))
                continue;

            juce::StringArray numbers;

            for (auto& ci : channels)
                numbers.add (juce::String (ci.indexInDevice + 1));

            result.push_back ({ in->getDeviceID(),
                                "Input " + numbers.joinIntoString ("+") + (stereo ? " (stereo)" : "") });
        }
    }

    return result;
}

RecordingSettings InputRouter::getRecordingSettings (TrackId id) const
{
    RecordingSettings r;

    if (auto* t = findTrack (owner.getEdit(), id))
    {
        r.inputId = t->state.getProperty (inputProp).toString();
        r.monitoring = (bool) t->state.getProperty (monitorProp, false);
    }

    return r;
}

void InputRouter::setTrackInput (TrackId id, const juce::String& inputId)
{
    if (auto* t = findTrack (owner.getEdit(), id))
    {
        owner.beginUndoStep ("Change Input");
        t->state.setProperty (inputProp, inputId, &owner.getEdit()->getUndoManager());
        routeMidiInputs();
    }
}

void InputRouter::setMonitoring (TrackId id, bool shouldMonitor)
{
    if (auto* t = findTrack (owner.getEdit(), id))
    {
        owner.beginUndoStep (shouldMonitor ? "Monitoring On" : "Monitoring Off");
        t->state.setProperty (monitorProp, shouldMonitor, &owner.getEdit()->getUndoManager());
        routeMidiInputs();
    }
}

InputMeter InputRouter::getInputMeter (TrackId id)
{
    const auto inputId = getRecordingSettings (id).inputId;
    te::InputDevice* device = nullptr;

    for (auto* in : owner.getTeEngine().getDeviceManager().getWaveInputDevices())
        if (in->getDeviceID() == inputId && in->isEnabled())
            device = in;

    if (device == nullptr)
        return {};

    auto& meter = impl->meters[inputId];

    if (meter == nullptr)
        meter = std::make_unique<Impl::Meter>();

    meter->attach (&device->levelMeasurer);

    // Reading clears the levels, so a second meter reading this input in
    // the same timer tick (track header and Recording section) gets the
    // first one's reading instead of nothing.
    const auto now = juce::Time::getMillisecondCounterHiRes();

    if (now - meter->lastReadMs > 10.0)
    {
        meter->lastReadMs = now;

        // Mono inputs report one channel; show it on both meter bars.
        auto& client = meter->client;
        auto left = client.getAndClearAudioLevel (0).dB;
        auto right = client.getNumChannelsUsed() > 1 ? client.getAndClearAudioLevel (1).dB : left;
        meter->reading.level = { left, right };

        if (juce::jmax (left, right) >= clipLevelDb)
            meter->reading.clipped = true;   // stays on until cleared
    }

    return meter->reading;
}

void InputRouter::clearClipIndicator (TrackId id)
{
    if (auto it = impl->meters.find (getRecordingSettings (id).inputId); it != impl->meters.end())
        it->second->reading.clipped = false;
}

//==============================================================================
void InputRouter::noteOn (int note, float velocity)
{
    if (! juce::isPositiveAndBelow (note, 128) || impl->keyboardDevice == nullptr)
        return;

    impl->heldNotes[(size_t) note] = true;
    impl->keyboardDevice->handleIncomingMidiMessage (juce::MidiMessage::noteOn (1, note, velocity), impl->keyboardSource);
}

void InputRouter::noteOff (int note)
{
    if (! juce::isPositiveAndBelow (note, 128) || impl->keyboardDevice == nullptr)
        return;

    impl->heldNotes[(size_t) note] = false;
    impl->keyboardDevice->handleIncomingMidiMessage (juce::MidiMessage::noteOff (1, note), impl->keyboardSource);
}

void InputRouter::allNotesOff()
{
    // Release held keys before the input moves to another track, so the old
    // track's synth doesn't keep sounding.
    for (int n = 0; n < 128; ++n)
        if (impl->heldNotes[(size_t) n])
            noteOff (n);
}

} // namespace ikaros
