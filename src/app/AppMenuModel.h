#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace ikaros
{

//==============================================================================
/**
    The app menus: File, Edit, Track, Region, Mix, View, Window, Help.

    A MenuBarModel just describes menus. The same model feeds our custom-drawn
    MenuBarComponent on Linux/Windows and the native menu bar on macOS
    (via MenuBarModel::setMacMainMenu in MainComponent).
*/
class AppMenuModel  : public juce::MenuBarModel
{
public:
    explicit AppMenuModel (juce::ApplicationCommandManager&);

    juce::StringArray getMenuBarNames() override;
    juce::PopupMenu getMenuForIndex (int topLevelMenuIndex, const juce::String& menuName) override;
    void menuItemSelected (int, int) override {}   // commands are dispatched by the command manager

private:
    juce::ApplicationCommandManager& commands;
};

} // namespace ikaros
