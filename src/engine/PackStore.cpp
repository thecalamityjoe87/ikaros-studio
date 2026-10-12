#include "engine/PackStore.h"
#include "engine/SoundLibrary.h"
#include <juce_cryptography/juce_cryptography.h>

namespace ikaros
{

namespace
{
    // An address from the index or the build: a web address, a file:// URL,
    // or a plain path to a local file.
    juce::URL toUrl (const juce::String& text)
    {
        if (text.startsWithIgnoreCase ("http://") || text.startsWithIgnoreCase ("https://")
             || text.startsWithIgnoreCase ("file:"))
            return juce::URL (text);

        return juce::URL (juce::File (text));
    }

    bool isSha256 (const juce::String& s)
    {
        return s.length() == 64 && s.containsOnly ("0123456789abcdef");
    }

    juce::String readPackId (const juce::File& packFolder)
    {
        return juce::JSON::parse (packFolder.getChildFile ("pack.json"))["id"].toString();
    }

    constexpr int connectionTimeoutMs = 15000;
}

//==============================================================================
// Fetches and reads the pack index.
class PackStore::IndexJob  : public juce::ThreadPoolJob
{
public:
    IndexJob (PackStore& s, juce::URL u)
        : juce::ThreadPoolJob ("Pack index"), store (&s), url (std::move (u)) {}

    JobStatus runJob() override
    {
        std::vector<Listing> listings;
        int status = 0;

        // ParameterHandling::inAddress: a plain GET. (Local files have no
        // status code, so 0 counts as fine too.)
        auto in = url.createInputStream (juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                                             .withConnectionTimeoutMs (connectionTimeoutMs)
                                             .withStatusCode (&status));

        const bool ok = in != nullptr && (status == 0 || status == 200);
        auto json = ok ? juce::JSON::parse (in->readEntireStreamAsString()) : juce::var();

        if (auto* packs = json["packs"].getArray())
        {
            for (auto& p : *packs)
            {
                Listing l;
                l.info.id = p["id"].toString();
                l.info.name = p["name"].toString();
                l.info.description = p["description"].toString();
                l.info.category = p["category"].toString();
                l.info.author = p["author"].toString();
                l.info.license = p["license"].toString();
                l.info.availableVersion = p["version"].toString();
                l.info.downloadSize = (juce::int64) p["downloadSize"];
                l.info.installedSize = (juce::int64) p["installedSize"];
                l.sha256 = p["sha256"].toString().toLowerCase();

                // A relative address is next to the index.
                const auto address = p["url"].toString();
                l.url = address.contains (":") || address.startsWith ("/") ? toUrl (address)
                                                                           : url.getParentURL().getChildURL (address);

                // Leave out entries we couldn't install safely.
                if (SoundLibrary::isValidPackId (l.info.id) && l.info.name.isNotEmpty()
                     && address.isNotEmpty() && isSha256 (l.sha256))
                    listings.push_back (std::move (l));
            }
        }

        const bool loaded = ok && json["packs"].isArray();

        // Back to the message thread with the result. The WeakReference
        // becomes null if the store has been deleted meanwhile.
        juce::MessageManager::callAsync ([store = store, listings = std::move (listings), loaded]() mutable
        {
            if (store != nullptr)
                store->indexLoaded (std::move (listings), loaded);
        });

        return jobHasFinished;
    }

private:
    juce::WeakReference<PackStore> store;
    juce::URL url;
};

//==============================================================================
// Downloads, checks and installs one pack.
class PackStore::InstallJob  : public juce::ThreadPoolJob
{
public:
    InstallJob (PackStore& s, const Listing& l, const juce::File& folder)
        : juce::ThreadPoolJob ("Install " + l.info.id), packId (l.info.id), store (s), weakStore (&s),
          url (l.url), sha256 (l.sha256), expectedSize (l.info.downloadSize), installFolder (folder)
    {
    }

    const juce::String packId;

