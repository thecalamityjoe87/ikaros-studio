#include "app/MainComponent.h"
#include "app/CommandIDs.h"
#include "ui/dialogs/NewTrackDialog.h"
#include "ui/library/MoreSoundsDialog.h"
#include "ui/dialogs/SavingOverlay.h"
#include "ui/pluginwindow/PluginWindow.h"
#include "engine/PluginLibrary.h"
#include "engine/TrackController.h"
#include "ui/theme/Tokens.h"

namespace ikaros
{

using namespace tokens;

namespace
{
    constexpr int minLibraryWidth = 200, maxLibraryWidth = 420;
    constexpr int minArrangementHeight = 160;   // ruler, one track and the scroll bar

    // Dragging a panel's edge this far past its smallest size hides the panel.
    constexpr int collapseDistance = 80;
}

MainComponent::MainComponent (AudioEngine& e) : engine (e)
{
    setOpaque (true);

    addAndMakeVisible (menuBar);
    addAndMakeVisible (toolbar);
    addAndMakeVisible (library);
    addAndMakeVisible (arrangement);
    addAndMakeVisible (bottomPanel);

    arrangement.onOpenRegionInEditor = [this] (RegionId)
    {
        setBottomPanelVisible (true);
        bottomPanel.setMasterMode (false);   // the Editor works on a track
        bottomPanel.setTab (BottomPanel::Tab::editor);
    };
    arrangement.onSnapChanged = [this] { commands.commandStatusChanged(); };
    toolbar.onNewTrackRequested = [this] (juce::Component*) { showNewTrackDialog(); };
    bottomPanel.onViewStateChanged = [this] { commands.commandStatusChanged(); };
    bottomPanel.onCollapseRequested = [this] (bool collapse) { setBottomPanelVisible (! collapse); };
    bottomPanel.onDownloadPackRequested = [this] (const juce::String& packId) { MoreSoundsDialog::show (*this, engine, packId); };
    library.onMoreSoundsRequested = [this] { MoreSoundsDialog::show (*this, engine); };
    setUpResizers();

    // Commands: register everything this component can do, make it the
    // default target, and let the command manager turn key presses into commands.
    commands.registerAllCommandsForTarget (this);
    commands.setFirstCommandTarget (this);
    addKeyListener (commands.getKeyMappings());

   #if JUCE_MAC
    juce::MenuBarModel::setMacMainMenu (&menuModel);
   #endif

    engine.addListener (this);
    selection.addChangeListener (this);

    // Third-party plug-in windows: the engine supplies the plug-in's editor,
    // PluginWindow the frame around it.
    engine.setPluginWindowFactory (&PluginWindow::create);

    // Look for new plug-ins in the background once the window is up. Ones
    // scanned before load straight from the saved list.
    juce::Timer::callAfterDelay (1500, [safeThis = SafePointer<MainComponent> (this)]
    {
        if (safeThis != nullptr)
            safeThis->engine.getPlugins().scan();
    });

    refreshAll();
    startTimerHz (30);   // playhead, meters, CPU
    setSize (1440, 900);

    // Like GarageBand, start with the session chooser: recent sessions if
    // there are any, otherwise the templates. (Async: the window isn't on
    // screen yet.)
    juce::MessageManager::callAsync ([safeThis = SafePointer<MainComponent> (this)]
    {
        if (safeThis != nullptr)
            safeThis->showSessionChooser (safeThis->engine.getRecentSessions().empty() ? SessionChooser::Page::newSession
                                                                                      : SessionChooser::Page::recent);
    });
}

MainComponent::~MainComponent()
{
    stopTimer();
    engine.removeListener (this);
    selection.removeChangeListener (this);
    engine.setPluginWindowFactory (nullptr);
    engine.getPlugins().cancelScan();

   #if JUCE_MAC
    juce::MenuBarModel::setMacMainMenu (nullptr);
   #endif
}

//==============================================================================
void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (colour::windowBg);
}

void MainComponent::resized()
{
    // Dialogs drawn over the window (session chooser, new track, saving) cover all of it.
    for (auto* child : getChildren())
        if (dynamic_cast<SessionChooser*> (child) != nullptr || dynamic_cast<NewTrackDialog*> (child) != nullptr
             || dynamic_cast<SavingOverlay*> (child) != nullptr || dynamic_cast<MoreSoundsDialog*> (child) != nullptr)
            child->setBounds (getLocalBounds());

    auto r = getLocalBounds();
    menuBar.setBounds (r.removeFromTop (size::menuBarHeight));
    toolbar.setBounds (r.removeFromTop (size::toolbarHeight));

    library.setVisible (libraryVisible);

    if (libraryVisible)
    {
        libraryWidth = juce::jlimit (minLibraryWidth, maxLibraryWidth, libraryWidth);
        library.setBounds (r.removeFromLeft (libraryWidth));
    }

    const int bottomHeight = bottomPanelVisible ? juce::jlimit (BottomPanel::preferredHeight,
                                                                getMaxBottomPanelHeight(), bottomPanelHeight)
                                                : BottomPanel::collapsedHeight;
    bottomPanel.setBounds (r.removeFromBottom (bottomHeight));
    arrangement.setBounds (r);

    // The handles straddle the panel edges, on top of everything else. The
    // library's handle stays out while it's being dragged, even when the drag
    // has hidden the library, so dragging back out can show it again.
    const int half = ResizeHandle::thickness / 2;
    libraryResizer.setVisible (libraryVisible || libraryResizer.isDragging());
    libraryResizer.setBounds (r.getX() - half, r.getY(), ResizeHandle::thickness, bottomPanel.getBottom() - r.getY());
    bottomResizer.setBounds (r.getX(), bottomPanel.getY() - half, r.getWidth(), ResizeHandle::thickness);
}

