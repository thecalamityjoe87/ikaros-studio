#pragma once

#include <juce_core/juce_core.h>
#include <atomic>
#include <vector>

namespace ikaros
{

//==============================================================================
/**
    Packing and unpacking .iksession files: a whole session (its data, every
    audio file it uses and its preview picture) in one file, like
    GarageBand's .band, that can move between Linux, Windows and macOS.

    A bundle is a ZIP archive (ZIP64 when it's over 4 GB) laid out like this:

        mimetype                "application/x-ikaros-session", stored first and
                                uncompressed, so a file can be recognised from
                                its first bytes
        bundle.json             format version, the app version that saved it,
                                and the sound packs the session uses
        Session.tracktionedit   the session itself (Tracktion's XML)
        Preview.png             the picture the session chooser shows
        View.xml                how the window looked (see SessionViews)
        Audio/...               recordings, imported files and loops

    The session is edited in an unpacked copy (a working folder, see
    AudioEngine). Saving packs that folder into a new bundle next to the old
    one and then swaps it in, so a failed save never damages a session.

    This class only deals with files; it doesn't touch Tracktion, so it can
    run on a background thread. Paths go through JUCE's file streams (not
    the C library), so names with any characters work on every system.
*/
class SessionBundle
{
public:
    static constexpr const char* extension = ".iksession";
    static constexpr const char* mimeType = "application/x-ikaros-session";
    static constexpr const char* sessionEntry = "Session.tracktionedit";
    static constexpr const char* previewEntry = "Preview.png";
    static constexpr const char* viewEntry = "View.xml";
    static constexpr const char* infoEntry = "bundle.json";
    static constexpr const char* audioFolder = "Audio";

    /** True if the file has the .iksession extension. */
    static bool isBundleFile (const juce::File&);

    //==============================================================================
    /** One file to put in a bundle. */
    struct Entry
    {
        juce::String path;      // where it goes in the bundle, with "/" separators, e.g. "Audio/Take 1.wav"
        juce::File file;
        bool compress = false;  // audio is stored as it is: it's already compressed, or doesn't shrink much
    };

    /** Writes a bundle. The mimetype and bundle.json (made from `info`) go
        first, then the entries. It's written to a temporary file beside
        `bundle`, which only replaces `bundle` once it's complete.

        progress, if given, goes from 0 to 1 as the files are written (it
        can be read from another thread). */
    static juce::Result pack (const juce::File& bundle, const std::vector<Entry>& entries,
                              const juce::var& info, std::atomic<float>* progress = nullptr);

    /** Unpacks a bundle into a folder (normally a new, empty one). Entries
        that would end up outside the folder (absolute paths, "..") are
        refused, so a malicious bundle can't write anywhere else. */
    static juce::Result unpack (const juce::File& bundle, const juce::File& intoFolder);

    /** Reads one entry, e.g. the preview picture, without unpacking the rest.
        Empty if it isn't there. */
    static juce::MemoryBlock readEntry (const juce::File& bundle, const juce::String& path);

    //==============================================================================
    /** A file name that's valid on Linux, Windows and macOS: characters
        Windows doesn't allow are replaced, trailing dots and spaces dropped,
        and names Windows reserves (CON, NUL, COM1...) changed. */
    static juce::String makePortableFileName (const juce::String& name);
};

} // namespace ikaros
