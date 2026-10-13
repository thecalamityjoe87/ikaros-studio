#include "engine/SessionRecovery.h"
#include "platform/FileLock.h"

namespace ikaros
{

// Writes a whole file to a temporary file next to it, then swaps it in, so
// a crash halfway through leaves the old file rather than half a new one.
// (flush() also asks the OS to put the data on the disk: fsync on Linux.)
static bool writeAtomically (const juce::File& file, const juce::String& text)
{
    juce::TemporaryFile temp (file);

    {
        juce::FileOutputStream out (temp.getFile());

        if (! out.openedOk() || ! out.writeText (text, false, false, nullptr))
            return false;

        out.flush();

        if (out.getStatus().failed())
            return false;
    }

    return temp.overwriteTargetFileWithTemporary();
}

//==============================================================================
bool SessionRecovery::writeInfo (const juce::File& workFolder, const Info& info)
{
    // juce::DynamicObject is JUCE's JSON object; juce::var holds any JSON value.
    juce::Array<juce::var> takes;

    for (auto& take : info.takes)
    {
        auto t = new juce::DynamicObject();   // owned by the var it's put in (reference counted)
        t->setProperty ("kind", take.midi ? "midi" : "audio");
        t->setProperty ("track", juce::String::toHexString ((juce::int64) take.track));   // 64-bit IDs don't survive as JSON numbers
        t->setProperty ("path", take.path);
        t->setProperty ("startBeats", take.startBeats);
        t->setProperty ("recordedAfter", take.recordedAfter);
        takes.add (juce::var (t));
    }

    auto root = new juce::DynamicObject();
    juce::var json (root);
    root->setProperty ("name", info.name);
    root->setProperty ("bundle", info.bundle.getFullPathName());
    root->setProperty ("legacySession", info.legacySession.getFullPathName());
    root->setProperty ("takes", takes);

    return writeAtomically (workFolder.getChildFile (infoFile), juce::JSON::toString (json));
}

std::optional<SessionRecovery::Info> SessionRecovery::readInfo (const juce::File& workFolder)
{
    const auto json = juce::JSON::parse (workFolder.getChildFile (infoFile));

    if (! json.isObject())
        return std::nullopt;

    // An empty path makes an empty juce::File (not the current folder).
    auto toFile = [] (const juce::var& v)
    {
        const auto path = v.toString();
        return juce::File::isAbsolutePath (path) ? juce::File (path) : juce::File();
    };

    Info info;
    info.name = json["name"].toString();
    info.bundle = toFile (json["bundle"]);
    info.legacySession = toFile (json["legacySession"]);

    if (auto* takes = json["takes"].getArray())
    {
        for (auto& t : *takes)
        {
            Take take;
            take.midi = t["kind"].toString() == "midi";
            take.track = (TrackId) t["track"].toString().getHexValue64();
            take.path = t["path"].toString();
            take.startBeats = (double) t["startBeats"];
            take.recordedAfter = t["recordedAfter"].toString();

            if (take.track != 0 && take.path.isNotEmpty())
                info.takes.push_back (take);
        }
    }

    return info;
}

void SessionRecovery::clear (const juce::File& workFolder)
{
    workFolder.getChildFile (autosaveFile).deleteFile();
    workFolder.getChildFile (infoFile).deleteFile();
    workFolder.getChildFile (takesFolder).deleteRecursively();
}

std::vector<juce::File> SessionRecovery::findCrashedWorkFolders (const juce::File& root)
{
    std::vector<juce::File> crashed;

    for (auto& folder : root.findChildFiles (juce::File::findDirectories, false))
    {
        auto lock = folder.getChildFile (lockFile);

        if (lock.existsAsFile() && ! platform::FileLock::isHeld (lock))
            crashed.push_back (folder);
    }

    return crashed;
}

bool SessionRecovery::hasWorkToRecover (const juce::File& workFolder)
{
    auto info = readInfo (workFolder);
    return info.has_value() && (workFolder.getChildFile (autosaveFile).existsAsFile() || ! info->takes.empty());
}

juce::Time SessionRecovery::getLastWriteTime (const juce::File& workFolder, const Info& info)
{
    juce::Time latest;

    auto consider = [&latest] (const juce::File& f)
    {
        if (f.existsAsFile())
            latest = juce::jmax (latest, f.getLastModificationTime());
    };

    consider (workFolder.getChildFile (autosaveFile));

    for (auto& take : info.takes)
        consider (workFolder.getChildFile (take.path));

    return latest;
}

//==============================================================================
// A WAV file is a "RIFF" header followed by chunks, each a 4-letter ID, a
// 32-bit little-endian size, and that many bytes (plus a pad byte if odd):
//
//     "RIFF" <file size - 8> "WAVE"
//         "fmt " <16+> channels, sample rate, bytes per frame...
//         ...other chunks (bext: when it was recorded; JUNK: room to grow)
//         "data" <audio size> the audio
bool SessionRecovery::repairWavFile (const juce::File& file)
{
    const auto fileSize = file.getSize();
    juce::int64 dataSizePos = -1, dataStart = 0;
    juce::uint32 declaredSize = 0;
    int blockAlign = 0;

    {
        juce::FileInputStream in (file);

        if (! in.openedOk() || fileSize < 44)
            return false;

        char id[4];
        auto readId = [&] { return in.read (id, 4) == 4; };
        auto is = [&] (const char* name) { return std::memcmp (id, name, 4) == 0; };

        if (! readId() || ! is ("RIFF"))
            return false;   // (RF64, the over-4 GB variant, only exists once a recording finished)

        in.readInt();   // the file size (fixed below)

        if (! readId() || ! is ("WAVE"))
            return false;

        while (in.getPosition() + 8 <= fileSize && readId())
        {
            const auto size = (juce::uint32) in.readInt();   // readInt() is little-endian, like WAV
            const auto start = in.getPosition();

            if (is ("fmt "))
            {
                in.setPosition (start + 12);   // past format, channels, sample rate and bytes per second
                blockAlign = (int) (juce::uint16) in.readShort();
            }
            else if (is ("data"))
            {
                dataSizePos = start - 4;
                dataStart = start;
                declaredSize = size;
                break;
            }

            in.setPosition (start + (juce::int64) size + (size & 1));
        }
    }

    if (dataSizePos < 0 || blockAlign <= 0)
        return false;

    // What's really there, in whole frames, kept within what 32 bits can say.
    auto actual = fileSize - dataStart;
    actual = juce::jmin (actual, (juce::int64) 0xfffffff0);
    actual -= actual % blockAlign;

    // A finished file says exactly this (or less, when chunks follow the audio).
    if (declaredSize != 0 && (juce::int64) declaredSize <= actual)
        return true;

    {
        // FileOutputStream opens an existing file without emptying it; setPosition
        // lets us write over just the two sizes.
        juce::FileOutputStream out (file);

        if (! out.openedOk())
            return false;

        out.setPosition (4);
        out.writeInt ((int) (dataStart + actual - 8));
        out.setPosition (dataSizePos);
        out.writeInt ((int) actual);

        // Drop a half-written frame at the end.
        out.setPosition (dataStart + actual);
        out.truncate();
        out.flush();

        if (out.getStatus().failed())
            return false;
    }

    return true;
}

//==============================================================================
juce::String SessionRecovery::toJournalLines (const juce::Array<juce::MidiMessage>& messages)
{
    juce::String text;

    for (auto& m : messages)
    {
        text << juce::String (m.getTimeStamp(), 6);

        for (int i = 0; i < m.getRawDataSize(); ++i)
            text << ' ' << juce::String::toHexString (m.getRawData()[i]).paddedLeft ('0', 2);

        text << '\n';
    }

    return text;
}

juce::Array<juce::MidiMessage> SessionRecovery::readJournal (const juce::File& file)
{
    juce::Array<juce::MidiMessage> messages;
    const auto text = file.loadFileAsString();
    juce::StringArray lines;
    lines.addLines (text);

    // The last line only counts if it was finished (ends with a newline).
    if (! text.endsWithChar ('\n') && ! lines.isEmpty())
        lines.remove (lines.size() - 1);

    for (auto& line : lines)
    {
        auto parts = juce::StringArray::fromTokens (line, " ", {});

        if (parts.size() < 2 || parts.size() > 4)   // a time and 1-3 bytes (no SysEx in takes)
            continue;

        juce::uint8 bytes[3] {};

        for (int i = 1; i < parts.size(); ++i)
            bytes[i - 1] = (juce::uint8) parts[i].getHexValue32();

        // Status bytes have the top bit set; anything else is garbage.
        if ((bytes[0] & 0x80) == 0)
            continue;

        messages.add (juce::MidiMessage (bytes, parts.size() - 1, parts[0].getDoubleValue()));
    }

    return messages;
}

} // namespace ikaros