//==============================================================================
void MainComponent::setUpResizers()
{
    // Library: drag its right edge. Well past the narrowest width, it hides.
    libraryResizer.onDragStart = [this] { resizeStartSize = libraryVisible ? libraryWidth : 0; };
    libraryResizer.onDrag = [this] (int offset)
    {
        const int width = resizeStartSize + offset;
        libraryVisible = width > minLibraryWidth - collapseDistance;

        if (libraryVisible)
            libraryWidth = juce::jlimit (minLibraryWidth, maxLibraryWidth, width);

        resized();
    };
    libraryResizer.onDragEnd = [this] { resized(); commands.commandStatusChanged(); };
    libraryResizer.onDoubleClick = [this] { libraryWidth = size::sidebarWidth; resized(); };

    // Bottom panel: drag its top edge. Well below the smallest height it
    // collapses to its header row; dragging up opens it again.
    bottomResizer.onDragStart = [this]
    {
        resizeStartSize = bottomPanelVisible ? bottomPanel.getHeight() : BottomPanel::collapsedHeight;
    };
    bottomResizer.onDrag = [this] (int offset)
    {
        const int height = resizeStartSize - offset;   // dragging up makes it taller
        const bool show = height > BottomPanel::preferredHeight - collapseDistance;

        if (show)
            bottomPanelHeight = juce::jlimit (BottomPanel::preferredHeight, getMaxBottomPanelHeight(), height);

        if (show != bottomPanelVisible)
            setBottomPanelVisible (show);
        else
            resized();
    };
    bottomResizer.onDragEnd = [this] { commands.commandStatusChanged(); };
    bottomResizer.onDoubleClick = [this]
    {
        bottomPanelHeight = BottomPanel::preferredHeight;
        setBottomPanelVisible (true);
        resized();
    };

    addAndMakeVisible (libraryResizer);
    addAndMakeVisible (bottomResizer);
}

int MainComponent::getMaxBottomPanelHeight() const
{
    const int available = getHeight() - size::menuBarHeight - size::toolbarHeight;
    return juce::jmax (BottomPanel::preferredHeight, available - minArrangementHeight);
}

void MainComponent::setLibraryVisible (bool shouldShow)
{
    libraryVisible = shouldShow;
    resized();
    commands.commandStatusChanged();
}

void MainComponent::setBottomPanelVisible (bool shouldShow)
{
    if (bottomPanelVisible == shouldShow)
        return;

    bottomPanelVisible = shouldShow;
    bottomPanel.setCollapsed (! shouldShow);
    resized();
    commands.commandStatusChanged();
}

//==============================================================================
void MainComponent::refreshAll()
{
    arrangement.refresh();
    bottomPanel.refresh();
    library.refresh();
    updateSessionStatus();

    // Tells menus and shortcuts to re-query getCommandInfo (e.g. Undo enabled).
    commands.commandStatusChanged();
}


void MainComponent::sessionChanged (const ModelChanges& changes)
{
    // The selected track or region may have been deleted, undone, or gone
    // with the old session. (Notes and knobs can't remove either.)
    if (changes.session || changes.tracks || changes.regions)
        selection.validate();

    if (changes.session)
    {
        refreshAll();
        return;
    }

    // Tempo, time signature and key move every region on the beat grid.
    if (changes.regions || changes.transport)
    {
        arrangement.refresh();
    }
    else
    {
        if (changes.tracks || changes.mixer)
            arrangement.refreshTracks();

        if (! changes.changedRegions.empty())
            arrangement.refreshRegions (changes.changedRegions);
    }

    bottomPanel.refresh (changes);

    if (changes.tracks)
        library.refresh();   // the selected track's sound is highlighted there

    updateSessionStatus();
    commands.commandStatusChanged();
}

void MainComponent::changeListenerCallback (juce::ChangeBroadcaster*)
{
    // The selection changed: the regions themselves didn't, so only the
    // highlights, the bottom panel and the library's highlighted sound move.
    arrangement.refreshTracks();
    bottomPanel.refresh();
    library.refresh();
    commands.commandStatusChanged();
}

void MainComponent::updateSessionStatus()
{
    const SessionStatus status { engine.getSessionName(), engine.isUntitled(), engine.hasUnsavedChanges() };

    if (status != shownSessionStatus)
    {
        shownSessionStatus = status;
        menuBar.setSessionStatus (status.name, status.untitled, status.unsaved);
    }
}

juce::Component::WindowControlKind MainComponent::getWindowControlAt (juce::Point<float> p) const
{
    // Only the menu strip acts as a title bar; everything else is app content.
    if (menuBar.getBounds().toFloat().contains (p))
        return menuBar.getWindowControlAt (p - menuBar.getPosition().toFloat());

    return WindowControlKind::client;
}

