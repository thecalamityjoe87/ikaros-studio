#pragma once

#include "engine/AudioEngine.h"
#include <thread>

namespace ikaros
{

//==============================================================================
/**
    Packs a session into its .iksession file on a background thread (see
    AudioEngine::beginSave), so the window keeps drawing while a big session
    saves.

    It covers the window while the save runs, so nothing can be changed
    halfway through (a change then wouldn't be in the file, yet would look
    saved). Quick saves finish before it has drawn anything; a slow one shows
    "Saving..." with a progress bar after a moment.
*/
class SavingOverlay  : public juce::Component,
                       private juce::Timer
{
public:
    /** Takes a job from AudioEngine::beginSave, runs it, then calls
        AudioEngine::finishSave and onDone (on the message thread). The
        overlay deletes itself afterwards. */
    static void run (juce::Component& parent, AudioEngine&, std::unique_ptr<AudioEngine::SaveJob>,
                     std::function<void (bool saved)> onDone);

    ~SavingOverlay() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override   { return true; }   // swallow keys while saving

private:
    SavingOverlay (AudioEngine&, std::unique_ptr<AudioEngine::SaveJob>, std::function<void (bool)>);

    AudioEngine& engine;
    std::unique_ptr<AudioEngine::SaveJob> job;
    std::function<void (bool)> onDone;

    std::thread worker;
    std::atomic<bool> finished { false };

    double progress = 0.0;                 // what the progress bar shows (it reads this variable)
    juce::ProgressBar progressBar { progress };
    juce::uint32 startTime = 0;
    juce::Rectangle<int> card;

    void timerCallback() override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SavingOverlay)
};

} // namespace ikaros
