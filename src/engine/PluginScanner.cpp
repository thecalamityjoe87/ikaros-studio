#include "engine/PluginScanner.h"
#include <chrono>
#include <cstdlib>
#include <set>
#include <thread>

namespace ikaros
{

namespace
{
    // The worker process is this app, started with "--IkarosPluginScan:<pipe>".
    // JUCE's ChildProcessCoordinator/Worker add and parse that argument.
    const juce::String workerCommandLineID ("IkarosPluginScan");

    juce::MemoryBlock toMessage (const juce::XmlElement& xml)
    {
        juce::MemoryOutputStream out;
        xml.writeTo (out, juce::XmlElement::TextFormat().withoutHeader().singleLine());
        return out.getMemoryBlock();
    }
}

//==============================================================================
// Runs in the worker process. Receives <SCAN format="VST3" file="..."/>,
// loads that file's plug-ins and answers <FOUND> with their descriptions.
class PluginScanner::Worker  : public juce::ChildProcessWorker
{
public:
    Worker()
    {
        // The same formats as the app (whichever JUCE_PLUGINHOST_* are on).
        juce::addDefaultFormatsToManager (formats);
    }

private:
    juce::AudioPluginFormatManager formats;

    // Called on the connection's own thread. Plug-ins must be loaded on the
    // message thread, so the scan is posted there.
    void handleMessageFromCoordinator (const juce::MemoryBlock& message) override
    {
        // shared_ptr because std::function (which callAsync takes) must be copyable.
        std::shared_ptr<juce::XmlElement> xml = juce::parseXML (message.toString());

        if (xml != nullptr && xml->hasTagName ("SCAN"))
            juce::MessageManager::callAsync ([this, xml] { scan (*xml); });
    }

    void scan (const juce::XmlElement& request)
    {
        juce::XmlElement reply ("FOUND");
        reply.setAttribute ("id", request.getIntAttribute ("id"));

        for (auto* format : formats.getFormats())
        {
            if (format->getName() == request.getStringAttribute ("format"))
            {
                juce::OwnedArray<juce::PluginDescription> found;
                format->findAllTypesForFile (found, request.getStringAttribute ("file"));

                for (auto* desc : found)
                    reply.addChildElement (desc->createXml().release());
            }
        }

        sendMessageToCoordinator (toMessage (reply));
    }

    // The app closed the connection (scan over, or it gave up on a plug-in
    // that hung) or went away: quit. If a hung plug-in is blocking the
    // message thread, the polite quit never runs, so a separate thread ends
    // the process for sure shortly after. (JUCE's coordinator only asks the
    // worker to quit; it doesn't kill it.)
    void handleConnectionLost() override
    {
        juce::MessageManager::callAsync ([] { juce::MessageManager::getInstance()->stopDispatchLoop(); });

        std::thread ([]
        {
            std::this_thread::sleep_for (std::chrono::seconds (2));
            std::_Exit (0);   // no clean-up: the process may be stuck inside a plug-in
        }).detach();
    }
};

bool PluginScanner::runWorkerIfRequested (const juce::String& commandLine)
{
    // Lives until the process exits. (A function-local static is created the
    // first time this line runs, and destroyed at exit.)
    static std::unique_ptr<Worker> worker;

    auto w = std::make_unique<Worker>();

    if (! w->initialiseFromCommandLine (commandLine, workerCommandLineID))
        return false;

    worker = std::move (w);
    return true;
}

//==============================================================================
// Runs in the app, on the scan thread: starts the worker process and asks it
// to scan one file at a time.
class PluginScanner::Coordinator  : private juce::ChildProcessCoordinator
{
public:
    ~Coordinator() override   { killWorkerProcess(); }

    enum class Result { found, failed, cancelled };

