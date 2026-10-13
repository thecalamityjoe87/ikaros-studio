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
        add (CommandIDs::closeSession);
        m.addSeparator();
        add (CommandIDs::audioSettings);
        m.addSeparator();

        juce::PopupMenu exportMenu;
        exportMenu.addCommandItem (&commands, CommandIDs::exportSong);
        exportMenu.addCommandItem (&commands, CommandIDs::exportStems);
        m.addSubMenu ("Export", exportMenu);

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
        add (CommandIDs::newAudioTrack);
        add (CommandIDs::duplicateTrackSettings);
        add (CommandIDs::duplicateTrack);
        m.addSeparator();
        add (CommandIDs::renameTrack);
        add (CommandIDs::moveTrackUp);
        add (CommandIDs::moveTrackDown);
        m.addSeparator();
        add (CommandIDs::muteTrack);
        add (CommandIDs::soloTrack);
        add (CommandIDs::armTrack);
        m.addSeparator();
        add (CommandIDs::deleteTrack);
    }
    else if (name == "Region")
    {
        add (CommandIDs::newRegion);
        m.addSeparator();
        add (CommandIDs::splitRegion);
        add (CommandIDs::duplicateRegion);
        m.addSeparator();
        add (CommandIDs::cutRegions);
        add (CommandIDs::copyRegions);
        add (CommandIDs::pasteRegions);
        m.addSeparator();
        add (CommandIDs::renameRegion);
        add (CommandIDs::deleteRegion);
    }
    else if (name == "Mix")
    {
        add (CommandIDs::togglePlay);
        add (CommandIDs::toggleRecord);
        add (CommandIDs::goToStart);
        add (CommandIDs::toggleLoop);
        add (CommandIDs::toggleMetronome);
        add (CommandIDs::toggleCountIn);
        m.addSeparator();
        add (CommandIDs::showMixerTab);
        add (CommandIDs::toggleAutomation);
    }
    else if (name == "View")
    {
        add (CommandIDs::toggleLibrary);
        add (CommandIDs::toggleBottomPanel);
        m.addSeparator();
        add (CommandIDs::zoomIn);
        add (CommandIDs::zoomOut);
        m.addSeparator();
        add (CommandIDs::snapToBar);
        add (CommandIDs::snapToBeat);
        add (CommandIDs::snapOff);
        m.addSeparator();
        add (CommandIDs::showSoundTab);
        add (CommandIDs::showEditorTab);
        add (CommandIDs::showEqTab);
        add (CommandIDs::showMixerTab);
        m.addSeparator();
        add (CommandIDs::toggleDetails);
    }
    else if (name == "Window")
    {
        add (CommandIDs::moreSounds);
        m.addSeparator();
        add (CommandIDs::toggleFullScreen);
    }
    else if (name == "Help")
    {
        add (CommandIDs::about);
    }

    return m;
}

} // namespace ikaros
