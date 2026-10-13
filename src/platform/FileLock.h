#pragma once

#include <juce_core/juce_core.h>

namespace ikaros::platform
{

/*  A lock on a file, held for as long as the FileLock object exists. The
    operating system lets go of it when the process ends, however it ends,
    so a lock file nobody holds is a sign that its owner crashed.

    The engine puts one in each open session's working copy (see
    SessionRecovery): at launch, a working copy whose lock file is there but
    not held belongs to a session that was open when the app crashed.

    Other processes can still read the file; they just can't lock it. The
    lock isn't handed on to child processes (the plug-in scanner), so a
    crashed app's lock can't outlive it in one of them.
*/
class FileLock
{
public:
    /** Creates the file if needed and tries to lock it; see isLocked(). */
    explicit FileLock (const juce::File&);
    ~FileLock();   // lets go of the lock (the file stays)

    /** False if someone else holds the lock, or the file couldn't be made. */
    bool isLocked() const;

    /** True if some process (this one included) holds a lock on the file. */
    static bool isHeld (const juce::File&);

private:
   #if JUCE_WINDOWS
    void* handle = nullptr;   // a Windows HANDLE (not named, to keep <windows.h> out of headers)
   #else
    int fd = -1;              // a POSIX file descriptor
   #endif

    JUCE_DECLARE_NON_COPYABLE (FileLock)
};

} // namespace ikaros::platform