void MainComponent::timerCallback()
{
    menuBar.updateWindowState();
    toolbar.update (libraryVisible);
    arrangement.updateTransport();
    bottomPanel.update();
    library.update();
    updateSessionStatus();   // "unsaved" isn't broadcast, so check it here (cheap: it only tells the menu bar on a change)
}

//==============================================================================
void MainComponent::getAllCommands (juce::Array<juce::CommandID>& ids)
{
    const juce::CommandID all[] { CommandIDs::newSession, CommandIDs::openSession, CommandIDs::saveSession,
                                   CommandIDs::saveSessionAs, CommandIDs::closeSession, CommandIDs::audioSettings,
                                   CommandIDs::undo, CommandIDs::redo, CommandIDs::deleteSelection,
                                   CommandIDs::newTrack, CommandIDs::newAudioTrack, CommandIDs::deleteTrack,
                                   CommandIDs::muteTrack, CommandIDs::soloTrack, CommandIDs::armTrack,
                                   CommandIDs::duplicateTrackSettings, CommandIDs::duplicateTrack,
                                   CommandIDs::renameTrack, CommandIDs::moveTrackUp, CommandIDs::moveTrackDown,
                                   CommandIDs::newRegion, CommandIDs::splitRegion, CommandIDs::duplicateRegion,
                                   CommandIDs::cutRegions, CommandIDs::copyRegions, CommandIDs::pasteRegions,
                                   CommandIDs::renameRegion, CommandIDs::deleteRegion,
                                   CommandIDs::togglePlay, CommandIDs::toggleRecord, CommandIDs::goToStart, CommandIDs::toggleLoop,
                                   CommandIDs::toggleMetronome, CommandIDs::toggleCountIn,
                                   CommandIDs::toggleLibrary, CommandIDs::toggleBottomPanel, CommandIDs::zoomIn, CommandIDs::zoomOut,
                                   CommandIDs::snapToBar, CommandIDs::snapToBeat, CommandIDs::snapOff,
                                   CommandIDs::showSoundTab, CommandIDs::showEditorTab, CommandIDs::showEqTab,
                                   CommandIDs::showMixerTab, CommandIDs::toggleDetails,
                                   CommandIDs::toggleFullScreen, CommandIDs::moreSounds, CommandIDs::about,
                                   juce::StandardApplicationCommandIDs::quit };
    ids.addArray (all, juce::numElementsInArray (all));
}