    Result scan (const juce::String& formatName, const juce::String& file,
                 juce::OwnedArray<juce::PluginDescription>& found, juce::Thread& thread)
    {
        if (! launched)
        {
            launched = launchWorkerProcess (juce::File::getSpecialLocation (juce::File::currentExecutableFile),
                                            workerCommandLineID, 0, 0);
            if (! launched)
                return Result::failed;
        }

        juce::XmlElement request ("SCAN");
        request.setAttribute ("id", ++lastRequestID);
        request.setAttribute ("format", formatName);
        request.setAttribute ("file", file);

        {
            const juce::ScopedLock sl (lock);
            reply.reset();
        }

        if (! sendMessageToWorker (toMessage (request)))
            return Result::failed;

        const auto started = juce::Time::getMillisecondCounter();

        for (;;)
        {
            replyArrived.wait (100);

            if (thread.threadShouldExit())
                return Result::cancelled;

            {
                const juce::ScopedLock sl (lock);

                if (reply != nullptr)
                {
                    for (auto* e : reply->getChildIterator())
                    {
                        auto desc = std::make_unique<juce::PluginDescription>();

                        if (desc->loadFromXml (*e))
                            found.add (desc.release());
                    }

                    return Result::found;
                }
            }

            // The worker crashed, or this plug-in has been loading for too long.
            if (connectionLost || juce::Time::getMillisecondCounter() - started > (juce::uint32) timeoutMs)
                return Result::failed;
        }
    }

private:
    juce::CriticalSection lock;
    std::unique_ptr<juce::XmlElement> reply;
    juce::WaitableEvent replyArrived;
    std::atomic<bool> connectionLost { false };
    bool launched = false;
    int lastRequestID = 0;

    // Both are called on the connection's thread.
    void handleMessageFromWorker (const juce::MemoryBlock& message) override
    {
        auto xml = juce::parseXML (message.toString());

        if (xml != nullptr && xml->hasTagName ("FOUND") && xml->getIntAttribute ("id") == lastRequestID)
        {
            const juce::ScopedLock sl (lock);
            reply = std::move (xml);
            replyArrived.signal();
        }
    }

