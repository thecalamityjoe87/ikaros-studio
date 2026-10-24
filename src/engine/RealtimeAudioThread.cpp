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

    if (id == 0)
        return;

    if (id != promotedId)
    {
        // On a CPU with fast and slow cores, only the fast ones (the same
        // block can take three times as long on a slow one).
        platform::keepOnFastestCores (id);

        // One try per thread: if it's refused, asking again won't help.
        promotedId = id;
        promoted = platform::makeThreadRealtime (id);
        calmTicks = 0;
        calmTicksNeeded = 4 * 5;   // 5 seconds at 4 Hz
        DBG ("Audio thread " << id << (promoted ? " is now real-time" : " stays at ordinary priority"));
        return;
    }

    // A thread that was real-time but isn't any more was put back to ordinary
    // priority because it ran too long without a break (the session was too
    // heavy; see platform::makeThreadRealtime), or by RealtimeKit when the
    // whole desktop was starved. Once the load has been light for a few
    // seconds, ask again, so one overload doesn't cost real-time for the
    // rest of the session. Each overload is a 100 ms stall, and the CPU
    // meter can't see every kind of load (a sound server or driver being
    // slow, say), so the wait doubles each time, up to two minutes.
    if (promoted && isRealtime() == false)
    {
        calmTicks = deviceManager.getCpuUsage() < 0.7 ? calmTicks + 1 : 0;

        if (calmTicks >= calmTicksNeeded)
        {
            calmTicks = 0;
            calmTicksNeeded = juce::jmin (calmTicksNeeded * 2, 4 * 120);
            promoted = platform::makeThreadRealtime (id);
            DBG ("Audio thread " << id << (promoted ? " is real-time again" : " stays at ordinary priority"));
        }
    }
}

} // namespace ikaros