void MainComponent::getCommandInfo (juce::CommandID id, juce::ApplicationCommandInfo& info)
{
    // ModifierKeys::commandModifier is Cmd on macOS and Ctrl elsewhere.
    const auto cmd = juce::ModifierKeys::commandModifier;
    const auto shift = juce::ModifierKeys::shiftModifier;
    auto track = engine.getTracks().getTrack (selection.getTrack());
    const bool hasTrack = track.has_value();
    const bool hasRegions = selection.hasRegions();
    const auto transport = engine.getTransportState();

    switch (id)
    {
        case CommandIDs::newSession:
            info.setInfo ("New Session...", "Start a new session from a template", "File", 0);
            info.addDefaultKeypress ('n', cmd);
            break;
        case CommandIDs::openSession:
            info.setInfo ("Open Session...", "Open a saved session", "File", 0);
            info.addDefaultKeypress ('o', cmd);
            break;
        case CommandIDs::saveSession:
            info.setInfo ("Save", "Save the session", "File", 0);
            info.addDefaultKeypress ('s', cmd);
            break;
        case CommandIDs::saveSessionAs:
            info.setInfo ("Save As...", "Save the session under a new name", "File", 0);
            info.addDefaultKeypress ('s', cmd | shift);
            break;
        case CommandIDs::closeSession:
            info.setInfo ("Close Session", "Close the session and choose another", "File", 0);
            info.addDefaultKeypress ('w', cmd);
            break;
        case CommandIDs::audioSettings:
            info.setInfo ("Audio & MIDI Settings...", "Choose audio and MIDI devices", "File", 0);
            info.addDefaultKeypress (',', cmd);
            break;

        case CommandIDs::undo:
            info.setInfo (juce::String ("Undo " + engine.getUndoDescription()).trimEnd(),
                          "Undo the last change", "Edit", 0);
            info.setActive (engine.canUndo());
            info.addDefaultKeypress ('z', cmd);
            break;
        case CommandIDs::redo:
            info.setInfo (juce::String ("Redo " + engine.getRedoDescription()).trimEnd(),
                          "Redo the last undone change", "Edit", 0);
            info.setActive (engine.canRedo());
            info.addDefaultKeypress ('z', cmd | shift);
           #if ! JUCE_MAC
            info.addDefaultKeypress ('y', cmd);
           #endif
            break;
        case CommandIDs::deleteSelection:
            // No shortcut: the Delete key deletes regions (Region > Delete),
            // so pressing it with nothing selected can't remove a whole track.
            info.setInfo ("Delete", "Delete the selected regions, or the selected track", "Edit", 0);
            info.setActive (hasTrack);
            break;

        case CommandIDs::newTrack:
            info.setInfo ("New Instrument Track", "Add a track with the built-in synth", "Track", 0);
            info.addDefaultKeypress ('n', cmd | shift);
            break;
        case CommandIDs::newAudioTrack:
            info.setInfo ("New Audio Track", "Add a track for recording a microphone or guitar", "Track", 0);
            info.addDefaultKeypress ('a', cmd | shift);
            break;
        case CommandIDs::armTrack:
            info.setInfo ("Arm for Recording", "Record onto the selected track", "Track", 0);
            info.setActive (hasTrack && ! transport.recording);
            info.setTicked (hasTrack && track->armed);
            break;
        case CommandIDs::deleteTrack:
            info.setInfo ("Delete Track", "Delete the selected track", "Track", 0);
            info.setActive (hasTrack);
            break;
        case CommandIDs::muteTrack:
            info.setInfo ("Mute", "Mute the selected track", "Track", 0);
            info.setActive (hasTrack);
            info.setTicked (hasTrack && track->muted);
            info.addDefaultKeypress ('m', cmd);
            break;
        case CommandIDs::soloTrack:
            info.setInfo ("Solo", "Solo the selected track", "Track", 0);
            info.setActive (hasTrack);
            info.setTicked (hasTrack && track->solo);
            break;
        case CommandIDs::duplicateTrackSettings:
            info.setInfo ("New Track with Duplicate Settings", "Add an empty track with the selected track's sound and settings", "Track", 0);
            info.setActive (hasTrack);
            info.addDefaultKeypress ('d', cmd | shift);
            break;
        case CommandIDs::duplicateTrack:
            info.setInfo ("Duplicate Track", "Copy the selected track, with its settings and regions", "Track", 0);
            info.setActive (hasTrack);
            break;
        case CommandIDs::renameTrack:
            info.setInfo ("Rename Track", "Rename the selected track", "Track", 0);
            info.setActive (hasTrack);
            break;
        case CommandIDs::moveTrackUp:
            info.setInfo ("Move Track Up", "Move the selected track up the list", "Track", 0);
            info.setActive (hasTrack && engine.getTracks().canMoveTrack (track->id, -1));
            break;
        case CommandIDs::moveTrackDown:
            info.setInfo ("Move Track Down", "Move the selected track down the list", "Track", 0);
            info.setActive (hasTrack && engine.getTracks().canMoveTrack (track->id, 1));
            break;

        case CommandIDs::newRegion:
            info.setInfo ("New Chord Region at Playhead", "Add a 4-bar MIDI region with a chord pattern", "Region", 0);
            info.setActive (hasTrack);
            break;
        case CommandIDs::splitRegion:
            info.setInfo ("Split at Playhead", "Cut the selected regions in two at the playhead", "Region", 0);
            info.setActive (canSplitAtPlayhead());
            info.addDefaultKeypress ('t', cmd);
            break;
        case CommandIDs::duplicateRegion:
            info.setInfo ("Duplicate", "Copy the selected regions to just after them", "Region", 0);
            info.setActive (hasRegions);
            info.addDefaultKeypress ('d', cmd);
            break;
        case CommandIDs::cutRegions:
            info.setInfo ("Cut", "Move the selected regions to the clipboard", "Region", 0);
            info.setActive (hasRegions);
            info.addDefaultKeypress ('x', cmd);
            break;
        case CommandIDs::copyRegions:
            info.setInfo ("Copy", "Copy the selected regions", "Region", 0);
            info.setActive (hasRegions);
            info.addDefaultKeypress ('c', cmd);
            break;
        case CommandIDs::pasteRegions:
            info.setInfo ("Paste at Playhead", "Paste the copied regions on the selected track at the playhead", "Region", 0);
            info.setActive (hasTrack && engine.getTracks().regions().canPaste());
            info.addDefaultKeypress ('v', cmd);
            break;
        case CommandIDs::renameRegion:
            info.setInfo ("Rename...", "Rename the selected region", "Region", 0);
            info.setActive (selection.getRegion() != 0);
            info.addDefaultKeypress (juce::KeyPress::F2Key, 0);
            break;
        case CommandIDs::deleteRegion:
            info.setInfo (selection.getRegions().size() > 1 ? "Delete Regions" : "Delete Region",
                          "Delete the selected regions", "Region", 0);
            info.setActive (hasRegions);
            info.addDefaultKeypress (juce::KeyPress::deleteKey, 0);
            info.addDefaultKeypress (juce::KeyPress::backspaceKey, 0);
            break;

        case CommandIDs::togglePlay:
            info.setInfo (transport.playing ? "Stop" : "Play", "Start or stop playback", "Transport", 0);
            info.addDefaultKeypress (juce::KeyPress::spaceKey, 0);
            break;
        case CommandIDs::toggleRecord:
            info.setInfo (transport.recording ? "Stop Recording" : "Record", "Record onto the armed or selected track", "Transport", 0);
            info.setActive (hasTrack);
            info.addDefaultKeypress ('r', 0);
            break;
        case CommandIDs::goToStart:
            info.setInfo ("Go to Start", "Move the playhead to the beginning", "Transport", 0);
            info.addDefaultKeypress (juce::KeyPress::homeKey, 0);
            info.addDefaultKeypress (juce::KeyPress::returnKey, 0);
            break;
        case CommandIDs::toggleLoop:
            info.setInfo ("Loop", "Turn looping on or off", "Transport", 0);
            info.setTicked (transport.looping);
            info.addDefaultKeypress ('l', cmd);
            break;
        case CommandIDs::toggleMetronome:
            info.setInfo ("Metronome", "Turn the click on or off", "Transport", 0);
            info.setTicked (transport.metronome);
            info.addDefaultKeypress ('u', cmd);
            break;
        case CommandIDs::toggleCountIn:
            info.setInfo ("Count-in", "Play one bar of clicks before recording starts", "Transport", 0);
            info.setTicked (transport.countIn);
            break;

        case CommandIDs::toggleLibrary:
            info.setInfo ("Show Library", "Show or hide the sound library", "View", 0);
            info.setTicked (libraryVisible);
            info.addDefaultKeypress ('y', cmd | shift);
            break;
        case CommandIDs::toggleBottomPanel:
            info.setInfo ("Show Bottom Panel", "Show or hide the Sound, Editor and Mixer panel", "View", 0);
            info.setTicked (bottomPanelVisible);
            info.addDefaultKeypress ('b', cmd | shift);
            break;
        case CommandIDs::zoomIn:
            info.setInfo ("Zoom In", "Zoom in on the timeline", "View", 0);
            info.addDefaultKeypress ('=', cmd);
            info.addDefaultKeypress ('+', cmd);
            break;
        case CommandIDs::zoomOut:
            info.setInfo ("Zoom Out", "Zoom out on the timeline", "View", 0);
            info.addDefaultKeypress ('-', cmd);
            break;
        case CommandIDs::snapToBar:
            info.setInfo ("Snap to Bars", "Regions and edges land on bar lines", "View", 0);
            info.setTicked (arrangement.getSnap() == ArrangementView::Snap::bar);
            break;
        case CommandIDs::snapToBeat:
            info.setInfo ("Snap to Beats", "Regions and edges land on beat lines", "View", 0);
            info.setTicked (arrangement.getSnap() == ArrangementView::Snap::beat);
            break;
        case CommandIDs::snapOff:
            info.setInfo ("Snap Off", "Regions and edges go exactly where you drop them", "View", 0);
            info.setTicked (arrangement.getSnap() == ArrangementView::Snap::off);
            break;
        case CommandIDs::showSoundTab:
            info.setInfo ("Sound", "Show the Sound controls", "View", 0);
            info.setTicked (! bottomPanel.isMixerShown() && bottomPanel.getTab() == BottomPanel::Tab::sound);
            info.addDefaultKeypress ('1', cmd);
            break;
        case CommandIDs::showEqTab:
            info.setInfo ("EQ", "Show the EQ", "View", 0);
            info.setTicked (! bottomPanel.isMixerShown() && bottomPanel.getTab() == BottomPanel::Tab::eq);
            info.addDefaultKeypress ('3', cmd);
            break;
        case CommandIDs::showEditorTab:
            info.setInfo ("Editor", "Show the piano roll", "View", 0);
            info.setTicked (! bottomPanel.isMixerShown() && bottomPanel.getTab() == BottomPanel::Tab::editor);
            info.addDefaultKeypress ('2', cmd);
            break;
        case CommandIDs::showMixerTab:
            info.setInfo ("Mixer", "Show every track's fader at once", "View", 0);
            info.setTicked (bottomPanel.isMixerShown());
            info.addDefaultKeypress ('4', cmd);
            break;
        case CommandIDs::toggleDetails:
            info.setInfo ("Show Details", "Show advanced track details", "View", 0);
            info.setTicked (bottomPanel.areDetailsVisible());
            break;

        case CommandIDs::toggleFullScreen:
            info.setInfo ("Full Screen", "Toggle full screen", "Window", 0);
           #if JUCE_MAC
            info.addDefaultKeypress ('f', cmd | juce::ModifierKeys::ctrlModifier);
           #else
            info.addDefaultKeypress (juce::KeyPress::F11Key, 0);
           #endif
            break;
        case CommandIDs::moreSounds:
            info.setInfo ("More Sounds...", "Download more instruments and loops", "Window", 0);
            break;

        case CommandIDs::about:
            info.setInfo ("About Ikaros Studio", "About this app", "Help", 0);
            break;

        case juce::StandardApplicationCommandIDs::quit:
            info.setInfo ("Quit", "Quit Ikaros Studio", "File", 0);
            info.addDefaultKeypress ('q', cmd);
            break;

        default:
            break;
    }
}

