#include "engine/PackStore.h"
#include "engine/SoundLibrary.h"
#include <juce_cryptography/juce_cryptography.h>
#include <set>

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

    // The library tile a Nota registry pack belongs under, from its tags (first match wins).
    juce::String categoryFromTags (const juce::var& tags)
    {
        struct Rule { const char* category; const char* words; };
        static const Rule rules[] {
            { "Drums",     "drum drums percussion kit cymbal cymbals snare steel-drum" },
            { "Bass",      "bass double-bass upright-bass" },
            { "Keyboards", "piano keys keyboard organ harpsichord rhodes electric-piano" },
            { "Guitar",    "guitar ukulele banjo mandolin" },
            { "Synths",    "synth synthesizer pad lead" },
            { "Strings",   "strings violin viola cello orchestra harp" },
            { "Woodwinds", "woodwind woodwinds wind flute saxophone sax clarinet oboe bassoon recorder pipes" },
            { "Brass",     "brass trumpet trombone tuba horn french-horn" },
        };

        juce::StringArray have;

        if (auto* list = tags.getArray())
            for (auto& t : *list)
                have.add (t.toString().toLowerCase());

        for (auto& rule : rules)
            for (auto& word : juce::StringArray::fromTokens (rule.words, " ", {}))
                if (have.contains (word))
                    return rule.category;

        return "Other";
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
                auto address = p["url"].toString();

                // A Nota registry entry: the pack's zip is the author's own SFZ library.
                const auto versions = p["versions"];
                const auto newest = versions.size() > 0 ? versions[0] : juce::var();
                const auto asset = newest["asset"];

                if (address.isEmpty() && asset.isObject())
                {
                    const auto kind = p["kind"].toString();
                    const auto archive = asset["archive"].toString();
                    address = asset["url"].toString();

                    // Only SFZ instrument libraries, in zips (the loop and mixed packs aren't supported).
                    if ((kind != "multisample" && kind != "one-shots")
                         || ! (archive == "zip" || (archive.isEmpty() && address.endsWithIgnoreCase (".zip"))))
                        continue;

                    l.rawArchive = true;
                    l.info.id = "nota." + p["id"].toString().toLowerCase();
                    l.info.category = categoryFromTags (p["tags"]);
                    l.info.availableVersion = newest["version"].toString();
                    l.info.downloadSize = (juce::int64) asset["size"];
                    l.info.installedSize = (juce::int64) asset["unpackedSize"];
                    l.sha256 = asset["sha256"].toString().toLowerCase();
                    l.source = p["source"].toString();
                    l.attribution = p["attribution"].toString();
                }

                l.url = address.contains (":") || address.startsWith ("/") ? toUrl (address)
                                                                           : url.getParentURL().getChildURL (address);

                // Leave out entries we couldn't install safely.
                if (SoundLibrary::isValidPackId (l.info.id) && l.info.name.isNotEmpty()
                     && address.isNotEmpty() && isSha256 (l.sha256))
                    listings.push_back (std::move (l));
            }
        }

        // Collections, keeping only packs the index lists (and could install).
        std::vector<CollectionListing> collections;

        if (auto* list = json["collections"].getArray())
        {
            for (auto& c : *list)
            {
                CollectionListing collection { c["id"].toString(), c["name"].toString(), c["description"].toString(), {} };

                if (auto* ids = c["packs"].getArray())
                    for (auto& id : *ids)
                        if (std::any_of (listings.begin(), listings.end(), [&] (const Listing& l) { return l.info.id == id.toString(); }))
                            collection.packIds.push_back (id.toString());

                if (collection.id.isNotEmpty() && collection.name.isNotEmpty() && ! collection.packIds.empty())
                    collections.push_back (std::move (collection));
            }
        }

        const bool loaded = ok && json["packs"].isArray();

        // Back to the message thread with the result. The WeakReference
        // becomes null if the store has been deleted meanwhile.
        juce::MessageManager::callAsync ([store = store, listings = std::move (listings),
                                          collections = std::move (collections), loaded]() mutable
        {
            if (store != nullptr)
                store->indexLoaded (std::move (listings), std::move (collections), loaded);
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
          url (l.url), sha256 (l.sha256), expectedSize (l.info.downloadSize), installFolder (folder), listing (l)
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
    Listing listing;

    // Returns an error to show the user, "cancelled", or empty on success.
    juce::String install()
    {
        // Two downloads can start together on a fresh install: if the other
        // one created the folder first, that's fine too.
        if (! installFolder.createDirectory() && ! installFolder.isDirectory())
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
        if (listing.rawArchive)
            return unpackRaw (packFolder);

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

    // A Nota archive is the author's SFZ library: it goes in the pack's Library
    // folder, and the pack.json and instrument.json files are written here.
    juce::String unpackRaw (juce::File& packFolder)
    {
        juce::ZipFile zip (downloadFile);

        // No absolute paths, no "..", no links (which could point anywhere on the computer).
        for (int i = 0; i < zip.getNumEntries(); ++i)
        {
            auto* entry = zip.getEntry (i);
            const auto name = entry->filename.replaceCharacter ('\\', '/');

            if (entry->isSymbolicLink || name.startsWith ("/") || (name.length() > 1 && name[1] == ':')
                 || name.contains ("../") || name.endsWith (".."))
                return "This pack's download isn't laid out the way it should be.";
        }

        stagingFolder.deleteRecursively();
        packFolder = stagingFolder.getChildFile (juce::File::createLegalFileName (listing.info.name));
        auto library = packFolder.getChildFile ("Library");

        if (zip.getNumEntries() == 0 || ! library.createDirectory())
            return "This pack's download is empty.";

        if (shouldExit())
            return "cancelled";

        if (auto result = zip.uncompressTo (library); result.failed())
            return "Couldn't unpack the pack: " + result.getErrorMessage();

        return writePackFiles (packFolder, library);
    }

    // Every file in the library with one of the extensions (";"-separated), ignoring case and macOS's resource-fork copies.
    static juce::Array<juce::File> findFilesWithExtension (const juce::File& library, const juce::String& extensions)
    {
        juce::Array<juce::File> found;

        for (auto& f : library.findChildFiles (juce::File::findFiles | juce::File::ignoreHiddenFiles, true))
            if (f.hasFileExtension (extensions) && ! f.getFullPathName().contains ("/__MACOSX/"))
                found.add (f);

        return found;
    }

    // A kit of one-shots: the first sample of each folder (or every sample, if it's all one folder) on
    // consecutive keys from C2, played as they are. Returns no file if there's no audio.
    static juce::File writeKit (const juce::File& library)
    {
        auto audio = findFilesWithExtension (library, "wav;flac;mp3");
        audio.sort();

        std::map<juce::String, juce::File> firstInFolder;

        for (auto& f : audio)
            firstInFolder.emplace (f.getParentDirectory().getFullPathName(), f);

        std::vector<juce::File> picks;

        if (firstInFolder.size() > 1)
            for (auto& entry : firstInFolder)
                picks.push_back (entry.second);
        else
            picks.assign (audio.begin(), audio.end());

        if (picks.empty())
            return {};

        constexpr int firstKey = 36, lastKey = 127;
        juce::String sfz ("<group> loop_mode=one_shot\n");

        for (int i = 0; i < (int) picks.size() && firstKey + i <= lastKey; ++i)
            sfz << "<region> key=" << (firstKey + i) << " sample=" << picks[(size_t) i].getRelativePathFrom (library).replaceCharacter ('\\', '/') << "\n";

        auto kit = library.getChildFile ("kit.sfz");
        return kit.replaceWithText (sfz) ? kit : juce::File();
    }

    // One instrument per SFZ program: the SFZ files no other SFZ file includes.
    // A pack with no SFZ files (loose one-shots) gets a kit that maps one sample per key.
    juce::String writePackFiles (const juce::File& packFolder, const juce::File& library)
    {
        auto sfzFiles = findFilesWithExtension (library, "sfz");
        sfzFiles.sort();

        // Include paths are relative to the program that includes them (not the fragment holding the
        // line), so a file counts as included if its path ends with one.
        std::set<juce::String> includePaths;

        for (auto& f : sfzFiles)
            for (auto& line : juce::StringArray::fromLines (f.loadFileAsString()))
                if (auto text = line.trim(); text.startsWith ("#include"))
                    if (auto path = text.fromFirstOccurrenceOf ("\"", false, false).upToFirstOccurrenceOf ("\"", false, false); path.isNotEmpty())
                        includePaths.insert (path.replaceCharacter ('\\', '/'));

        juce::Array<juce::File> programs;

        for (auto& f : sfzFiles)
        {
            const auto relative = f.getRelativePathFrom (library).replaceCharacter ('\\', '/');
            const bool included = std::any_of (includePaths.begin(), includePaths.end(), [&] (const juce::String& p)
            {
                return relative == p || relative.endsWith ("/" + p);
            });

            if (! included)
                programs.add (f);
        }

        if (programs.isEmpty())
            if (auto kit = writeKit (library); kit.existsAsFile())
                programs.add (kit);

        if (programs.isEmpty())
            return "This pack has no instruments Ikaros Studio can play.";

        std::set<juce::String> usedFolders;

        for (auto& program : programs)
        {
            // A pack with one program is named for the pack; otherwise each is named for its file.
            const auto name = programs.size() == 1 ? listing.info.name : program.getFileNameWithoutExtension();
            auto folderName = juce::File::createLegalFileName (name);

            if (folderName.isEmpty())
                folderName = "Instrument";

            const auto base = folderName;

            for (int n = 2; ! usedFolders.insert (folderName.toLowerCase()).second; ++n)
                folderName = base + " " + juce::String (n);

            auto info = std::make_unique<juce::DynamicObject>();
            info->setProperty ("name", name);
            info->setProperty ("category", listing.info.category);
            info->setProperty ("sfz", "../../Library/" + program.getRelativePathFrom (library).replaceCharacter ('\\', '/'));

            auto folder = packFolder.getChildFile ("Instruments").getChildFile (folderName);

            if (! folder.createDirectory()
                 || ! folder.getChildFile ("instrument.json").replaceWithText (juce::JSON::toString (juce::var (info.release()))))
                return "Couldn't save the pack in " + installFolder.getFullPathName() + ".";
        }

        const auto license = listing.info.license;
        auto credit = std::make_unique<juce::DynamicObject>();
        credit->setProperty ("what", listing.info.name);
        credit->setProperty ("who", listing.info.author);
        credit->setProperty ("license", license);
        credit->setProperty ("url", listing.source);
        credit->setProperty ("attributionRequired", license.startsWith ("CC-BY") || license == "MIT" || listing.attribution.isNotEmpty());

        auto pack = std::make_unique<juce::DynamicObject>();
        pack->setProperty ("format", 2);
        pack->setProperty ("id", packId);
        pack->setProperty ("name", listing.info.name);
        pack->setProperty ("version", listing.info.availableVersion.isNotEmpty() ? listing.info.availableVersion : "1.0.0");
        pack->setProperty ("description", listing.info.description);
        pack->setProperty ("author", listing.info.author);
        pack->setProperty ("license", license);
        pack->setProperty ("installedSize", listing.info.installedSize);
        pack->setProperty ("credits", juce::Array<juce::var> { juce::var (credit.release()) });

        if (! packFolder.getChildFile ("pack.json").replaceWithText (juce::JSON::toString (juce::var (pack.release()))))
            return "Couldn't save the pack in " + installFolder.getFullPathName() + ".";

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
    : sounds (s), indexUrl (getDefaultIndexUrl()), installFolder (s.getUserFolder())
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
    // A build with no index address has nothing to offer (not "offline").
    if (indexUrl.isEmpty())
    {
        indexLoaded ({}, {}, true);
        return;
    }

    indexStatus = IndexStatus::loading;
    pool.addJob (new IndexJob (*this, toUrl (indexUrl)), true);   // true: the pool deletes the job when it's done
    sendChangeMessage();
}

void PackStore::indexLoaded (std::vector<Listing> newListings, std::vector<CollectionListing> newCollections, bool ok)
{
    indexStatus = ok ? IndexStatus::loaded : IndexStatus::unavailable;
    listings = std::move (newListings);
    collections = std::move (newCollections);
    sendChangeMessage();
}

//==============================================================================
std::vector<StoreCollection> PackStore::getCollections() const
{
    std::vector<StoreCollection> result;
    const auto packs = getPacks();

    for (auto& c : collections)
    {
        StoreCollection info { c.id, c.name, c.description, c.packIds };

        for (auto& id : c.packIds)
        {
            auto p = std::find_if (packs.begin(), packs.end(), [&] (const StorePack& sp) { return sp.id == id; });

            if (p == packs.end())
                continue;

            if (p->state == StorePack::State::downloading || p->state == StorePack::State::installing)
            {
                ++info.packsDownloading;
            }
            else if (p->state == StorePack::State::notInstalled || p->state == StorePack::State::updateAvailable)
            {
                ++info.packsToInstall;
                info.downloadSize += p->downloadSize;
            }
        }

        info.allInstalled = info.packsToInstall == 0 && info.packsDownloading == 0;
        result.push_back (std::move (info));
    }

    return result;
}

std::optional<StoreCollection> PackStore::findCollection (const juce::String& id) const
{
    for (auto& c : getCollections())
        if (c.id == id)
            return c;

    return std::nullopt;
}

void PackStore::installCollection (const juce::String& id)
{
    auto collection = findCollection (id);

    if (! collection.has_value())
        return;

    for (auto& packId : collection->packIds)
        if (auto p = findPack (packId); p.has_value()
             && (p->state == StorePack::State::notInstalled || p->state == StorePack::State::updateAvailable))
            install (packId);
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
