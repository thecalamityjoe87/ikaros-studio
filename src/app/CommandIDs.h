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
        closeSession,

        undo,
        redo,
        deleteSelection,
        preferences,        // Edit > Preferences (Settings in the macOS app menu)

        newTrack,
        newAudioTrack,
        armTrack,
        deleteTrack,
        muteTrack,
        soloTrack,
        duplicateTrackSettings,
        duplicateTrack,
        renameTrack,
        moveTrackUp,
        moveTrackDown,

        newRegion,
        splitRegion,
        duplicateRegion,
        cutRegions,
        copyRegions,
        pasteRegions,
        renameRegion,
        deleteRegion,

        togglePlay,
        toggleRecord,
        goToStart,
        toggleLoop,
        toggleMetronome,
        countInOff,
        countInOneBar,
        countInTwoBars,
        toggleMidiCycleTakes,

        toggleLibrary,
        toggleBottomPanel,
        zoomIn,
        zoomOut,
        snapToBar,
        snapToBeat,
        snapOff,
        showSoundTab,
        showEditorTab,
        showEqTab,
        showMixerTab,
        toggleAutomation,
        toggleTempoLane,
        compactTracks,
        tallTracks,
        toggleInspector,

        toggleFullScreen,
        manageSounds,
        freesoundKey,
        about,

        exportSong,
        exportStems,

        showPlugins,        // Mix > Plug-ins...: the library's plug-in list
        zoomReset,          // View > Reset Zoom: back to the starting zoom
        toggleTuner,        // View > Tuner: the transport readout becomes a tuner
        closeTuner,         // Escape while tuning (in no menu)
        showPedalboardTab,  // View > Pedalboard: an audio track's pedals
        convertToMidiRegion,    // Region > Convert to MIDI Region: a Drummer region's notes, to edit
    };
}

} // namespace ikaros
