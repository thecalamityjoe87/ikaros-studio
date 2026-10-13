#include "engine/AudioDeviceWatcher.h"
#include "engine/AudioEngine.h"
#include "engine/Preferences.h"
#include "engine/TrackHelpers.h"
#include "platform/AudioHardware.h"

namespace ikaros
{

// ALSA names are "Card, Port; Description" and other systems' are the card's
// name: the first part is what the user knows the device as.
static juce::String friendlyName (const juce::String& deviceName)
{
    return deviceName.upToFirstOccurrenceOf (",", false, false).upToFirstOccurrenceOf (";", false, false).trim();
}

static juce::String firstNew (const juce::StringArray& now, const juce::StringArray& before)
{
    for (auto& name : now)
        if (! before.contains (name))
            return name;

    return {};
}

std::optional<AudioDevicePrompt> findDeviceChange (const AudioDeviceState& before, const AudioDeviceState& now)
{
    auto wentAway = [] (const juce::StringArray& was, const juce::StringArray& is, const juce::String& inUse)
    {
        return inUse.isNotEmpty() && was.contains (inUse) && ! is.contains (inUse);
    };

    const bool lostInput = wentAway (before.inputs, now.inputs, before.inUseInput);
    const bool lostOutput = wentAway (before.outputs, now.outputs, before.inUseOutput);

    AudioDevicePrompt prompt;

    if (lostInput || lostOutput)
    {
        prompt.kind = AudioDevicePrompt::Kind::disappeared;
        prompt.deviceName = friendlyName (lostOutput ? before.inUseOutput : before.inUseInput);

        // Some systems have already moved to another device: offer only what isn't in use yet.
        if (lostInput && now.inUseInput != now.defaultInput)
            prompt.inputName = now.defaultInput;

        if (lostOutput && now.inUseOutput != now.defaultOutput)
            prompt.outputName = now.defaultOutput;
    }
    else
    {
        prompt.inputName = firstNew (now.inputs, before.inputs);
        prompt.outputName = firstNew (now.outputs, before.outputs);

        if (prompt.inputName == now.inUseInput)     prompt.inputName = {};
        if (prompt.outputName == now.inUseOutput)   prompt.outputName = {};

        // One device at a time: an input and an output that belong to different
        // devices came together, so take the output.
        if (prompt.inputName.isNotEmpty() && prompt.outputName.isNotEmpty()
             && friendlyName (prompt.inputName) != friendlyName (prompt.outputName))
            prompt.inputName = {};
    }

    if (prompt.inputName.isEmpty() && prompt.outputName.isEmpty())
        return std::nullopt;

    prompt.useName = friendlyName (prompt.outputName.isNotEmpty() ? prompt.outputName : prompt.inputName);

    if (prompt.kind == AudioDevicePrompt::Kind::appeared)
        prompt.deviceName = prompt.useName;

    return prompt;
}

//==============================================================================
AudioDeviceWatcher::AudioDeviceWatcher (AudioEngine& e)
    : owner (e)
{
    known = readState();
    haveKnown = true;

    owner.getTeEngine().getDeviceManager().deviceManager.addChangeListener (this);
    startTimer (1000);
}

AudioDeviceWatcher::~AudioDeviceWatcher()
{
    stopTimer();
    owner.getTeEngine().getDeviceManager().deviceManager.removeChangeListener (this);
}

std::optional<AudioDevicePrompt> AudioDeviceWatcher::popPrompt()
{
    return std::exchange (pending, std::nullopt);
}

void AudioDeviceWatcher::setStateSource (std::function<AudioDeviceState()> source)
{
    stateSource = std::move (source);
    haveKnown = false;
    pending.reset();
}

AudioDeviceState AudioDeviceWatcher::readState() const
{
    auto& dm = owner.getTeEngine().getDeviceManager();
    auto& adm = dm.deviceManager;
    AudioDeviceState state;

    // The pretend device in tests has no list of devices.
    if (dm.isHostedAudioDeviceInterfaceInUse())
        return state;

    auto* type = adm.getCurrentDeviceTypeObject();

    if (type == nullptr)
        return state;

    const auto setup = adm.getAudioDeviceSetup();
    state.inputs = type->getDeviceNames (true);
    state.outputs = type->getDeviceNames (false);
    state.inUseInput = setup.inputDeviceName;
    state.inUseOutput = setup.outputDeviceName;
    state.defaultInput = state.inputs[type->getDefaultDeviceIndex (true)];
    state.defaultOutput = state.outputs[type->getDefaultDeviceIndex (false)];
    return state;
}

void AudioDeviceWatcher::check()
{
    // Not while recording: nothing is lost by waiting, the change is still there afterwards.
    if (owner.getTransportState().recording)
        return;

    if (! stateSource)
        platform::refreshAudioDeviceLists (owner.getTeEngine().getDeviceManager().deviceManager, lastHardware);

    const auto now = stateSource ? stateSource() : readState();

    if (! haveKnown)
    {
        known = now;
        haveKnown = true;
        return;
    }

    auto change = findDeviceChange (known, now);
    known = now;

    if (! change)
        return;

    auto& prefs = owner.getPreferences();
    const auto choice = prefs.get (settings::deviceChange);

    if (choice == settings::DeviceChange::neverAsk)
        return;

    // A switch that fails is shown to the user as a question, which will show why.
    if (choice == settings::DeviceChange::alwaysSwitch && owner.switchAudioDevice (*change).isEmpty())
        return;

    pending = std::move (change);
}

} // namespace ikaros
