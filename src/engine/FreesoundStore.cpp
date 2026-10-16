#include "engine/FreesoundStore.h"
#include "engine/Preferences.h"
#include "engine/SoundLibrary.h"
#include "platform/AppFolders.h"

namespace ikaros
{

namespace
{
    constexpr int timeoutMs = 15000;
    constexpr juce::int64 maxPreviewBytes = 30 * 1024 * 1024;
    const juce::String apiRoot ("https://freesound.org/apiv2/");
    const juce::String middleDot = juce::String::fromUTF8 (" \xc2\xb7 ");

    // The key goes into a header, so only what a real key is made of gets through.
    bool isPlausibleKey (const juce::String& key)
    {
        return key.length() >= 20 && key.length() <= 80
                && key.containsOnly ("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789");
    }

    // The key is only ever sent to Freesound, and only Freesound's own addresses are followed.
    bool isFreesoundAddress (const juce::String& address)
    {
        if (! address.startsWithIgnoreCase ("https://") || address.contains ("@"))
            return false;

        const auto domain = juce::URL (address).getDomain().toLowerCase();
        return domain == "freesound.org" || domain.endsWith (".freesound.org");
    }

    struct Reply  { int status = 0; juce::var json; };

    Reply getJson (const juce::URL& url, const juce::String& key)
    {
        Reply reply;
        auto options = juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                           .withConnectionTimeoutMs (timeoutMs)
                           .withStatusCode (&reply.status)
                           .withExtraHeaders ("Authorization: Token " + key + "\r\n");

        if (auto in = url.createInputStream (options))
            reply.json = juce::JSON::parse (in->readEntireStreamAsString());

        return reply;
    }

    // What a failed request means to the user.
    juce::String describeFailure (int status)
    {
        if (status == 0)    return "Couldn't reach Freesound. Check your internet connection and try again.";
        if (status == 429)  return "Freesound asks for a short break after that many searches. Try again in a minute.";
        return "Freesound had a problem (error " + juce::String (status) + "). Try again later.";
    }

    bool isRefusal (int status)     { return status == 401 || status == 403; }

    juce::String licenseFor (const juce::String& raw)
    {
        const auto s = raw.trim().toLowerCase();

        if (s == "creative commons 0" || s.contains ("publicdomain/zero") || s.contains ("/zero/"))
            return "CC0-1.0";

        // "/licenses/by/" is plain Attribution; by-nc and by-sa have more in the path.
        if (s.contains ("/licenses/by/"))
        {
            const auto version = s.fromFirstOccurrenceOf ("/licenses/by/", false, false).upToFirstOccurrenceOf ("/", false, false);
            return version.isNotEmpty() && version.containsOnly ("0123456789.") ? "CC-BY-" + version : juce::String ("CC-BY");
        }

        return s == "attribution" ? juce::String ("CC-BY") : juce::String();
    }

    // Many loops say their tempo in the name or tags: "Funk 95 BPM", "bpm120".
    double bpmFromText (const juce::String& text)
    {
        const auto s = text.toLowerCase();
        auto isGap = [] (juce::juce_wchar c) { return c == ' ' || c == '-' || c == '_'; };
        auto isDigit = [] (juce::juce_wchar c) { return c >= '0' && c <= '9'; };

        for (int at = s.indexOf ("bpm"); at >= 0; at = s.indexOf (at + 3, "bpm"))
        {
            int end = at;

            while (end > 0 && isGap (s[end - 1]))
                --end;

            int start = end;

            while (start > 0 && isDigit (s[start - 1]))
                --start;

            for (int pass = 0; pass < 2; ++pass)
            {
                if (pass == 1)
                {
                    start = at + 3;

                    while (start < s.length() && isGap (s[start]))
                        ++start;

                    end = start;

                    while (end < s.length() && isDigit (s[end]))
                        ++end;
                }

                if (end - start >= 2 && end - start <= 3)
                    if (auto bpm = s.substring (start, end).getIntValue(); bpm >= 40 && bpm <= 300)
                        return bpm;
            }
        }

        return 0.0;
    }