    JobStatus runJob() override
    {
        const auto error = install();

        // Tidy up whatever's left, whether it worked or not.
        downloadFile.deleteFile();
        stagingFolder.deleteRecursively();

        juce::MessageManager::callAsync ([store = weakStore, id = packId, error]
        {
            if (store != nullptr)
                store->downloadFinished (id, error);
        });

        return jobHasFinished;
    }

private:
    PackStore& store;                        // for progress (setDownload is thread-safe)
    juce::WeakReference<PackStore> weakStore;
    juce::URL url;
    juce::String sha256;
    juce::int64 expectedSize;
    juce::File installFolder, downloadFile, stagingFolder;

    // Returns an error to show the user, "cancelled", or empty on success.
    juce::String install()
    {
        if (! installFolder.createDirectory())
            return "Couldn't create the Sounds folder (" + installFolder.getFullPathName() + ").";

        // Hidden names, so the library never mistakes these for packs.
        downloadFile = installFolder.getChildFile (".download-" + packId + ".zip");
        stagingFolder = installFolder.getChildFile (".installing-" + packId);

        if (auto error = download(); error.isNotEmpty())
            return error;

        store.setDownload (packId, StorePack::State::installing, 1.0f);

        // The SHA-256 from the index proves the file is the one the index
        // means: not cut short, damaged or swapped.
        if (juce::SHA256 (downloadFile).toHexString() != sha256)
            return "The download was damaged. Please try again.";

        juce::File packFolder;

        if (auto error = unpack (packFolder); error.isNotEmpty())
            return error;

        return moveIntoPlace (packFolder);
    }

    juce::String download()
    {
        int status = 0;
        auto in = url.createInputStream (juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                                             .withConnectionTimeoutMs (connectionTimeoutMs)
                                             .withStatusCode (&status));

        if (in == nullptr || (status != 0 && status != 200))
            return "Couldn't download it. Check your internet connection and try again.";

        downloadFile.deleteFile();
        juce::FileOutputStream out (downloadFile);

        if (! out.openedOk())
            return "Couldn't save the download in " + installFolder.getFullPathName() + ".";

        const auto total = in->getTotalLength() > 0 ? in->getTotalLength() : expectedSize;
        juce::HeapBlock<char> buffer (65536);
        juce::int64 done = 0;
        float shown = -1.0f;

        while (! in->isExhausted())
        {
            if (shouldExit())
                return "cancelled";

            const auto got = in->read (buffer, 65536);

            if (got < 0)
                return "The download stopped. Check your internet connection and try again.";

            if (got == 0)
                break;

            if (! out.write (buffer, (size_t) got))
                return "Couldn't save the download. Is the disk full?";

            done += got;
            const auto progress = total > 0 ? juce::jlimit (0.0f, 1.0f, (float) done / (float) total) : 0.0f;

            // Only tell the window about whole percents.
            if (progress - shown >= 0.01f)
            {
                shown = progress;
                store.setDownload (packId, StorePack::State::downloading, progress);
            }
        }

        out.flush();
        return out.getStatus().wasOk() ? juce::String() : "Couldn't save the download. Is the disk full?";
    }

    // Unpacks into the hidden staging folder and finds the pack in it.
    juce::String unpack (juce::File& packFolder)
    {
        juce::ZipFile zip (downloadFile);
        juce::String top;

        // Every entry must be inside one folder: no absolute paths, no "..",
        // no links (which could point anywhere on the computer).
        for (int i = 0; i < zip.getNumEntries(); ++i)
        {
            auto* entry = zip.getEntry (i);
            const auto name = entry->filename.replaceCharacter ('\\', '/');
            const auto first = name.upToFirstOccurrenceOf ("/", false, false);

            if (entry->isSymbolicLink || name.startsWith ("/") || name.contains (":")
                 || name.contains ("../") || name.endsWith ("..") || ! name.contains ("/")
                 || first.isEmpty() || first.startsWith (".") || (top.isNotEmpty() && first != top))
                return "This pack's download isn't laid out the way it should be.";

            top = first;
        }

        stagingFolder.deleteRecursively();

        if (top.isEmpty() || ! stagingFolder.createDirectory())
            return "This pack's download is empty.";

        if (shouldExit())
            return "cancelled";

        if (auto result = zip.uncompressTo (stagingFolder); result.failed())
            return "Couldn't unpack the pack: " + result.getErrorMessage();

        packFolder = stagingFolder.getChildFile (top);

        if (readPackId (packFolder) != packId)
            return "This download isn't the pack it should be.";

        return {};
    }

