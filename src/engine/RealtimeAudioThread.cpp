#include "engine/RealtimeAudioThread.h"
#include "platform/AudioHardware.h"

namespace ikaros
{

RealtimeAudioThread::RealtimeAudioThread (juce::AudioDeviceManager& dm) : deviceManager (dm)
{
    deviceManager.addAudioCallback (this);
    startTimerHz (4);
}

RealtimeAudioThread::~RealtimeAudioThread()
{
    stopTimer();
    deviceManager.removeAudioCallback (this);
}

void RealtimeAudioThread::reattach()
{
    deviceManager.removeAudioCallback (this);
    deviceManager.addAudioCallback (this);
}

void RealtimeAudioThread::audioDeviceIOCallbackWithContext (const float* const*, int,
                                                            float* const* outputs, int numOutputs, int numSamples,
                                                            const juce::AudioIODeviceCallbackContext&)
{
    // Every callback's output is mixed into what you hear, so ours must be silent.
    for (int i = 0; i < numOutputs; ++i)
        if (outputs[i] != nullptr)
            juce::FloatVectorOperations::clear (outputs[i], numSamples);

    // JUCE's thread ID is cheap to read; the kernel's (which the scheduler
    // needs) is a system call, so it's only fetched for a new thread.
    const auto thread = juce::Thread::getCurrentThreadId();

    if (thread != audioThread.load())
    {
        audioThread = thread;

        // A device driven from the message thread (the tests' pretend one)
        // has no audio thread of its own, and the UI mustn't go real-time.
        audioThreadId = juce::MessageManager::existsAndIsCurrentThread() ? 0 : platform::currentThreadId();
    }
}

void RealtimeAudioThread::audioDeviceAboutToStart (juce::AudioIODevice*)
{
    // The device (re)starts with a new thread, and a new thread can get the
    // old one's JUCE ID (Linux reuses them), so the callback wouldn't notice
    // the change: forget the old one, and the next callback looks again.
    audioThread = nullptr;
}

void RealtimeAudioThread::audioDeviceStopped()
{
    // No audio thread until the device starts again.
    audioThread = nullptr;
    audioThreadId = 0;
}

std::optional<bool> RealtimeAudioThread::isRealtime() const
{
    return platform::isThreadRealtime (audioThreadId.load());
}

void RealtimeAudioThread::timerCallback()
{
    const auto id = audioThreadId.load();

    if (id == 0 || id == promotedId)
        return;

    // One try per thread: if it's refused, asking again won't help.
    promotedId = id;
    const bool ok = platform::makeThreadRealtime (id);
    DBG ("Audio thread " << id << (ok ? " is now real-time" : " stays at ordinary priority"));
    juce::ignoreUnused (ok);
}

} // namespace ikaros
