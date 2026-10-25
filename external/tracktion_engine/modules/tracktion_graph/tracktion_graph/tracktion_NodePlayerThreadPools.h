/*
    ,--.                     ,--.     ,--.  ,--.
  ,-'  '-.,--.--.,--,--.,---.|  |,-.,-'  '-.`--' ,---. ,--,--,      Copyright 2024
  '-.  .-'|  .--' ,-.  | .--'|     /'-.  .-',--.| .-. ||      \   Tracktion Software
    |  |  |  |  \ '-'  \ `--.|  \  \  |  |  |  |' '-' '|  ||  |       Corporation
    `---' `--'   `--`--'`---'`--'`--' `---' `--' `---' `--''--'    www.tracktion.com

    Tracktion Engine uses a GPL/commercial licence - see LICENCE.md for details.
*/

#pragma once


namespace tracktion::inline graph {

//==============================================================================
/**
    Available strategies for thread pools.
    This will determine how worker threads are handled when no Nodes are
    available for processing.
*/
enum class ThreadPoolStrategy
{
    conditionVariable,      /**< Uses CVs to pause threads. */
    realTime,               /**< Uses pause, yield and sleeps to suspend threads. */
    hybrid,                 /**< Uses a combination of the above, avoiding CVs on the audio thread. */
    semaphore,              /**< Uses a semaphore to suspend threads. */
    lightweightSemaphore,   /**< Uses a semaphore/spin mechanism to suspend threads.*/
    lightweightSemHybrid    /**< Uses a combination of semaphores/spin and yields to suspend threads.*/
};

/** Returns a function to create a ThreadPool for the given stategy. */
LockFreeMultiThreadedNodePlayer::ThreadPoolCreator getPoolCreatorFunction (ThreadPoolStrategy);

/** If set, called on each worker thread of a live player's pool (see
    LockFreeMultiThreadedNodePlayer::setRendersLive) when it starts (true) and
    just before it exits (false). Lets an app adjust its workers, e.g. ask for
    real-time priority where setThreadPriority can't get it.
*/
using RenderThreadObserver = void (*) (bool started);
inline std::atomic<RenderThreadObserver> renderThreadObserver { nullptr };

}
