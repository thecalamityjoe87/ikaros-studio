#include "engine/SessionBundle.h"
#include <miniz.h>

namespace ikaros
{

namespace
{
    // miniz reads and writes through these callbacks (its "IO" functions),
    // given a byte offset each time. They go through JUCE's file streams.
    size_t writeToStream (void* opaque, mz_uint64 offset, const void* data, size_t numBytes)
    {
        auto* out = static_cast<juce::FileOutputStream*> (opaque);

        if ((mz_uint64) out->getPosition() != offset && ! out->setPosition ((juce::int64) offset))
            return 0;

        return out->write (data, numBytes) ? numBytes : 0;
    }

    size_t readFromStream (void* opaque, mz_uint64 offset, void* buffer, size_t numBytes)
    {
        auto* in = static_cast<juce::FileInputStream*> (opaque);

        if ((mz_uint64) in->getPosition() != offset && ! in->setPosition ((juce::int64) offset))
            return 0;

        return (size_t) juce::jmax (0, in->read (buffer, (int) numBytes));
    }

    // Unpacking: each entry's bytes arrive in order, so they're just appended.
    size_t appendToStream (void* opaque, mz_uint64, const void* data, size_t numBytes)
    {
        return static_cast<juce::OutputStream*> (opaque)->write (data, numBytes) ? numBytes : 0;
    }

    // An entry being packed: reads its file and reports progress as it goes.
    struct FileSource
    {
        juce::FileInputStream stream;
        std::atomic<float>* progress = nullptr;
        juce::int64 doneBefore = 0, total = 1;

        explicit FileSource (const juce::File& f) : stream (f) {}

        static size_t read (void* opaque, mz_uint64 offset, void* buffer, size_t numBytes)
        {
            auto* source = static_cast<FileSource*> (opaque);
            const auto got = readFromStream (&source->stream, offset, buffer, numBytes);

            if (source->progress != nullptr)
                source->progress->store ((float) (source->doneBefore + (juce::int64) offset + (juce::int64) got)
                                         / (float) source->total);

            return got;
        }
    };

    juce::Result zipError (mz_zip_archive& zip, const juce::String& what)
    {
        return juce::Result::fail (what + " (" + mz_zip_get_error_string (mz_zip_get_last_error (&zip)) + ")");
    }

