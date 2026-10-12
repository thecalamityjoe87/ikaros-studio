#pragma once

#include "engine/EngineTypes.h"

namespace ikaros
{

class SoundLibrary;

//==============================================================================
/**
    Downloadable sound packs, like GarageBand's Sound Library: the app comes
    with one small pack, and the user downloads the ones they want.

    The packs on offer are listed in a pack index, a JSON file on the web
    (see getDefaultIndexUrl, set at build time with IKAROS_PACK_INDEX_URL):

        { "format": 1,
          "packs": [ { "id": "ikaros-studio.drums", "name": "Drums", "version": "1.0.0",
                       "category": "Drums", "description": "...",
                       "author": "...", "license": "CC0-1.0",
                       "downloadSize": 52428800, "installedSize": 73400320,
                       "url": "Drums-1.0.0.zip", "sha256": "<64 hex digits>" } ] }

    "url" may be relative to the index's own address. Each download is a zip
    holding one pack folder (see SoundLibrary for what's in it).
    tools/make_pack.py makes the zip and its index entry.

    Downloads run in the background: download, check the
    SHA-256, unpack into a hidden folder next to the installed packs, then
    swap it in (replacing an older version). The library is then rescanned
    (see onPacksChanged). Nothing here touches Tracktion.

    Every method is for the message thread. Listeners (ChangeListener) hear
    whenever anything shown in the window changes, progress included.
*/
class PackStore  : public juce::ChangeBroadcaster
{
public:
    explicit PackStore (SoundLibrary&);
    ~PackStore() override;

    /** Called on the message thread after a pack was installed, updated or
        removed (AudioEngine rescans the library and reconnects tracks). */
    std::function<void (const juce::String& packId)> onPacksChanged;

    //==============================================================================
    // The index

    /** The index URL the app was built with. The IKAROS_PACK_INDEX
        environment variable overrides it (for testing a new index). */
    static juce::String getDefaultIndexUrl();

    /** Uses another index (the smoke test's). Call refreshIndex() after. */
    void setIndexUrl (const juce::String& url)     { indexUrl = url; }

    /** Where downloaded packs go (the user's Sounds folder unless the test
        says otherwise). It must be one of the folders SoundLibrary reads. */
    void setInstallFolder (const juce::File& folder)   { installFolder = folder; }
    juce::File getInstallFolder() const                { return installFolder; }

    /** Fetches the index again, in the background. */
    void refreshIndex();

    enum class IndexStatus { notLoaded, loading, loaded, unavailable };
    IndexStatus getIndexStatus() const noexcept     { return indexStatus; }

    //==============================================================================
    // The packs

    /** Every pack in the index and every installed pack, by name. */
    std::vector<StorePack> getPacks() const;
    std::optional<StorePack> findPack (const juce::String& id) const;

    /** Downloads and installs (or updates) a pack from the index. */
    void install (const juce::String& id);

    /** Stops a download (nothing is installed). */
    void cancel (const juce::String& id);

    /** True while a download is under way or waiting its turn. */
    bool isBusy() const;

    /** Deletes an installed pack. Packs that come with the app can't be
        removed. Returns false if it couldn't. */
    bool remove (const juce::String& id);

private:
    struct Listing
    {
        StorePack info;            // id, name, description, category, author, license, sizes, availableVersion
        juce::URL url;
        juce::String sha256;       // lowercase hex
    };

    class IndexJob;
    class InstallJob;

    SoundLibrary& sounds;
    juce::String indexUrl;
    juce::File installFolder;
    IndexStatus indexStatus = IndexStatus::notLoaded;
    std::vector<Listing> listings;

    // Per-pack download state, keyed by pack id. Jobs update progress from
    // their thread, so it's guarded by the lock.
    struct Download { StorePack::State state; float progress; juce::String error; };
    std::map<juce::String, Download> downloads;
    mutable juce::CriticalSection downloadLock;

    // Lets jobs check, from the message thread, that the store still exists.
    JUCE_DECLARE_WEAK_REFERENCEABLE (PackStore)

    // Background work: the index, and downloads (two can run at once).
    // Declared last so it's destroyed first (members are destroyed in
    // reverse order), while what the jobs use still exists.
    juce::ThreadPool pool { juce::ThreadPoolOptions().withThreadName ("Pack store").withNumberOfThreads (3) };

    void indexLoaded (std::vector<Listing>, bool ok);
    void setDownload (const juce::String& id, StorePack::State, float progress, const juce::String& error = {});
    void downloadFinished (const juce::String& id, const juce::String& error);
    const Listing* findListing (const juce::String& id) const;

    JUCE_DECLARE_NON_COPYABLE (PackStore)
};

} // namespace ikaros