    // "Loop_120.wav" -> "Loop_120"
    juce::String displayName (const juce::String& name)
    {
        const auto dot = name.lastIndexOfChar ('.');
        return dot > 0 && name.length() - dot <= 5 ? name.substring (0, dot).trim() : name.trim();
    }

    // Letters, digits and dashes only, so a name can't point anywhere else.
    juce::String fileNameFor (const FreesoundSound& s)
    {
        auto slug = s.name.toLowerCase().replaceCharacter (' ', '-').retainCharacters ("abcdefghijklmnopqrstuvwxyz0123456789-");
        return s.id + (slug.isEmpty() ? juce::String() : "-" + slug.substring (0, 40)) + ".ogg";
    }

    std::optional<FreesoundSound> parseSound (const juce::var& v)
    {
        FreesoundSound s;
        s.id = v["id"].toString();
        s.name = displayName (v["name"].toString());
        s.user = v["username"].toString();
        s.license = licenseFor (v["license"].toString());
        s.pageUrl = v["url"].toString();
        s.previewUrl = v["previews"]["preview-hq-ogg"].toString();
        s.key = v["tonality"].toString();
        s.duration = (double) v["duration"];

        // Only what Freesound told us is trusted for the name of a file or an address we open.
        if (s.id.isEmpty() || ! s.id.containsOnly ("0123456789") || s.name.isEmpty() || s.license.isEmpty()
             || ! isFreesoundAddress (s.previewUrl))
            return std::nullopt;

        if (! isFreesoundAddress (s.pageUrl))
            s.pageUrl = "https://freesound.org/s/" + s.id + "/";

        juce::StringArray tags;

        if (auto* list = v["tags"].getArray())
            for (auto& t : *list)
                tags.add (t.toString());

        for (auto& t : tags)
            if (s.tags.size() < 30 && t.isNotEmpty() && t.length() <= 40)
                s.tags.add (t.toLowerCase());

        auto bpm = (double) v["bpm"];
        s.bpm = bpm >= 40.0 && bpm <= 300.0 ? bpm : bpmFromText (s.name + " " + tags.joinIntoString (" "));
        return s;
    }

    juce::URL searchUrl (const juce::String& text, int pageSize, const FreesoundFilters& filters = {})
    {
        const auto query = text.trim().substring (0, 200);
        juce::String filter = "tag:loop AND duration:[0.5 TO 60] "
                              "AND (license:\"Creative Commons 0\" OR license:\"Attribution\")";

        const auto minBpm = filters.minBpm, maxBpm = filters.maxBpm;

        if (minBpm > 0 || maxBpm > 0)
            filter << " AND bpm:[" << (minBpm > 0 ? juce::String (minBpm) : juce::String ("*")) << " TO "
                   << (maxBpm > 0 ? juce::String (maxBpm) : juce::String ("*")) << "]";

        // Each group is one choice in the browser: a loop needs any one of its tags.
        for (auto& group : filters.anyOf)
        {
            juce::StringArray terms;

            for (auto& tag : group)
                if (tag.isNotEmpty() && tag.containsOnly ("abcdefghijklmnopqrstuvwxyz0123456789-"))
                    terms.add ("tag:\"" + tag + "\"");

            if (! terms.isEmpty())
                filter << " AND (" << terms.joinIntoString (" OR ") << ")";
        }

        auto url = juce::URL (apiRoot + "search/")
                       .withParameter ("query", query)
                       .withParameter ("filter", filter)
                       .withParameter ("fields", "id,name,username,license,duration,previews,url,tags,bpm,tonality")
                       .withParameter ("page_size", juce::String (pageSize));

        return query.isEmpty() ? url.withParameter ("sort", "downloads_desc") : url;
    }

