#include "platform/FileLock.h"

#if JUCE_WINDOWS
 #include <windows.h>
#else
 #include <cerrno>
 #include <fcntl.h>
 #include <sys/file.h>
 #include <unistd.h>
#endif

namespace ikaros::platform
{

#if JUCE_WINDOWS

// Windows: the file is held open without letting anyone else write to it.
// Opening it for writing then fails with a "sharing violation" until the
// handle is closed, which Windows does itself when a process dies.
FileLock::FileLock (const juce::File& file)
{
    auto h = CreateFileW (file.getFullPathName().toWideCharPointer(), GENERIC_READ | GENERIC_WRITE,
                          FILE_SHARE_READ, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);

    if (h != INVALID_HANDLE_VALUE)
        handle = h;
}

FileLock::~FileLock()
{
    if (handle != nullptr)
        CloseHandle (handle);
}

bool FileLock::isLocked() const   { return handle != nullptr; }

bool FileLock::isHeld (const juce::File& file)
{
    auto h = CreateFileW (file.getFullPathName().toWideCharPointer(), GENERIC_READ | GENERIC_WRITE,
                          FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);

    if (h == INVALID_HANDLE_VALUE)
        return GetLastError() == ERROR_SHARING_VIOLATION;

    CloseHandle (h);
    return false;
}

#else

// Linux and macOS: flock(). Unlike fcntl() locks, an flock() lock belongs to
// the open file, not the whole process, so closing some other handle to the
// same file doesn't drop it, and a second FileLock in this process sees it.
// O_CLOEXEC keeps the descriptor (and so the lock) out of child processes.
FileLock::FileLock (const juce::File& file)
{
    fd = ::open (file.getFullPathName().toRawUTF8(), O_RDWR | O_CREAT | O_CLOEXEC, 0644);

    if (fd >= 0 && ::flock (fd, LOCK_EX | LOCK_NB) != 0)
    {
        ::close (fd);
        fd = -1;
    }
}

FileLock::~FileLock()
{
    if (fd >= 0)
        ::close (fd);   // closing the file releases the lock
}

bool FileLock::isLocked() const   { return fd >= 0; }

bool FileLock::isHeld (const juce::File& file)
{
    const int probe = ::open (file.getFullPathName().toRawUTF8(), O_RDONLY | O_CLOEXEC);

    if (probe < 0)
        return false;

    // A shared lock can't be had while someone holds the exclusive one.
    const bool held = ::flock (probe, LOCK_SH | LOCK_NB) != 0 && errno == EWOULDBLOCK;
    ::close (probe);
    return held;
}

#endif

} // namespace ikaros::platform