    // Opens a bundle for reading. The caller must call mz_zip_reader_end.
    bool openForReading (mz_zip_archive& zip, juce::FileInputStream& in)
    {
        if (in.failedToOpen())
            return false;

        mz_zip_zero_struct (&zip);
        zip.m_pRead = readFromStream;
        zip.m_pIO_opaque = &in;
        return mz_zip_reader_init (&zip, (mz_uint64) in.getTotalLength(), 0);
    }
}

//==============================================================================
bool SessionBundle::isBundleFile (const juce::File& f)
{
    return f.hasFileExtension (extension);
}

juce::Result SessionBundle::pack (const juce::File& bundle, const std::vector<Entry>& entries,
                                  const juce::var& info, std::atomic<float>* progress)
{
    auto temp = bundle.getSiblingFile (bundle.getFileName() + ".saving");
    temp.deleteFile();

    juce::int64 total = 1;   // (1, not 0, so progress never divides by zero)

    for (auto& e : entries)
        total += e.file.getSize();

    auto result = juce::Result::ok();

    {
        juce::FileOutputStream out (temp);

        if (out.failedToOpen())
            return juce::Result::fail ("Couldn't write to " + bundle.getParentDirectory().getFullPathName());

        mz_zip_archive zip;
        mz_zip_zero_struct (&zip);
        zip.m_pWrite = writeToStream;
        zip.m_pIO_opaque = &out;

        if (! mz_zip_writer_init_v2 (&zip, 0, 0))
            return zipError (zip, "Couldn't start the session file");

        // The mimetype first and uncompressed, then what the bundle holds.
        const juce::String mime (mimeType);
        const auto infoText = juce::JSON::toString (info);

        if (! mz_zip_writer_add_mem (&zip, "mimetype", mime.toRawUTF8(), mime.getNumBytesAsUTF8(), MZ_NO_COMPRESSION)
             || ! mz_zip_writer_add_mem (&zip, infoEntry, infoText.toRawUTF8(), infoText.getNumBytesAsUTF8(), MZ_DEFAULT_COMPRESSION))
            result = zipError (zip, "Couldn't write the session file");

        juce::int64 done = 0;

        for (auto& e : entries)
        {
            if (result.failed())
                break;

            FileSource source (e.file);

            if (source.stream.failedToOpen())
            {
                result = juce::Result::fail ("Couldn't read " + e.file.getFullPathName());
                break;
            }

            source.progress = progress;
            source.doneBefore = done;
            source.total = total;

            const auto size = (mz_uint64) e.file.getSize();
            const MZ_TIME_T modified = (MZ_TIME_T) (e.file.getLastModificationTime().toMilliseconds() / 1000);

            if (! mz_zip_writer_add_read_buf_callback (&zip, e.path.toRawUTF8(), FileSource::read, &source, size, &modified,
                                                       nullptr, 0, e.compress ? MZ_DEFAULT_COMPRESSION : MZ_NO_COMPRESSION,
                                                       nullptr, 0, nullptr, 0))
                result = zipError (zip, "Couldn't add " + e.path + " to the session file");

            done += (juce::int64) size;
        }

        if (result.wasOk() && ! mz_zip_writer_finalize_archive (&zip))
            result = zipError (zip, "Couldn't finish the session file");

        mz_zip_writer_end (&zip);
        out.flush();

        if (result.wasOk() && out.getStatus().failed())
            result = juce::Result::fail ("Couldn't write the session file: " + out.getStatus().getErrorMessage());
    }

    // Only now, with a complete new file, does it replace the old one.
    if (result.wasOk())
    {
        const bool replaced = bundle.existsAsFile() ? temp.replaceFileIn (bundle) : temp.moveFileTo (bundle);

        if (! replaced)
            result = juce::Result::fail ("Couldn't replace " + bundle.getFullPathName());
    }

    if (result.failed())
        temp.deleteFile();
    else if (progress != nullptr)
        progress->store (1.0f);

    return result;
}

juce::Result SessionBundle::unpack (const juce::File& bundle, const juce::File& intoFolder)
{
    juce::FileInputStream in (bundle);
    mz_zip_archive zip;

    if (! openForReading (zip, in))
        return juce::Result::fail (bundle.getFileName() + " isn't an Ikaros Studio session, or it's damaged.");

    auto result = juce::Result::ok();
    intoFolder.createDirectory();

    for (mz_uint i = 0; i < mz_zip_reader_get_num_files (&zip) && result.wasOk(); ++i)
    {
        mz_zip_archive_file_stat stat;

        if (! mz_zip_reader_file_stat (&zip, i, &stat) || mz_zip_reader_is_file_a_directory (&zip, i))
            continue;

        // Never write outside the folder: no absolute paths, drive letters or "..".
        const auto name = juce::String::fromUTF8 (stat.m_filename).replaceCharacter ('\\', '/');
        const auto target = intoFolder.getChildFile (name);
        const bool safe = name.isNotEmpty() && ! name.startsWithChar ('/') && ! name.containsChar (':')
                            && ! juce::StringArray::fromTokens (name, "/", {}).contains ("..")
                            && target.isAChildOf (intoFolder);

        if (! safe)
        {
            result = juce::Result::fail (bundle.getFileName() + " contains a file it shouldn't (" + name + ").");
            break;
        }

        target.getParentDirectory().createDirectory();
        target.deleteFile();
        juce::FileOutputStream out (target);

        if (out.failedToOpen() || ! mz_zip_reader_extract_to_callback (&zip, i, appendToStream, &out, 0))
            result = zipError (zip, "Couldn't unpack " + name);
    }

    mz_zip_reader_end (&zip);
    return result;
}

juce::MemoryBlock SessionBundle::readEntry (const juce::File& bundle, const juce::String& path)
{
    juce::FileInputStream in (bundle);
    mz_zip_archive zip;
    juce::MemoryOutputStream data;

    if (openForReading (zip, in))
    {
        const int index = mz_zip_reader_locate_file (&zip, path.toRawUTF8(), nullptr, 0);

        if (index >= 0)
            mz_zip_reader_extract_to_callback (&zip, (mz_uint) index, appendToStream, &data, 0);

        mz_zip_reader_end (&zip);
    }

    return data.getMemoryBlock();
}

//==============================================================================
juce::String SessionBundle::makePortableFileName (const juce::String& name)
{
    juce::String result;

    // Characters Windows doesn't allow in names, and control characters.
    for (auto c : name)
        result << (juce::String ("<>:\"/\\|?*").containsChar (c) || c < 32 ? juce::juce_wchar ('_') : c);

    // Windows drops trailing dots and spaces, so two names could become one.
    result = result.trimEnd();

    while (result.endsWithChar ('.'))
        result = result.dropLastCharacters (1).trimEnd();

    // Names Windows reserves for devices, with or without an extension.
    const auto base = result.upToFirstOccurrenceOf (".", false, false).toUpperCase();
    static const juce::StringArray reserved { "CON", "PRN", "AUX", "NUL",
                                              "COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7", "COM8", "COM9",
                                              "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9" };

    if (reserved.contains (base))
        result = "_" + result;

    return result.isEmpty() ? juce::String ("Untitled") : result;
}

} // namespace ikaros
