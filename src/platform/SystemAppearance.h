#pragma once

namespace ikaros::platform
{

/*  Whether the desktop is set to dark mode, for Preferences > Appearance >
    Theme > Match system.

    macOS and Windows tell JUCE, which asks them. On Linux, JUCE only looks
    for "dark" in the GTK theme's name, which misses GNOME's own dark mode
    (it keeps the Adwaita theme and sets a colour scheme instead). So on
    Linux this asks the desktop portal first: the cross-desktop setting that
    GNOME, KDE and others share, and that a Flatpak can read. Without a
    portal, or if it has no preference, it falls back to JUCE.

    Cheap enough to call every few seconds. Message thread only.
*/
bool systemPrefersDark();

} // namespace ikaros::platform
