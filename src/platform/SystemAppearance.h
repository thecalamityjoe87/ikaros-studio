#pragma once

#include <functional>
#include <memory>

namespace ikaros::platform
{

/*  Whether the desktop is set to dark mode, for Preferences > Appearance >
    Theme > Match system, and a call when that changes.

    macOS and Windows tell JUCE, which asks them. On Linux, JUCE only looks
    for "dark" in the GTK theme's name, which misses GNOME's own dark mode
    (it keeps the Adwaita theme and sets a colour scheme instead). So on
    Linux this asks the desktop portal first: the cross-desktop setting that
    GNOME, KDE and others share, and that a Flatpak can read. Without a
    portal, or if it has no preference, it falls back to JUCE.

    The portal is read once, when this is made; after that it tells us of
    changes with its SettingChanged signal, so prefersDark() never waits on
    D-Bus. onChange isn't called for changes JUCE sees (listen to JUCE's
    DarkModeSettingListener for those).

    Message thread only: on Linux the signal arrives through JUCE's event
    loop, and onChange is called there.
*/
class SystemAppearance
{
public:
    explicit SystemAppearance (std::function<void()> onChange);
    ~SystemAppearance();

    bool prefersDark() const;

private:
    struct Portal;
    std::unique_ptr<Portal> portal;   // Linux only; null elsewhere
};

} // namespace ikaros::platform