bool MainComponent::perform (const InvocationInfo& info)
{
    // Nothing happens while a save is being written (see SavingOverlay); a
    // change then wouldn't be in the file, yet would look saved.
    if (saving)
        return true;

    auto& tracks = engine.getTracks();
    const auto trackId = selection.getTrack();

    switch (info.commandID)
    {
        case CommandIDs::newSession:      newSession(); break;
        case CommandIDs::openSession:     openSession(); break;
        case CommandIDs::saveSession:     saveSession(); break;
        case CommandIDs::saveSessionAs:   saveSessionAs(); break;
        case CommandIDs::closeSession:    closeSession(); break;
        case CommandIDs::audioSettings:   showAudioSettings(); break;

        case CommandIDs::undo:            engine.undo(); break;
        case CommandIDs::redo:            engine.redo(); break;

        case CommandIDs::deleteSelection:
            if (selection.hasRegions())
                tracks.regions().deleteRegions (selection.getRegions());
            else if (trackId != 0)
                tracks.deleteTrack (trackId);
            break;

        case CommandIDs::newTrack:
            if (auto id = tracks.addInstrumentTrack())
                selection.selectTrack (id);
            break;

        case CommandIDs::newAudioTrack:
            if (auto id = tracks.addAudioTrack())
                selection.selectTrack (id);
            break;

        case CommandIDs::armTrack:
            if (auto t = tracks.getTrack (trackId))
                tracks.inputs().setArmed (trackId, ! t->armed);
            break;

        case CommandIDs::deleteTrack:     tracks.deleteTrack (trackId); break;

        case CommandIDs::muteTrack:
            if (auto t = tracks.getTrack (trackId))
                tracks.setMute (trackId, ! t->muted);
            break;

        case CommandIDs::soloTrack:
            if (auto t = tracks.getTrack (trackId))
                tracks.setSolo (trackId, ! t->solo);
            break;

        case CommandIDs::duplicateTrackSettings:
        case CommandIDs::duplicateTrack:
            if (auto id = tracks.duplicateTrack (trackId, info.commandID == CommandIDs::duplicateTrack))
                selection.selectTrack (id);
            break;

        case CommandIDs::renameTrack:     arrangement.startRenamingTrack (trackId); break;
        case CommandIDs::moveTrackUp:     tracks.moveTrack (trackId, -1); break;
        case CommandIDs::moveTrackDown:   tracks.moveTrack (trackId, 1); break;

        case CommandIDs::newRegion:
            if (auto rid = tracks.regions().createChordRegion (trackId, engine.getTransportState().positionBeats))
                selection.selectRegion (trackId, rid);
            break;

        case CommandIDs::splitRegion:
        {
            auto rightHalves = tracks.regions().splitRegions (getRegionsToSplit(), engine.getTransportState().positionBeats);

            if (! rightHalves.empty())
                selection.selectRegions (trackId, rightHalves);
            break;
        }

        case CommandIDs::duplicateRegion:
        {
            // Selecting the copies means pressing it again keeps adding copies.
            auto copies = tracks.regions().duplicateRegions (selection.getRegions());

            if (! copies.empty())
                selection.selectRegions (trackId, copies);
            break;
        }

        case CommandIDs::cutRegions:
            tracks.regions().copyRegions (selection.getRegions());
            tracks.regions().deleteRegions (selection.getRegions());
            break;

        case CommandIDs::copyRegions:     tracks.regions().copyRegions (selection.getRegions()); break;

        case CommandIDs::pasteRegions:
        {
            auto pasted = tracks.regions().pasteRegions (trackId, engine.getTransportState().positionBeats);

            if (auto first = pasted.empty() ? std::nullopt : tracks.regions().getRegion (pasted.front()))
                selection.selectRegions (first->trackId, pasted);
            break;
        }

        case CommandIDs::renameRegion:    arrangement.startRenaming (selection.getRegion()); break;
        case CommandIDs::deleteRegion:    tracks.regions().deleteRegions (selection.getRegions()); break;

        case CommandIDs::togglePlay:      engine.togglePlay(); break;
        case CommandIDs::toggleRecord:    engine.toggleRecord(); break;
        case CommandIDs::goToStart:       engine.goToStart(); break;
        case CommandIDs::toggleLoop:      engine.setLooping (! engine.getTransportState().looping); break;
        case CommandIDs::toggleMetronome: engine.setMetronomeEnabled (! engine.getTransportState().metronome); break;
        case CommandIDs::toggleCountIn:   engine.setCountInEnabled (! engine.getTransportState().countIn); break;

        case CommandIDs::zoomIn:          arrangement.zoomIn(); break;
        case CommandIDs::zoomOut:         arrangement.zoomOut(); break;
        case CommandIDs::snapToBar:       arrangement.setSnap (ArrangementView::Snap::bar); break;
        case CommandIDs::snapToBeat:      arrangement.setSnap (ArrangementView::Snap::beat); break;
        case CommandIDs::snapOff:         arrangement.setSnap (ArrangementView::Snap::off); break;

        case CommandIDs::toggleLibrary:     setLibraryVisible (! libraryVisible); break;
        case CommandIDs::toggleBottomPanel: setBottomPanelVisible (! bottomPanelVisible); break;

        // Picking a tab (or details) also opens the bottom panel if it's hidden.
        case CommandIDs::showSoundTab:    setBottomPanelVisible (true); bottomPanel.setTab (BottomPanel::Tab::sound); break;
        case CommandIDs::showEqTab:       setBottomPanelVisible (true); bottomPanel.setTab (BottomPanel::Tab::eq); break;
        case CommandIDs::showEditorTab:   setBottomPanelVisible (true); bottomPanel.setTab (BottomPanel::Tab::editor); break;
        case CommandIDs::showMixerTab:    setBottomPanelVisible (true); bottomPanel.setMixerShown (! bottomPanel.isMixerShown()); break;

        case CommandIDs::toggleDetails:
            setBottomPanelVisible (true);
            bottomPanel.setDetailsVisible (! bottomPanel.areDetailsVisible());
            break;

        case CommandIDs::toggleFullScreen:
            if (auto* window = dynamic_cast<juce::DocumentWindow*> (getTopLevelComponent()))
                window->setFullScreen (! window->isFullScreen());
            break;

        case CommandIDs::moreSounds:      MoreSoundsDialog::show (*this, engine); break;
        case CommandIDs::about:           showAbout(); break;
        case juce::StandardApplicationCommandIDs::quit: requestQuit(); break;

        default:
            return false;
    }

    commands.commandStatusChanged();
    return true;
}

