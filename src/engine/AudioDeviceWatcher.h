#pragma once

#include "engine/RecordingTypes.h"
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

    /** For tests: reads the devices from here instead of JUCE. */
    void setStateSource (std::function<AudioDeviceState()>);

private:
    void timerCallback() override       { check(); }
    void changeListenerCallback (juce::ChangeBroadcaster*) override   { check(); }
    AudioDeviceState readState() const;

    AudioEngine& owner;
    std::function<AudioDeviceState()> stateSource;   // empty: ask JUCE
    AudioDeviceState known;
    bool haveKnown = false;
    juce::String lastHardware;                       // see platform::refreshAudioDeviceLists
    std::optional<AudioDevicePrompt> pending;
};

} // namespace ikaros
