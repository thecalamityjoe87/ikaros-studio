#pragma once

#include <juce_core/juce_core.h>

namespace ikaros::platform
{

/*  Where the app keeps large files that belong to this computer only (the
    unpacked copies of open sessions, see AudioEngine):

        Linux    $XDG_DATA_HOME/ikaros-studio, or ~/.local/share/ikaros-studio
        Windows  %LOCALAPPDATA%\Ikaros Studio (not the roaming profile)
        macOS    ~/Library/Application Support/Ikaros Studio

    Unlike the temp folder, it isn't cleared on restart (so a crashed
    session's work can be found again), and unlike the settings folder it's
    a sensible place for gigabytes of audio. The folder may not exist yet.
*/
juce::File getLocalDataFolder();

} // namespace ikaros::platform
