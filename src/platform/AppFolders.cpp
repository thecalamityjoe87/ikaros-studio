#include "platform/AppFolders.h"

namespace ikaros::platform
{

juce::File getLocalDataFolder()
{
   #if JUCE_LINUX || JUCE_BSD
    // The XDG base directory spec: $XDG_DATA_HOME, defaulting to ~/.local/share.
    const auto xdg = juce::SystemStats::getEnvironmentVariable ("XDG_DATA_HOME", {});
    const auto base = xdg.isNotEmpty() && juce::File::isAbsolutePath (xdg)
                        ? juce::File (xdg)
                        : juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile (".local/share");
    return base.getChildFile ("ikaros-studio");
   #elif JUCE_WINDOWS
    return juce::File::getSpecialLocation (juce::File::windowsLocalAppData).getChildFile ("Ikaros Studio");
   #else
    // macOS: userApplicationDataDirectory is ~/Library; apps keep their data
    // in Application Support inside it.
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile ("Application Support").getChildFile ("Ikaros Studio");
   #endif
}

} // namespace ikaros::platform