    // Returns an error for the user, or an empty string. `job` says when the store is closing.
    juce::String downloadTo (const juce::String& address, const juce::File& target, juce::ThreadPoolJob& job)
    {
        int status = 0;
        auto in = juce::URL (address).createInputStream (juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                                                             .withConnectionTimeoutMs (timeoutMs)
                                                             .withStatusCode (&status));

        if (in == nullptr || status != 200)
            return describeFailure (status);

        auto partial = target.getSiblingFile (target.getFileName() + ".part");
        partial.deleteFile();

        {
            juce::FileOutputStream out (partial);

            if (! out.openedOk())
                return "Couldn't save the loop. Is the disk full?";

            juce::HeapBlock<char> buffer (65536);
            juce::int64 total = 0;

            while (! in->isExhausted())
            {
                if (job.shouldExit())
                    break;

                const auto got = in->read (buffer, 65536);

                if (got < 0)
                    return describeFailure (0);

                if (got == 0)
                    break;

                total += got;

                if (total > maxPreviewBytes || ! out.write (buffer, (size_t) got))
                    return "Couldn't save the loop. Is the disk full?";
            }

            out.flush();
        }

        if (job.shouldExit())
        {
            partial.deleteFile();
            return "cancelled";
        }

        return partial.moveFileTo (target) ? juce::String() : "Couldn't save the loop. Is the disk full?";
    }

    // Credits made before they carried the id are matched by their address, which ends in it.
    bool isCreditFor (const juce::var& credit, const juce::String& soundId)
    {
        return credit["freesoundId"].toString() == soundId
                || credit["url"].toString().trimCharactersAtEnd ("/").endsWith ("/" + soundId);
    }

    void writeJson (const juce::File& file, const juce::var& value)
    {
        file.getParentDirectory().createDirectory();
        file.replaceWithText (juce::JSON::toString (value, false));
    }
}

//==============================================================================
juce::String FreesoundSound::describe() const
{
    juce::StringArray parts;

    if (bpm > 0.0)
        parts.add (juce::String (juce::roundToInt (bpm)) + " bpm");

    if (duration > 0.0)
        parts.add (duration < 10.0 ? juce::String (duration, 1) + " s" : juce::String (juce::roundToInt (duration)) + " s");

    parts.add (license.startsWith ("CC-BY") ? "CC BY" : "CC0");
    return parts.joinIntoString (middleDot);
}

//==============================================================================
// Runs `work` on a pool thread; what it returns runs on the message thread, if the store is still there.
class FreesoundStore::Job  : public juce::ThreadPoolJob
{
public:
    using Work = std::function<Finish (juce::ThreadPoolJob&)>;

    Job (FreesoundStore& s, Work w) : juce::ThreadPoolJob ("Freesound"), store (&s), work (std::move (w)) {}

