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

// Lower is better. A sound server usually holds the card, so only ALSA's shared modes open;
// the direct hardware ones fail as busy.
static int modeRank (const juce::String& name)
{
    if (name.containsIgnoreCase ("front output"))
        return 0;

    return name.containsIgnoreCase ("hardware device") ? 2 : 1;
}

// ALSA's usbstream plugin takes only a card, so it's listed for every sound card, MIDI-only ones
// (a keyboard controller) too, as "<card>; USB Stream Output" with both inputs and outputs. It only
// drives Tascam's US-122L and US-144: on anything else opening it fails.
static AudioDeviceState withoutUsbStreams (AudioDeviceState state)
{
    for (auto* list : { &state.inputs, &state.outputs })
        list->strings.removeIf ([] (const juce::String& name) { return name.endsWith ("USB Stream Output"); });

    return state;
}

std::optional<AudioDevicePrompt> findDeviceChange (const AudioDeviceState& listedBefore, const AudioDeviceState& listedNow)
{
    const auto before = withoutUsbStreams (listedBefore);
    const auto now = withoutUsbStreams (listedNow);

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

        // ALSA lists several modes per card, not all with inputs (and an input
        // and output of different modes can't be opened together): take the best one that has both.
        int bestRank = 99;

        for (auto& name : now.outputs)
        {
            if (! before.outputs.contains (name) && now.inputs.contains (name) && ! before.inputs.contains (name)
                 && modeRank (name) < bestRank)
            {
                bestRank = modeRank (name);
                prompt.inputName = prompt.outputName = name;
            }
        }

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

void AudioDeviceWatcher::switchTo (const AudioDevicePrompt& prompt, std::function<void (juce::String)> done)
{
    auto& adm = owner.getTeEngine().getDeviceManager().deviceManager;

    if (switching)
    {
        if (done)
            done ("Already switching.");

        return;
    }

    switching = Switching { prompt, adm.getAudioDeviceSetup(), std::move (done), 0 };
    adm.closeAudioDevice();
    trySwitch();
}

void AudioDeviceWatcher::trySwitch()
{
    constexpr int maxTries = 8;

    auto error = owner.openDevicesFor (switching->prompt);

    if (error.isNotEmpty() && ++switching->tries < maxTries)
        return;

    auto finished = std::move (*switching);
    switching.reset();

    // A failed switch leaves nothing open.
    if (error.isNotEmpty())
        owner.getTeEngine().getDeviceManager().deviceManager.setAudioDeviceSetup (finished.previous, true);

    if (finished.done)
        finished.done (error);
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

void AudioDeviceWatcher::reopenIfBack (const AudioDeviceState& now)
{
    auto left = [] (const juce::StringArray& was, const juce::StringArray& is, const juce::String& name)
    {
        return name.isNotEmpty() && was.contains (name) && ! is.contains (name);
    };

    if (left (known.inputs, now.inputs, known.inUseInput) || left (known.outputs, now.outputs, known.inUseOutput))
    {
        goneInput = known.inUseInput;
        goneOutput = known.inUseOutput;
        return;
    }

    if (goneInput.isEmpty() && goneOutput.isEmpty())
        return;

    // Moved to another device since: nothing to bring back.
    if (now.inUseInput != goneInput || now.inUseOutput != goneOutput)
    {
        goneInput = goneOutput = {};
        return;
    }

    auto back = [] (const juce::StringArray& list, const juce::String& name) { return name.isEmpty() || list.contains (name); };

    if (! back (now.inputs, goneInput) || ! back (now.outputs, goneOutput))
        return;

    goneInput = goneOutput = {};

    // The setup still names the device but its stream ended when it left, so no prompt would come either.
    auto& adm = owner.getTeEngine().getDeviceManager().deviceManager;
    adm.closeAudioDevice();
    adm.restartLastAudioDevice();
}

void AudioDeviceWatcher::check()
{
    if (switching)
        return;

    // Not while recording: nothing is lost by waiting, the change is still there afterwards.
    if (owner.getTransportState().recording)
        return;

    // Not while the device picker is open: it keeps references to the lists being replaced.
    if (! stateSource && ! owner.isAudioSettingsOpen())
        platform::refreshAudioDeviceLists (owner.getTeEngine().getDeviceManager().deviceManager, hardware);

    const auto now = stateSource ? stateSource() : readState();

    if (! haveKnown)
    {
        known = now;
        haveKnown = true;
        return;
    }

    if (! stateSource)
        reopenIfBack (now);

    auto change = findDeviceChange (known, now);
    known = now;

    if (! change)
        return;

    auto& prefs = owner.getPreferences();
    const auto choice = prefs.get (settings::deviceChange);

    if (choice == settings::DeviceChange::neverAsk)
        return;

    if (choice == settings::DeviceChange::alwaysSwitch)
    {
        // A switch that fails is shown to the user as a question, which will show why.
        owner.switchAudioDevice (*change, [this, prompt = *change] (juce::String error)
                                 {
                                     if (error.isNotEmpty())
                                         pending = prompt;
                                 });
        return;
    }

    pending = std::move (change);
}

} // namespace ikaros
