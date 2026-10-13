#pragma once

#include "engine/AudioEngine.h"

namespace ikaros
{

//==============================================================================
/**
    The Audio & MIDI Settings window's content: the device picker, and under
    it the monitoring delay (latency) and dropouts, kept up to date while
    the settings change.
*/
class AudioSettingsPanel  : public juce::Component,
                            private juce::Timer
{
public:
    explicit AudioSettingsPanel (AudioEngine&);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    AudioEngine& engine;
    std::unique_ptr<juce::Component> selector;   // JUCE's device picker, from the engine
    AudioDeviceStatus status;
    juce::Rectangle<int> infoArea;

    static constexpr int infoHeight = 100;

    void timerCallback() override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioSettingsPanel)
};

} // namespace ikaros