    // Swaps the new pack in, replacing an installed older version.
    juce::String moveIntoPlace (const juce::File& packFolder)
    {
        juce::File old;

        for (auto& f : installFolder.findChildFiles (juce::File::findDirectories | juce::File::ignoreHiddenFiles, false))
            if (readPackId (f) == packId)
                old = f;

        // Move the old version aside first, so a failure can put it back.
        auto aside = installFolder.getChildFile (".old-" + packId);
        aside.deleteRecursively();

        if (old.exists() && ! old.moveFileTo (aside))
            return "Couldn't replace the installed version (is a file in it open?).";

        auto destination = installFolder.getChildFile (packFolder.getFileName());

        if (destination.exists())   // a different pack with the same folder name
            destination = installFolder.getNonexistentChildFile (packFolder.getFileName(), {}, true);

        if (! packFolder.moveFileTo (destination))
        {
            if (old != juce::File())
                aside.moveFileTo (old);

            return "Couldn't install the pack in " + installFolder.getFullPathName() + ".";
        }

        aside.deleteRecursively();
        return {};
    }
};

//==============================================================================
PackStore::PackStore (SoundLibrary& s)
    : sounds (s), indexUrl (getDefaultIndexUrl()), installFolder (SoundLibrary::getUserSoundsFolder())
{
}

PackStore::~PackStore()
{
    // Stop downloads (each tidies up its files), waiting up to 10 seconds.
    pool.removeAllJobs (true, 10000);
}

juce::String PackStore::getDefaultIndexUrl()
{
    const auto fromEnvironment = juce::SystemStats::getEnvironmentVariable ("IKAROS_PACK_INDEX", {});

    if (fromEnvironment.isNotEmpty())
        return fromEnvironment;

    // Set at build time (CMake's IKAROS_PACK_INDEX_URL).
    return IKAROS_PACK_INDEX_URL;
}

void PackStore::refreshIndex()
{
    indexStatus = IndexStatus::loading;
    pool.addJob (new IndexJob (*this, toUrl (indexUrl)), true);   // true: the pool deletes the job when it's done
    sendChangeMessage();
}

void PackStore::indexLoaded (std::vector<Listing> newListings, bool ok)
{
    indexStatus = ok ? IndexStatus::loaded : IndexStatus::unavailable;
    listings = std::move (newListings);
    sendChangeMessage();
}

const PackStore::Listing* PackStore::findListing (const juce::String& id) const
{
    for (auto& l : listings)
        if (l.info.id == id)
            return &l;

    return nullptr;
}

//==============================================================================
std::vector<StorePack> PackStore::getPacks() const
{
    std::vector<StorePack> result;

    for (auto& l : listings)
        result.push_back (l.info);

    for (auto& installed : sounds.getPacks())
    {
        auto it = std::find_if (result.begin(), result.end(), [&] (const StorePack& p) { return p.id == installed.id; });

        if (it == result.end())
        {
            StorePack p;
            p.id = installed.id;
            p.name = installed.name;
            p.description = installed.description;
            p.author = installed.author;
            p.license = installed.license;
            p.installedSize = installed.installedSize;
            result.push_back (p);
            it = std::prev (result.end());
        }

        it->installedVersion = installed.version;
        it->bundled = installed.bundled;
    }

    const juce::ScopedLock sl (downloadLock);

    for (auto& p : result)
    {
        if (auto d = downloads.find (p.id); d != downloads.end())
        {
            p.state = d->second.state;
            p.progress = d->second.progress;
            p.error = d->second.error;

            if (p.state != StorePack::State::downloading && p.state != StorePack::State::installing)
                p.state = StorePack::State::notInstalled;   // (only errors are kept for finished downloads)
            else
                continue;
        }

        if (p.installedVersion.isEmpty())
            p.state = StorePack::State::notInstalled;
        else if (p.availableVersion.isNotEmpty() && SoundLibrary::compareVersions (p.availableVersion, p.installedVersion) > 0)
            p.state = StorePack::State::updateAvailable;
        else
            p.state = StorePack::State::installed;
    }

    std::sort (result.begin(), result.end(), [] (const StorePack& a, const StorePack& b)
    {
        return a.name.compareNatural (b.name) < 0;
    });

    return result;
}

std::optional<StorePack> PackStore::findPack (const juce::String& id) const
{
    for (auto& p : getPacks())
        if (p.id == id)
            return p;

    return std::nullopt;
}

bool PackStore::isBusy() const
{
    const juce::ScopedLock sl (downloadLock);

    for (auto& [id, d] : downloads)
        if (d.state == StorePack::State::downloading || d.state == StorePack::State::installing)
            return true;

    return false;
}

void PackStore::setDownload (const juce::String& id, StorePack::State state, float progress, const juce::String& error)
{
    {
        const juce::ScopedLock sl (downloadLock);
        downloads[id] = { state, progress, error };
    }

    sendChangeMessage();   // safe from any thread: listeners hear on the message thread
}

//==============================================================================
void PackStore::install (const juce::String& id)
{
    auto* listing = findListing (id);

    if (listing == nullptr)
        return;

    if (auto p = findPack (id); p.has_value()
         && (p->bundled || p->state == StorePack::State::downloading || p->state == StorePack::State::installing))
        return;

    setDownload (id, StorePack::State::downloading, 0.0f);
    pool.addJob (new InstallJob (*this, *listing, installFolder), true);
}

void PackStore::cancel (const juce::String& id)
{
    // Stops the pack's job if it's running (it notices, tidies up and
    // reports "cancelled"), or takes it off the queue if it hasn't started.
    // The selector runs while the pool holds its lock, so the job can't
    // finish and be deleted while it's looked at.
    struct ThisPack  : juce::ThreadPool::JobSelector
    {
        explicit ThisPack (const juce::String& i) : id (i) {}

        bool isJobSuitable (juce::ThreadPoolJob* job) override
        {
            auto* install = dynamic_cast<InstallJob*> (job);
            return install != nullptr && install->packId == id;
        }

        const juce::String& id;
    };

    ThisPack selector (id);
    pool.removeAllJobs (true, 0, &selector);

    {
        const juce::ScopedLock sl (downloadLock);
        downloads.erase (id);
    }

    sendChangeMessage();
}

void PackStore::downloadFinished (const juce::String& id, const juce::String& error)
{
    {
        const juce::ScopedLock sl (downloadLock);

        if (error.isEmpty() || error == "cancelled")
            downloads.erase (id);
        else
            downloads[id] = { StorePack::State::notInstalled, 0.0f, error };
    }

    if (error.isEmpty() && onPacksChanged)
        onPacksChanged (id);

    sendChangeMessage();
}

bool PackStore::remove (const juce::String& id)
{
    auto pack = sounds.findPack (id);

    // Only packs in the folder we install into: never the app's own, nor a
    // folder the smoke test added.
    if (! pack.has_value() || pack->bundled || ! pack->folder.isAChildOf (installFolder) || isBusy())
        return false;

    if (! pack->folder.deleteRecursively())
        return false;

    {
        const juce::ScopedLock sl (downloadLock);
        downloads.erase (id);
    }

    if (onPacksChanged)
        onPacksChanged (id);

    sendChangeMessage();
    return true;
}

} // namespace ikaros
