#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace ikaros
{

// Every menu item / keyboard shortcut is an "application command".
// JUCE's ApplicationCommandManager maps IDs to handlers (MainComponent),
// menu entries (AppMenuModel) and key presses, so each action is defined once.
namespace CommandIDs
{
    enum : juce::CommandID
    {
        newSession = 0x2000,
        openSession,
        saveSession,
        saveSessionAs,
        audioSettings,

        undo,
        redo,
        deleteSelection,

        newTrack,
        deleteTrack,
        muteTrack,
        soloTrack,

        newRegion,
        deleteRegion,

        togglePlay,
        goToStart,
        toggleLoop,
        toggleMetronome,

        toggleLibrary,
        showSoundTab,
        showEditorTab,
        showMixerTab,
        toggleDetails,

        toggleFullScreen,
        about,
    };
}

} // namespace ikaros