//==============================================================================
std::vector<RegionId> MainComponent::getRegionsToSplit() const
{
    if (selection.hasRegions())
        return selection.getRegions();

    std::vector<RegionId> ids;

    for (auto& r : engine.getTracks().regions().getRegions (selection.getTrack()))
        ids.push_back (r.id);

    return ids;
}

bool MainComponent::canSplitAtPlayhead() const
{
    const auto beat = engine.getTransportState().positionBeats;

    for (auto id : getRegionsToSplit())
        if (auto r = engine.getTracks().regions().getRegion (id))
            if (beat > r->startBeat && beat < r->getEndBeat())
                return true;

    return false;
}

//==============================================================================
void MainComponent::showNewTrackDialog()
{
    // Like GarageBand: choose Software Instrument or Real Instrument first.
    NewTrackDialog::show (*this, engine, [this] (TrackId id) { selection.selectTrack (id); });
}

//==============================================================================
juce::File MainComponent::getDefaultSessionFolder()
{
    auto dir = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Ikaros Studio");
    dir.createDirectory();
    return dir;
}

void MainComponent::confirmDiscardChanges (std::function<void()> thenDo)
{
    if (! engine.hasUnsavedChanges())
    {
        thenDo();
        return;
    }

    auto options = juce::MessageBoxOptions()
                       .withIconType (juce::MessageBoxIconType::QuestionIcon)
                       .withTitle ("Save changes?")
                       .withMessage ("Do you want to save the changes to \"" + engine.getSessionName() + "\"?")
                       .withButton ("Save")
                       .withButton ("Don't Save")
                       .withButton ("Cancel")
                       .withAssociatedComponent (this);

    // showAsync's callback gets 1 for the first button, 2 for the second, and
    // 0 for the last one (Cancel). SafePointer guards against this component
    // being deleted while the dialog is open.
    juce::AlertWindow::showAsync (options, [safeThis = SafePointer<MainComponent> (this), thenDo] (int result)
    {
        if (safeThis == nullptr)
            return;

        if (result == 1)
            safeThis->saveSession ([thenDo] (bool saved) { if (saved) thenDo(); });
        else if (result == 2)
            thenDo();
    });
}

