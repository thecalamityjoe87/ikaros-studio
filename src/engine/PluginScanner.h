#pragma once

#include "engine/EngineTypes.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>
#include <map>
#include <memory>

namespace ikaros
{

//==============================================================================
/**
    Finds the third-party plug-ins (VST3, LV2, Audio Units) installed on this
    computer and adds them to a juce::KnownPluginList, on a background thread.

    Loading a plug-in to find out what's inside runs its code, and some
    plug-ins crash or hang while doing that. So each one is loaded in a
    separate "worker" process: a copy of this app started with a special
    command line (see runWorkerIfRequested). If the worker dies or stops
    answering, only that plug-in is lost: it goes on the list's blacklist, so
    later scans skip it, and the scan carries on with a fresh worker.

    Plug-ins that were scanned before and haven't changed since aren't loaded
    again, so a scan at startup is quick unless something new was installed.

    The list itself (and its blacklist) is owned by the caller, who also
    saves it.
*/
class PluginScanner  : private juce::Thread
{
public:
    PluginScanner (juce::AudioPluginFormatManager&, juce::KnownPluginList&);
    ~PluginScanner() override;

    /** Starts scanning, unless a scan is already running. With no folders,
        each format's standard install locations are searched. With
        rescanEverything, plug-ins already in the list (or on the blacklist)
        are scanned again. Call on the message thread. */
    void start (const juce::StringArray& folders = {}, bool rescanEverything = false);

    /** Where a scan with no folders looks, besides each format's standard
        install locations: extra folders, and formats to leave out. */
    void setSearchOptions (const juce::StringArray& extraFolders, const juce::StringArray& disabledFormats);

    /** Where to remember which files have been scanned (for formats such as
        LV2 that the plug-in list can't check by file). */
    void setRecordFile (const juce::File&);

    /** Stops after the current plug-in. */
    void cancel();

    bool isScanning() const     { return isThreadRunning(); }
    PluginScanStatus getStatus() const;

    /** Called on the message thread when a scan ends (finished or cancelled). */
    std::function<void()> onFinished;

    /** How long a plug-in may take to load before it counts as hung. */
    static constexpr int timeoutMs = 20000;

    //==============================================================================
    /** Call first thing at startup with the app's command line. If this
        process was started as a scan worker, it sets the worker up and
        returns true; the caller should then do nothing else but run the
        message loop (the worker quits by itself when the scan ends). */
    static bool runWorkerIfRequested (const juce::String& commandLine);

private:
    class Worker;
    class Coordinator;

    juce::AudioPluginFormatManager& formats;
    juce::KnownPluginList& list;
    juce::StringArray extraFolders, disabledFormats;

    // What the thread works through: (format, file) pairs, found on the
    // message thread before it starts.
    struct Job { juce::AudioPluginFormat* format; juce::String file; juce::Time modified; };
    std::vector<Job> jobs;
    bool removeMissingAfterwards = false;

    mutable juce::CriticalSection statusLock;
    PluginScanStatus status;

    std::unique_ptr<Coordinator> coordinator;

    // Files scanned successfully, with their modification times (ms).
    // Guarded by statusLock while the thread runs.
    juce::File recordFile;
    std::map<juce::String, juce::int64> record;

    bool isUnchanged (const juce::String& file, juce::Time modified, juce::AudioPluginFormat&) const;
    void saveRecord();

    // Expires when the scanner is deleted, so a finish() posted by the
    // thread can tell whether the scanner still exists.
    std::shared_ptr<bool> alive = std::make_shared<bool> (true);

    void run() override;
    bool scanInWorker (const Job&, juce::OwnedArray<juce::PluginDescription>& found);
    void finish();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginScanner)
};

} // namespace ikaros
