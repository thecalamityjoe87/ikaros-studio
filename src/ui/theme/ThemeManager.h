#pragma once

#include "engine/Preferences.h"
#include "platform/SystemAppearance.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace ikaros
{

class IkarosLookAndFeel;

//==============================================================================
/**
    Keeps the active palette (theme()) in line with Preferences > Appearance >
    Theme: Light, Dark, or Match system, which follows the desktop's dark
    mode while the app runs.

    The app owns one, made before the main window: its constructor picks the
    saved theme, so the window opens in the right colours. After that a
    change of setting, or of the desktop's mode, switches every open window
    on the spot.

    Message thread only.
*/
class ThemeManager  : private Preferences::Listener,
                      private juce::DarkModeSettingListener
{
public:
    ThemeManager (Preferences&, IkarosLookAndFeel&);
    ~ThemeManager() override;

private:
    void preferenceChanged (const juce::String& key) override;   // Preferences::Listener
    void darkModeSettingChanged() override;                      // the desktop's mode, as JUCE sees it

    /** Makes the palette the setting asks for active, and repaints
        everything if that's a change. */
    void update();

    Preferences& preferences;
    IkarosLookAndFeel& lookAndFeel;

    // Kept whatever the setting, so it's current when Match system is
    // picked; it calls update() when the desktop portal's mode changes.
    platform::SystemAppearance systemAppearance { [this] { update(); } };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ThemeManager)
};

} // namespace ikaros