void MainComponent::newSession()
{
    showSessionChooser (SessionChooser::Page::newSession);
}

void MainComponent::openSession()
{
    showSessionChooser (SessionChooser::Page::recent);
}

// Like closing a GarageBand project: after asking about unsaved changes,
// the session is replaced by an empty one and the chooser comes up.
void MainComponent::closeSession()
{
    confirmDiscardChanges ([this]
    {
        engine.newSession();
        showSessionChooser (engine.getRecentSessions().empty() ? SessionChooser::Page::newSession
                                                               : SessionChooser::Page::recent);
    });
}

void MainComponent::showSessionChooser (SessionChooser::Page page)
{
    // The chooser only reports the choice; unsaved changes are asked about
    // after it, so cancelling the chooser never loses anything.
    SessionChooser::Callbacks callbacks;

    callbacks.onNew = [this] (const NewSessionSettings& settings)
    {
        confirmDiscardChanges ([this, settings] { engine.newSession (settings); });
    };

    callbacks.onOpen = [this] (const juce::File& file)
    {
        confirmDiscardChanges ([this, file] { openSessionFile (file); });
    };

    callbacks.onOpenOther = [this] { openOtherSession(); };

    SessionChooser::show (*this, engine, page, std::move (callbacks));
}

void MainComponent::openOtherSession()
{
    confirmDiscardChanges ([this]
    {
        // Bundles, and session folders from earlier versions.
        fileChooser = std::make_unique<juce::FileChooser> ("Open Session", getDefaultSessionFolder(),
                                                           juce::String ("*") + AudioEngine::sessionFileExtension
                                                             + ";*" + AudioEngine::legacySessionFileExtension);

        fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                  [this] (const juce::FileChooser& fc)
        {
            if (auto file = fc.getResult(); file != juce::File())
                openSessionFile (file);
        });
    });
}

void MainComponent::openSessionFile (const juce::File& file)
{
    if (! engine.openSession (file))
        showError ("Couldn't open session", "\"" + file.getFileName() + "\" isn't a session Ikaros Studio can read, "
                                            "or it's damaged.");
}

void MainComponent::openFileFromSystem (const juce::File& file)
{
    // Double-clicked in the file manager (or passed on the command line):
    // it replaces the session chooser, after asking about unsaved changes.
    if (saving || ! file.existsAsFile())
        return;

    SessionChooser::dismiss (*this);
    confirmDiscardChanges ([this, file] { openSessionFile (file); });
}