    void handleConnectionLost() override
    {
        connectionLost = true;
        replyArrived.signal();
    }
};

//==============================================================================
PluginScanner::PluginScanner (juce::AudioPluginFormatManager& f, juce::KnownPluginList& l)
    : juce::Thread ("Ikaros plug-in scan"), formats (f), list (l)
{
}

PluginScanner::~PluginScanner()
{
    onFinished = nullptr;
    stopThread (timeoutMs + 2000);
}

void PluginScanner::setRecordFile (const juce::File& file)
{
    recordFile = file;
    record.clear();

    if (auto xml = juce::parseXML (recordFile))
        for (auto* e : xml->getChildWithTagNameIterator ("FILE"))
            record[e->getStringAttribute ("path")] = e->getStringAttribute ("modified").getLargeIntValue();
}

bool PluginScanner::isUnchanged (const juce::String& file, juce::Time modified, juce::AudioPluginFormat& format) const
{
    // Most formats list plug-ins by file, so the list itself knows. LV2 lists
    // them by URI, so we keep our own record of the bundles we've scanned.
    if (list.isListingUpToDate (file, format))
        return true;

    auto found = record.find (file);
    return found != record.end() && found->second == modified.toMilliseconds();
}

void PluginScanner::saveRecord()
{
    if (recordFile == juce::File())
        return;

    juce::XmlElement xml ("SCANNEDPLUGINFILES");

    for (auto& [path, modified] : record)
    {
        auto* e = xml.createNewChildElement ("FILE");
        e->setAttribute ("path", path);
        e->setAttribute ("modified", juce::String (modified));
    }

    xml.writeTo (recordFile);
}

void PluginScanner::start (const juce::StringArray& folders, bool rescanEverything)
{
    if (isThreadRunning())
        return;

    jobs.clear();
    removeMissingAfterwards = folders.isEmpty();
    const auto blacklisted = list.getBlacklistedFiles();
    std::set<juce::String> seen;   // some formats list a bundle once per plug-in inside it

    for (auto* format : formats.getFormats())
    {
        if (! format->canScanForPlugins())
            continue;

        auto path = folders.isEmpty() ? format->getDefaultLocationsToSearch()
                                      : juce::FileSearchPath (folders.joinIntoString (";"));

        for (auto& file : format->searchPathsForPlugins (path, true, false))
        {
            if (! seen.insert (format->getName() + ":" + file).second)
                continue;

            // Plug-ins are files or bundle folders (or, for Audio Units, IDs).
            const auto modified = juce::File::isAbsolutePath (file) ? juce::File (file).getLastModificationTime()
                                                                    : juce::Time();

            if (! rescanEverything && (blacklisted.contains (file) || isUnchanged (file, modified, *format)))
                continue;

            jobs.push_back ({ format, file, modified });
        }
    }

    {
        const juce::ScopedLock sl (statusLock);
        status = {};
        status.scanning = true;
        status.total = (int) jobs.size();
    }

    startThread (juce::Thread::Priority::low);
}

void PluginScanner::cancel()
{
    signalThreadShouldExit();
}

PluginScanStatus PluginScanner::getStatus() const
{
    const juce::ScopedLock sl (statusLock);
    return status;
}

void PluginScanner::run()
{
    for (auto& job : jobs)
    {
        if (threadShouldExit())
            break;

        const auto name = juce::File::isAbsolutePath (job.file) ? juce::File (job.file).getFileNameWithoutExtension()
                                                                : job.file;
        {
            const juce::ScopedLock sl (statusLock);
            status.current = name;
        }

        juce::OwnedArray<juce::PluginDescription> found;
        const bool ok = scanInWorker (job, found);

        if (threadShouldExit())
            break;   // cancelled half-way: don't blame the plug-in

        // KnownPluginList has its own lock, so it's safe to add from here.
        // It broadcasts a change, which Tracktion uses to save the list.
        if (ok)
        {
            for (auto* desc : found)
            {
                // What JUCE's own scanner fills in, so the list can tell
                // later whether the file has changed.
                desc->lastFileModTime = job.modified;
                desc->lastInfoUpdateTime = juce::Time::getCurrentTime();

                // JUCE recognises LV2 instruments from the LV2 spec files,
                // which aren't always installed. A plug-in that makes sound
                // without taking any in is an instrument.
                if (desc->pluginFormatName == "LV2" && ! desc->isInstrument
                     && desc->numInputChannels == 0 && desc->numOutputChannels > 0)
                    desc->isInstrument = true;

                list.addType (*desc);
            }

            list.removeFromBlacklist (job.file);
        }
        else
        {
            list.addToBlacklist (job.file);
        }

        const juce::ScopedLock sl (statusLock);

        if (ok)
            record[job.file] = job.modified.toMilliseconds();

        ++status.done;
        status.numFound += found.size();

        if (! ok)
            status.failed.add (name);
    }

    coordinator.reset();   // ends the worker process

    // Finish on the message thread. SafePointer-style guard: if the scanner is
    // deleted first, its destructor waits for this thread, and onFinished is
    // cleared, so the posted call finds nothing to do.
    juce::MessageManager::callAsync ([this, guard = std::weak_ptr<bool> (alive)]
    {
        if (! guard.expired())
            finish();
    });
}

bool PluginScanner::scanInWorker (const Job& job, juce::OwnedArray<juce::PluginDescription>& found)
{
    if (coordinator == nullptr)
        coordinator = std::make_unique<Coordinator>();

    const auto result = coordinator->scan (job.format->getName(), job.file, found, *this);

    if (result == Coordinator::Result::found)
        return true;

    // Crashed, hung or cancelled: start a fresh worker for the next one.
    coordinator.reset();
    return false;
}

void PluginScanner::finish()
{
    // Forget plug-ins that have been uninstalled (only after a scan of the
    // standard locations, which is where they'd be).
    if (removeMissingAfterwards)
    {
        for (auto& desc : list.getTypes())
            for (auto* format : formats.getFormats())
                if (format->getName() == desc.pluginFormatName && ! format->doesPluginStillExist (desc))
                    list.removeType (desc);
    }

    {
        const juce::ScopedLock sl (statusLock);
        status.scanning = false;
        status.current = {};
        saveRecord();
    }

    if (onFinished)
        onFinished();
}

} // namespace ikaros
