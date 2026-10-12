#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace ikaros::dragtypes
{

// Drags inside the window (juce::DragAndDropContainer) carry a juce::var
// "description". A loop or audio file dragged from the library carries this
// prefix followed by the file's full path, and the arrangement accepts it.
inline const juce::String libraryFilePrefix ("ikaros-library-file:");

inline juce::var describeLibraryFile (const juce::File& f)     { return libraryFilePrefix + f.getFullPathName(); }

// A sound (instrument) dragged from the library: this prefix and the
// sound's ID (SoundInfo::id, or "plugin:<id>").
inline const juce::String librarySoundPrefix ("ikaros-library-sound:");

inline juce::var describeLibrarySound (const juce::String& soundId)  { return librarySoundPrefix + soundId; }

/** The sound a drag carries, or an empty string if it isn't a library sound. */
inline juce::String getLibrarySound (const juce::var& description)
{
    auto text = description.toString();
    return text.startsWith (librarySoundPrefix) ? text.substring (librarySoundPrefix.length()) : juce::String();
}

/** The file a drag carries, or an empty File if it isn't a library file. */
inline juce::File getLibraryFile (const juce::var& description)
{
    auto text = description.toString();
    return text.startsWith (libraryFilePrefix) ? juce::File (text.substring (libraryFilePrefix.length())) : juce::File();
}

} // namespace ikaros::dragtypes
