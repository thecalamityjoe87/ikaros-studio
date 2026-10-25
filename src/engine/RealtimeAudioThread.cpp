#include "engine/RealtimeAudioThread.h"
#include "platform/AudioHardware.h"

#include <tracktion_graph/tracktion_graph.h>

namespace ikaros
{

namespace
{
    // The render workers running now (kernel thread IDs; 0 = a free slot).
    // Written by the workers themselves as they start and stop, read on the
    // message thread, so plain atomics: no locks between the two.
    std::array<std::atomic<std::int64_t>, RealtimeAudioThread::maxRenderThreads> renderThreads {};

    // On a worker, before it renders anything and after it's done (so the
    // system call and the loops here never run inside a block).
    void onRenderThread (bool started)
    {
        const auto tid = platform::currentThreadId();

        if (started)
        {
            for (auto& slot : renderThreads)
                if (std::int64_t empty = 0; slot.compare_exchange_strong (empty, tid))
                    return;

            return;   // more workers than slots: the rest stay at ordinary priority
        }

        for (auto& slot : renderThreads)
            if (auto mine = tid; slot.compare_exchange_strong (mine, 0))
                break;

        platform::forgetRealtimeThread (tid);
    }
}

void RealtimeAudioThread::trackRenderThreads()
{
    tracktion::graph::renderThreadObserver = onRenderThread;
}

std::vector<std::int64_t> RealtimeAudioThread::getRenderThreadIds()
{
    std::vector<std::int64_t> ids;

    for (auto& slot : renderThreads)
        if (const auto id = slot.load())
            ids.push_back (id);

    return ids;
}

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
    auto result = platform::isThreadRealtime (audioThreadId.load());

    for (auto id : getRenderThreadIds())
        if (result == true && platform::isThreadRealtime (id) == false)
            result = false;

    return result;
}

void RealtimeAudioThread::timerCallback()
{
    keepRealtime (audio, audioThreadId.load(), "Audio thread");

    for (size_t i = 0; i < workers.size(); ++i)
        keepRealtime (workers[i], renderThreads[i].load(), "Render worker");
}

void RealtimeAudioThread::keepRealtime (Promotion& p, std::int64_t id, const char* what)
{
    if (id != p.id)
    {
        // A worker forgets itself as it exits; the old audio thread is gone
        // once a new one runs.
        if (p.id != 0)
            platform::forgetRealtimeThread (p.id);

        p = {};
        p.id = id;

        if (id == 0)
            return;

        // On a CPU with fast and slow cores, only the fast ones (the same
        // block can take three times as long on a slow one).
        platform::keepOnFastestCores (id);

        // One try per thread: if it's refused, asking again won't help.
        p.promoted = platform::makeThreadRealtime (id);
        p.calmTicksNeeded = 4 * 5;   // 5 seconds at 4 Hz
        DBG (what << " " << id << (p.promoted ? " is now real-time" : " stays at ordinary priority"));
        return;
    }

    if (id == 0)
        return;

    // A thread that was real-time but isn't any more was put back to ordinary
    // priority because one of them ran too long without a break (the session
    // was too heavy; see platform::makeThreadRealtime), or by RealtimeKit when
    // the whole desktop was starved. Once the load has been light for a few
    // seconds, ask again, so one overload doesn't cost real-time for the
    // rest of the session. Each overload is a 100 ms stall, and the CPU
    // meter can't see every kind of load (a sound server or driver being
    // slow, say), so the wait doubles each time, up to two minutes.
    if (p.promoted && platform::isThreadRealtime (id) == false)
    {
        p.calmTicks = deviceManager.getCpuUsage() < 0.7 ? p.calmTicks + 1 : 0;

        if (p.calmTicks >= p.calmTicksNeeded)
        {
            p.calmTicks = 0;
            p.calmTicksNeeded = juce::jmin (p.calmTicksNeeded * 2, 4 * 120);
            p.promoted = platform::makeThreadRealtime (id);
            DBG (what << " " << id << (p.promoted ? " is real-time again" : " stays at ordinary priority"));
        }
    }
}

} // namespace ikaros
