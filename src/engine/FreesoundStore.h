#pragma once

#include "engine/EngineTypes.h"
#include <map>
#include <optional>
#include <set>

namespace ikaros
{

class Preferences;
class SoundLibrary;

//==============================================================================
/** What the loop browser's filters ask Freesound for. */
struct FreesoundFilters
{
    int minBpm = 0, maxBpm = 0;                // 0 leaves that end open
    std::vector<juce::StringArray> anyOf;      // each list: a loop needs at least one of these tags
};

/** One loop found on Freesound. Only CC0 and CC BY sounds are ever listed. */
struct FreesoundSound
{
    juce::String id;               // Freesound's number for it
    juce::String name, user;       // user: who shared it
    juce::StringArray tags;        // lowercase
    juce::String license;          // SPDX identifier: "CC0-1.0" or "CC-BY-4.0" (just "CC-BY" if the version is unknown)
    juce::String pageUrl;          // its page on freesound.org
    juce::String previewUrl;       // the OGG preview we download
    juce::String key;              // e.g. "C min", from Freesound's analysis or the tags (never for drums)
    double bpm = 0.0, duration = 0.0;

    bool attributionRequired() const    { return license.startsWith ("CC-BY"); }

    /** The line shown next to the name: tempo, length and license. */
    juce::String describe() const;
};

//==============================================================================
/**
    Searching Freesound (freesound.org) for loops, listening to them, and
    adding them to the library.

    Freesound's API needs a key that each user gets for free (see
    getApplyUrl), which is kept in the Preferences. With it the app can
    search and download the lossy previews (OGG, about 190 kbps), which is
    what's used here: the original files need the user to sign in to
    Freesound from the app (OAuth2), which isn't built.

    Only CC0 and CC BY loops are searched for, and the license is checked
    again on every result. A loop that's added is saved in a sound pack of
    its own, "Freesound Loops" (id packId) in the user's Sounds folder, like
    any other loops, and its author, license and address go into that
    pack's credits, so the Acknowledgements page lists it.

    Searching and downloading run in the background. Every method is for the
    message thread. Listeners hear whenever something shown in the window
    changes.
*/
class FreesoundStore  : public juce::ChangeBroadcaster
{
public:
    FreesoundStore (SoundLibrary&, Preferences&);
    ~FreesoundStore() override;

    static constexpr const char* packId = "freesound.loops";

    /** Where users apply for a key, and where they sign up first. */
    static juce::String getApplyUrl();
    static juce::String getRegisterUrl();

    /** Called after a loop was added, so the library can be rescanned. */
    std::function<void()> onLibraryChanged;

    /** Called when a loop the user asked to hear is ready to play. */
    std::function<void (const juce::File&)> onPreviewReady;

    //==============================================================================
    // The key

    bool hasKey() const;

    /** Forgets the key (and the search results). */
    void forgetKey();

    enum class KeyCheck { none, checking, accepted, rejected, failed };

    /** Tries a key with Freesound, and keeps it if Freesound accepts it. */
    void checkKey (const juce::String& key);
    KeyCheck getKeyCheck() const noexcept       { return keyCheck; }

    /** Why the last key check or search failed, for the user. */
    const juce::String& getMessage() const noexcept     { return message; }

    //==============================================================================
    // Searching

    enum class Status { idle, searching, ready, failed };

    /** Searches for loops (an empty search lists the most downloaded). */
    void search (const juce::String& text, const FreesoundFilters& filters = {});

    /** Adds the next page of results to the list. */
    void loadMore();

    Status getStatus() const noexcept                               { return status; }
    const std::vector<FreesoundSound>& getResults() const noexcept  { return results; }
    bool canLoadMore() const noexcept                               { return nextPage.isNotEmpty() && status == Status::ready; }
    std::optional<FreesoundSound> findResult (const juce::String& soundId) const;

    /** True when the last search failed because Freesound refused the key. */
    bool keyWasRefused() const noexcept     { return keyRefused; }

    //==============================================================================
    // Listening and adding

    /** Downloads the loop's preview if needed, then calls onPreviewReady. */
    void preview (const FreesoundSound&);

    /** Saves the loop in the library (and credits it). */
    void addToLibrary (const FreesoundSound&);

    /** The file for a loop's preview: the library's copy if it was added,
        else the downloaded preview (which may not exist yet). */
    juce::File getFileFor (const juce::String& soundId) const;

    bool isAdded (const juce::String& soundId) const;

    /** The Freesound number of a library loop that came from here, or an empty string. */
    juce::String findAddedId (const juce::File&) const;

    /** Deletes a loop that was added, and its credit. Returns false if it couldn't. */
    bool removeFromLibrary (const juce::String& soundId);

    /** True while its preview or library copy is downloading. */
    bool isBusy (const juce::String& soundId) const     { return busy.count (soundId) > 0; }

    /** A problem with the last download, cleared by the next one. */
    const juce::String& getNotice() const noexcept      { return notice; }

private:
    class Job;
    using Finish = std::function<void (FreesoundStore&)>;

    SoundLibrary& sounds;
    Preferences& preferences;

    KeyCheck keyCheck = KeyCheck::none;
    Status status = Status::idle;
    bool keyRefused = false;
    juce::String message, notice, nextPage;
    std::vector<FreesoundSound> results;
    int searchNumber = 0;          // answers to older searches are dropped

    std::set<juce::String> busy;
    juce::String wantedPreview;    // the loop the user clicked last
    std::map<juce::String, juce::File> added;   // sound id -> its file in the library

    JUCE_DECLARE_WEAK_REFERENCEABLE (FreesoundStore)

    void runSearch (const juce::URL&, bool append);
    void setKey (const juce::String&);
    juce::File getPackFolder() const;
    juce::File getCacheFolder() const;
    void loadAdded();
    void recordInPack (const FreesoundSound&, const juce::String& fileName);
    void downloadFinished (const FreesoundSound&, const juce::File& file, const juce::String& error, bool toLibrary);

    // Declared last so it's destroyed first, while what the jobs use still exists.
    juce::ThreadPool pool { juce::ThreadPoolOptions().withThreadName ("Freesound").withNumberOfThreads (2) };

    JUCE_DECLARE_NON_COPYABLE (FreesoundStore)
};

} // namespace ikaros
