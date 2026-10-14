#pragma once

#include "engine/RecordingTypes.h"
#include "platform/AudioHardware.h"
#include <functional>
#include <optional>

namespace ikaros
{

class AudioEngine;

/** The audio devices the computer has and which ones are in use, as JUCE sees them. */
struct AudioDeviceState
{
    juce::StringArray inputs, outputs;
    juce::String inUseInput, inUseOutput;
    juce::String defaultInput, defaultOutput;
};

/** What to ask the user, given the devices before and now: a device that
    appeared, or the one in use that went away. Nothing if there's nothing
    to offer (e.g. the system has already switched by itself). */
std::optional<AudioDevicePrompt> findDeviceChange (const AudioDeviceState& before, const AudioDeviceState& now);

//==============================================================================
/**
    Notices audio devices being plugged in or unplugged, and has the UI offer
    to switch to the new one (or away from the one that left), like GarageBand.

    It's checked when JUCE reports a change (macOS, Windows) and once a second
    (Linux, where JUCE doesn't, see platform::refreshAudioDeviceLists). It waits
    while a take is recording. The "Audio device changes" setting can have it
    switch without asking (alwaysSwitch) or stay quiet (neverAsk).

    Message thread only.
*/
class AudioDeviceWatcher  : private juce::Timer,
                            private juce::ChangeListener
{
public:
    explicit AudioDeviceWatcher (AudioEngine&);
    ~AudioDeviceWatcher() override;

    /** The question to ask, once (then nullopt). */
    std::optional<AudioDevicePrompt> popPrompt();

    /** Looks for a change now. */
    void check();

    /** Opens what the prompt names. The old device is closed first, as a sound
        server holds a new card for as long as the old stream runs through it,
        and may still do so for a few seconds after: so it tries again until
        it works or gives up. `done` gets the error, or empty. */
    void switchTo (const AudioDevicePrompt&, std::function<void (juce::String error)> done);

    /** For tests: reads the devices from here instead of JUCE. */
    void setStateSource (std::function<AudioDeviceState()>);

private:
    void timerCallback() override       { if (switching) trySwitch(); else check(); }
    void changeListenerCallback (juce::ChangeBroadcaster*) override   { check(); }
    AudioDeviceState readState() const;
    void trySwitch();
    void reopenIfBack (const AudioDeviceState& now);

    AudioEngine& owner;
    std::function<AudioDeviceState()> stateSource;   // empty: ask JUCE
    AudioDeviceState known;
    bool haveKnown = false;
    platform::HardwareWatch hardware;                // see platform::refreshAudioDeviceLists
    juce::String goneInput, goneOutput;              // the in-use device that left, until it's back or replaced
    std::optional<AudioDevicePrompt> pending;

    struct Switching
    {
        AudioDevicePrompt prompt;
        juce::AudioDeviceManager::AudioDeviceSetup previous;
        std::function<void (juce::String)> done;
        int tries = 0;
    };

    std::optional<Switching> switching;
};

} // namespace ikaros