void MainComponent::save (const juce::File& bundle, std::function<void (bool)> afterSave)
{
    // The picture for the session chooser goes into the session first.
    writeSessionPreview();
    auto job = engine.beginSave (bundle);

    if (job == nullptr)
    {
        showError ("Couldn't save session", "Ikaros Studio couldn't write \"" + bundle.getFullPathName()
                                            + "\". Check that the folder exists and that you can write to it.");
        if (afterSave)
            afterSave (false);

        return;
    }

    // Packing the audio into the file runs in the background (see SavingOverlay).
    saving = true;

    SavingOverlay::run (*this, engine, std::move (job), [this, bundle, afterSave] (bool ok)
    {
        saving = false;

        if (! ok)
            showError ("Couldn't save session", "Ikaros Studio couldn't write \"" + bundle.getFullPathName()
                                                + "\". Check that there's enough space on the disk.");
        if (afterSave)
            afterSave (ok);
    });
}

void MainComponent::saveSession (std::function<void (bool)> afterSave)
{
    if (engine.isUntitled())
    {
        saveSessionAs (std::move (afterSave));
        return;
    }

    save (engine.getSessionFile(), std::move (afterSave));
}

void MainComponent::saveSessionAs (std::function<void (bool)> afterSave)
{
    // A session is one file ("My Song.iksession").
    auto initial = engine.isUntitled() ? getDefaultSessionFolder().getChildFile (engine.getSessionName() == "Untitled Session"
                                                                                    ? juce::String ("My Session")
                                                                                    : engine.getSessionName())
                                       : engine.getSessionFile();

    fileChooser = std::make_unique<juce::FileChooser> ("Save Session As", initial,
                                                       juce::String ("*") + AudioEngine::sessionFileExtension);

    fileChooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
                              [this, afterSave] (const juce::FileChooser& fc)
    {
        auto chosen = fc.getResult();

        if (chosen == juce::File())
        {
            if (afterSave)
                afterSave (false);

            return;
        }

        // The dialog may hand back the name as typed, without the extension.
        const auto target = AudioEngine::getSessionFileFor (chosen);

        auto save = [this, target, afterSave]
        {
            this->save (target, afterSave);
        };

        if (! target.exists() || target == engine.getSessionFile())
        {
            save();
            return;
        }

        auto options = juce::MessageBoxOptions()
                           .withIconType (juce::MessageBoxIconType::WarningIcon)
                           .withTitle ("Replace session?")
                           .withMessage ("A session named \"" + target.getFileNameWithoutExtension()
                                           + "\" already exists in " + target.getParentDirectory().getFullPathName()
                                           + ". Replace it?")
                           .withButton ("Replace")
                           .withButton ("Cancel")
                           .withAssociatedComponent (this);

        juce::AlertWindow::showAsync (options, [save, afterSave] (int result)
        {
            if (result == 1)
                save();
            else if (afterSave)
                afterSave (false);
        });
    });
}

// A picture of the tracks for the session chooser, saved into the session.
// It's taken from what's on screen, so it shows the session the way the user
// left it.
void MainComponent::writeSessionPreview()
{
    if (arrangement.getWidth() <= 0 || arrangement.getHeight() <= 0)
        return;

    // The left part (track names and the first bars), in the chooser tiles'
    // 16:10 shape, reads better at tile size than the whole arrangement.
    auto area = arrangement.getLocalBounds();
    area = area.withWidth (juce::jmin (area.getWidth(), area.getHeight() * 16 / 10));
    auto snapshot = arrangement.createComponentSnapshot (area, true, 1.0f);
    const int width = juce::jmin (800, snapshot.getWidth());
    auto image = snapshot.rescaled (width, snapshot.getHeight() * width / snapshot.getWidth(),
                                    juce::Graphics::highResamplingQuality);

    auto file = engine.getPreviewFile();
    file.deleteFile();   // FileOutputStream appends to an existing file
    juce::FileOutputStream out (file);
    juce::PNGImageFormat png;

    if (out.openedOk())
        png.writeImageToStream (image, out);

    // The chooser caches decoded previews by file; drop the old picture.
    juce::ImageCache::releaseUnusedImages();
}

void MainComponent::requestQuit()
{
    if (saving)
        return;   // the save finishes first (it only takes a moment)

    confirmDiscardChanges ([] { juce::JUCEApplication::getInstance()->quit(); });
}

void MainComponent::showAudioSettings()
{
    juce::DialogWindow::LaunchOptions o;
    o.dialogTitle = "Audio & MIDI Settings";
    o.dialogBackgroundColour = colour::toolbarBg;
    o.content.setOwned (engine.createAudioSettingsComponent().release());   // the dialog takes ownership
    o.useNativeTitleBar = true;
    o.resizable = false;
    o.launchAsync();
}

void MainComponent::showAbout()
{
    juce::AlertWindow::showAsync (juce::MessageBoxOptions()
                                      .withIconType (juce::MessageBoxIconType::InfoIcon)
                                      .withTitle ("Ikaros Studio")
                                      .withMessage ("Version " + juce::JUCEApplication::getInstance()->getApplicationVersion()
                                                    + "\n\nAn easy-first music studio.\nBuilt with JUCE and Tracktion Engine.")
                                      .withButton ("OK")
                                      .withAssociatedComponent (this),
                                  nullptr);
}

void MainComponent::showError (const juce::String& title, const juce::String& message)
{
    juce::AlertWindow::showAsync (juce::MessageBoxOptions()
                                      .withIconType (juce::MessageBoxIconType::WarningIcon)
                                      .withTitle (title)
                                      .withMessage (message)
                                      .withButton ("OK")
                                      .withAssociatedComponent (this),
                                  nullptr);
}

} // namespace ikaros