    JobStatus runJob() override
    {
        auto finish = work (*this);

        if (finish && ! shouldExit())
            juce::MessageManager::callAsync ([weak = store, finish = std::move (finish)]
            {
                if (weak != nullptr)
                    finish (*weak);
            });

        return jobHasFinished;
    }

private:
    juce::WeakReference<FreesoundStore> store;
    Work work;
};

//==============================================================================
FreesoundStore::FreesoundStore (SoundLibrary& s, Preferences& p)
    : sounds (s), preferences (p)
{
    loadAdded();
}

FreesoundStore::~FreesoundStore() = default;

juce::String FreesoundStore::getApplyUrl()      { return "https://freesound.org/apiv2/apply/"; }
juce::String FreesoundStore::getRegisterUrl()   { return "https://freesound.org/home/register/"; }

juce::File FreesoundStore::getPackFolder() const    { return sounds.getUserFolder().getChildFile ("Freesound Loops"); }
juce::File FreesoundStore::getCacheFolder() const   { return platform::getLocalDataFolder().getChildFile ("freesound-previews"); }

//==============================================================================
bool FreesoundStore::hasKey() const
{
    return isPlausibleKey (preferences.get (settings::freesoundKey));
}

void FreesoundStore::setKey (const juce::String& key)
{
    preferences.set (settings::freesoundKey, key);
}

void FreesoundStore::forgetKey()
{
    setKey ({});
    ++searchNumber;
    results.clear();
    nextPage = {};
    status = Status::idle;
    keyRefused = false;
    keyCheck = KeyCheck::none;
    message = {};
    sendChangeMessage();
}

void FreesoundStore::checkKey (const juce::String& rawKey)
{
    const auto key = rawKey.trim();

    if (! isPlausibleKey (key))
    {
        keyCheck = KeyCheck::rejected;
        message = "That doesn't look like a Freesound API key. It's a long mix of letters and numbers, "
                  "shown as \"Client secret/Api key\".";
        sendChangeMessage();
        return;
    }

    keyCheck = KeyCheck::checking;
    message = {};
    sendChangeMessage();

    pool.addJob (new Job (*this, [key] (juce::ThreadPoolJob&) -> Finish
    {
        const auto reply = getJson (searchUrl ({}, 1), key);

        return [key, status = reply.status] (FreesoundStore& self)
        {
            if (status == 200)
            {
                self.setKey (key);
                self.keyCheck = KeyCheck::accepted;
                self.message = {};
                self.keyRefused = false;
            }
            else if (isRefusal (status))
            {
                self.keyCheck = KeyCheck::rejected;
                self.message = "Freesound didn't accept that key. Copy the \"Client secret/Api key\" value from your "
                               "API keys page.";
            }
            else
            {
                self.keyCheck = KeyCheck::failed;
                self.message = describeFailure (status);
            }

            self.sendChangeMessage();
        };
    }), true);
}

//==============================================================================
void FreesoundStore::search (const juce::String& text, const FreesoundFilters& filters)
{
    if (! hasKey())
        return;

    results.clear();
    nextPage = {};
    runSearch (searchUrl (text, 30, filters), false);
}

void FreesoundStore::loadMore()
{
    if (canLoadMore() && isFreesoundAddress (nextPage))
        runSearch (juce::URL (nextPage), true);
}

void FreesoundStore::runSearch (const juce::URL& url, bool append)
{
    const auto key = preferences.get (settings::freesoundKey);
    const int number = ++searchNumber;
    status = Status::searching;
    keyRefused = false;
    message = {};
    notice = {};
    sendChangeMessage();

    pool.addJob (new Job (*this, [url, key, number, append] (juce::ThreadPoolJob&) -> Finish
    {
        const auto reply = getJson (url, key);
        std::vector<FreesoundSound> found;

        if (auto* list = reply.json["results"].getArray())
            for (auto& item : *list)
                if (auto sound = parseSound (item))
                    found.push_back (std::move (*sound));

        const auto next = reply.json["next"].toString();

        return [number, append, status = reply.status, found = std::move (found), next] (FreesoundStore& self)
        {
            if (number != self.searchNumber)
                return;

            if (status == 200)
            {
                self.results.insert (self.results.end(), found.begin(), found.end());
                self.nextPage = isFreesoundAddress (next) ? next : juce::String();
                self.status = Status::ready;
            }
            else
            {
                self.status = Status::failed;
                self.keyRefused = isRefusal (status);
                self.message = self.keyRefused ? "Freesound didn't accept your API key." : describeFailure (status);

                if (! append)
                    self.results.clear();
            }

            self.sendChangeMessage();
        };
    }), true);
}

std::optional<FreesoundSound> FreesoundStore::findResult (const juce::String& soundId) const
{
    for (auto& r : results)
        if (r.id == soundId)
            return r;

    return std::nullopt;
}

//==============================================================================
juce::File FreesoundStore::getFileFor (const juce::String& soundId) const
{
    if (auto it = added.find (soundId); it != added.end() && it->second.existsAsFile())
        return it->second;

    return getCacheFolder().getChildFile (soundId + ".ogg");
}

bool FreesoundStore::isAdded (const juce::String& soundId) const
{
    auto it = added.find (soundId);
    return it != added.end() && it->second.existsAsFile();
}

juce::String FreesoundStore::findAddedId (const juce::File& file) const
{
    for (auto& [id, f] : added)
        if (f == file)
            return id;

    return {};
}

bool FreesoundStore::removeFromLibrary (const juce::String& soundId)
{
    auto it = added.find (soundId);

    if (it == added.end() || busy.count (soundId) > 0)
        return false;

    const auto file = it->second;

    if (file.existsAsFile() && ! file.deleteFile())
        return false;

    added.erase (it);

    const auto folder = getPackFolder();
    const auto loopsFile = folder.getChildFile ("Loops").getChildFile ("loops.json");
    const auto packFile = folder.getChildFile ("pack.json");

    juce::Array<juce::var> loops;

    // The parsed value owns the array, so it has to outlive the loop over it.
    const auto loopsJson = juce::JSON::parse (loopsFile);

    if (auto* list = loopsJson.getArray())
        for (auto& l : *list)
            if (l["freesoundId"].toString() != soundId)
                loops.add (l);

    const auto packJson = juce::JSON::parse (packFile);

    if (loops.isEmpty() && packJson["id"].toString() == packId)
    {
        // Nothing left from Freesound: the pack goes too.
        folder.deleteRecursively();
    }
    else
    {
        writeJson (loopsFile, loops);
        juce::Array<juce::var> credits;

        if (auto* list = packJson["credits"].getArray())
            for (auto& c : *list)
                if (! isCreditFor (c, soundId))
                    credits.add (c);

        if (auto* object = packJson.getDynamicObject())
        {
            object->setProperty ("credits", credits);
            writeJson (packFile, packJson);
        }
    }

    if (onLibraryChanged)
        onLibraryChanged();

    sendChangeMessage();
    return true;
}

void FreesoundStore::preview (const FreesoundSound& sound)
{
    wantedPreview = sound.id;
    const auto file = getFileFor (sound.id);

    if (file.existsAsFile())
    {
        if (onPreviewReady)
            onPreviewReady (file);

        return;
    }

    if (isBusy (sound.id))
        return;

    busy.insert (sound.id);
    notice = {};
    sendChangeMessage();

    const auto cache = getCacheFolder();

    pool.addJob (new Job (*this, [sound, file, cache] (juce::ThreadPoolJob& job) -> Finish
    {
        cache.createDirectory();

        // Keep the cache from growing for ever: previews unused for two weeks go.
        for (auto& old : cache.findChildFiles (juce::File::findFiles, false, "*.ogg"))
            if (old.getLastAccessTime() < juce::Time::getCurrentTime() - juce::RelativeTime::days (14))
                old.deleteFile();

        const auto error = downloadTo (sound.previewUrl, file, job);
        return [sound, file, error] (FreesoundStore& self) { self.downloadFinished (sound, file, error, false); };
    }), true);
}

void FreesoundStore::addToLibrary (const FreesoundSound& sound)
{
    if (isAdded (sound.id) || isBusy (sound.id))
        return;

    busy.insert (sound.id);
    notice = {};
    sendChangeMessage();

    const auto target = getPackFolder().getChildFile ("Loops").getChildFile (fileNameFor (sound));
    const auto cached = getCacheFolder().getChildFile (sound.id + ".ogg");

    pool.addJob (new Job (*this, [sound, target, cached] (juce::ThreadPoolJob& job) -> Finish
    {
        target.getParentDirectory().createDirectory();

        // Already heard? Then it's on disk.
        const auto error = cached.existsAsFile() && cached.copyFileTo (target) ? juce::String()
                                                                               : downloadTo (sound.previewUrl, target, job);

        return [sound, target, error] (FreesoundStore& self) { self.downloadFinished (sound, target, error, true); };
    }), true);
}

void FreesoundStore::downloadFinished (const FreesoundSound& sound, const juce::File& file, const juce::String& error,
                                       bool toLibrary)
{
    busy.erase (sound.id);

    if (error.isNotEmpty())
    {
        if (error != "cancelled")
            notice = error;
    }
    else if (toLibrary)
    {
        recordInPack (sound, file.getFileName());
        added[sound.id] = file;

        if (onLibraryChanged)
            onLibraryChanged();
    }
    else if (wantedPreview == sound.id && onPreviewReady)
    {
        onPreviewReady (file);
    }

    sendChangeMessage();
}

//==============================================================================
void FreesoundStore::loadAdded()
{
    const auto folder = getPackFolder();
    const auto loops = folder.getChildFile ("Loops");
    const auto loopsFile = loops.getChildFile ("loops.json");
    const auto loopsJson = juce::JSON::parse (loopsFile);
    juce::Array<juce::var> entries;

    if (auto* list = loopsJson.getArray())
        for (auto& l : *list)
        {
            entries.add (l);

            if (auto id = l["freesoundId"].toString(); id.isNotEmpty())
                added[id] = loops.getChildFile (l["file"].toString());
        }

    // Loops whose entry went missing (an older version lost some) come back from their credit.
    const auto packJson = juce::JSON::parse (folder.getChildFile ("pack.json"));
    bool repaired = false;

    if (auto* credits = packJson["credits"].getArray())
    {
        for (auto& file : loops.findChildFiles (juce::File::findFiles, false, "*.ogg"))
        {
            const auto id = file.getFileName().upToFirstOccurrenceOf ("-", false, false);

            if (id.isEmpty() || ! id.containsOnly ("0123456789") || added.count (id) > 0)
                continue;

            for (auto& c : *credits)
            {
                if (! isCreditFor (c, id))
                    continue;

                auto entry = std::make_unique<juce::DynamicObject>();
                const auto name = c["what"].toString();
                entry->setProperty ("file", file.getFileName());
                entry->setProperty ("name", name);
                entry->setProperty ("freesoundId", id);

                if (auto bpm = bpmFromText (name); bpm > 0.0)
                    entry->setProperty ("bpm", bpm);

                entries.add (juce::var (entry.release()));
                added[id] = file;
                repaired = true;
                break;
            }
        }
    }

    if (repaired)
        writeJson (loopsFile, entries);
}

// The loop's entry in loops.json and its credit in pack.json, which Acknowledgements lists.
void FreesoundStore::recordInPack (const FreesoundSound& sound, const juce::String& fileName)
{
    const auto folder = getPackFolder();
    const auto loopsFile = folder.getChildFile ("Loops").getChildFile ("loops.json");
    const auto packFile = folder.getChildFile ("pack.json");

    // The parsed values own the arrays, so they have to outlive the loops over them.
    const auto loopsJson = juce::JSON::parse (loopsFile);
    juce::Array<juce::var> loops;

    if (auto* list = loopsJson.getArray())
        for (auto& l : *list)
            if (l["freesoundId"].toString() != sound.id)
                loops.add (l);

    auto loop = std::make_unique<juce::DynamicObject>();
    loop->setProperty ("file", fileName);
    loop->setProperty ("name", sound.name);
    loop->setProperty ("freesoundId", sound.id);

    if (sound.bpm > 0.0)
        loop->setProperty ("bpm", sound.bpm);

    if (sound.key.isNotEmpty())
        loop->setProperty ("key", sound.key);

    if (! sound.tags.isEmpty())
    {
        juce::Array<juce::var> tags;

        for (auto& t : sound.tags)
            tags.add (t);

        loop->setProperty ("tags", tags);
    }

    loops.add (juce::var (loop.release()));
    writeJson (loopsFile, loops);

    auto credit = std::make_unique<juce::DynamicObject>();
    credit->setProperty ("what", sound.name);
    credit->setProperty ("who", sound.user);
    credit->setProperty ("license", sound.license);
    credit->setProperty ("url", sound.pageUrl);
    credit->setProperty ("freesoundId", sound.id);
    credit->setProperty ("attributionRequired", sound.attributionRequired());

    juce::Array<juce::var> credits;
    const auto pack = juce::JSON::parse (packFile);

    if (auto* list = pack["credits"].getArray())
        for (auto& c : *list)
            if (! isCreditFor (c, sound.id))
                credits.add (c);

    credits.add (juce::var (credit.release()));

    auto info = std::make_unique<juce::DynamicObject>();
    info->setProperty ("format", 2);
    info->setProperty ("id", packId);
    info->setProperty ("name", "Freesound Loops");
    info->setProperty ("version", "1.0.0");
    info->setProperty ("description", "Loops downloaded from freesound.org. Each one is credited to its author.");
    info->setProperty ("author", "Freesound contributors");
    info->setProperty ("credits", credits);
    writeJson (packFile, juce::var (info.release()));
}

} // namespace ikaros
