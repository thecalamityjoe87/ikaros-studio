#include "app/AppMenuModel.h"
#include "app/CommandIDs.h"

namespace ikaros
{

AppMenuModel::AppMenuModel (juce::ApplicationCommandManager& cm) : commands (cm)
{
    // Re-query menu item state (enabled/ticked) whenever commands change.
    setApplicationCommandManagerToWatch (&commands);
}

juce::StringArray AppMenuModel::getMenuBarNames()
{
    return { "File", "Edit", "Track", "Region", "Mix", "View", "Window", "Help" };
}

juce::PopupMenu AppMenuModel::getMenuForIndex (int, const juce::String& name)
{
    juce::PopupMenu m;

    // addCommandItem pulls the item's text, shortcut, enabled and ticked state
    // from MainComponent::getCommandInfo.
    auto add = [&] (juce::CommandID id) { m.addCommandItem (&commands, id); };

    if (name == "File")
    {
        add (CommandIDs::newSession);
        add (CommandIDs::openSession);
        m.addSeparator();
        add (CommandIDs::saveSession);
        add (CommandIDs::saveSessionAs);
        m.addSeparator();
        add (CommandIDs::audioSettings);
        // TODO(milestone-4): Export / Share submenu.

       #if ! JUCE_MAC   // macOS puts Quit in the app menu automatically
        m.addSeparator();
        add (juce::StandardApplicationCommandIDs::quit);
       #endif
    }
    else if (name == "Edit")
    {
        add (CommandIDs::undo);
        add (CommandIDs::redo);
        m.addSeparator();
        add (CommandIDs::deleteSelection);
    }
    else if (name == "Track")
    {
        add (CommandIDs::newTrack);
        add (CommandIDs::deleteTrack);
        m.addSeparator();
        add (CommandIDs::muteTrack);
        add (CommandIDs::soloTrack);
    }
    else if (name == "Region")
    {
        add (CommandIDs::newRegion);
        add (CommandIDs::deleteRegion);
    }
    else if (name == "Mix")
    {
        add (CommandIDs::togglePlay);
        add (CommandIDs::goToStart);
        add (CommandIDs::toggleLoop);
        add (CommandIDs::toggleMetronome);
        m.addSeparator();
        add (CommandIDs::showMixerTab);
    }
    else if (name == "View")
    {
        add (CommandIDs::toggleLibrary);
        m.addSeparator();
        add (CommandIDs::showSoundTab);
        add (CommandIDs::showEditorTab);
        add (CommandIDs::showMixerTab);
        m.addSeparator();
        add (CommandIDs::toggleDetails);
    }
    else if (name == "Window")
    {
        add (CommandIDs::toggleFullScreen);
    }
    else if (name == "Help")
    {
        add (CommandIDs::about);
    }

    return m;
}

} // namespace ikaros
