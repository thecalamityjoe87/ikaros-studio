#include "app/MainComponent.h"
#include "app/CommandIDs.h"
#include "ui/dialogs/NewTrackDialog.h"
#include "engine/TrackController.h"
#include "ui/theme/Tokens.h"

namespace ikaros
{

using namespace tokens;

MainComponent::MainComponent (AudioEngine& e) : engine (e)
{
    setOpaque (true);

    addAndMakeVisible (menuBar);
    addAndMakeVisible (toolbar);
    addAndMakeVisible (library);
    addAndMakeVisible (arrangement);
    addAndMakeVisible (bottomPanel);

    arrangement.onOpenRegionInEditor = [this] (RegionId) { bottomPanel.setTab (BottomPanel::Tab::editor); };
    arrangement.onAddTrackRequested = [this] (juce::Component*) { showNewTrackDialog(); };
    arrangement.onSnapChanged = [this] { commands.commandStatusChanged(); };
    toolbar.onNewTrackRequested = [this] (juce::Component*) { showNewTrackDialog(); };
    bottomPanel.onViewStateChanged = [this] { commands.commandStatusChanged(); };

    // Commands: register everything this component can do, make it the
    // default target, and let the command manager turn key presses into commands.
    commands.registerAllCommandsForTarget (this);
    commands.setFirstCommandTarget (this);
    addKeyListener (commands.getKeyMappings());

   #if JUCE_MAC
    juce::MenuBarModel::setMacMainMenu (&menuModel);
   #endif

    engine.addChangeListener (this);
    selection.addChangeListener (this);

    refreshAll();
    startTimerHz (30);   // playhead, meters, CPU
    setSize (1440, 900);
}

MainComponent::~MainComponent()
{
    stopTimer();
    engine.removeChangeListener (this);
    selection.removeChangeListener (this);

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
    auto r = getLocalBounds();
    menuBar.setBounds (r.removeFromTop (size::menuBarHeight));
    toolbar.setBounds (r.removeFromTop (size::toolbarHeight));

    library.setVisible (libraryVisible);

    if (libraryVisible)
        library.setBounds (r.removeFromLeft (size::sidebarWidth));

    bottomPanel.setBounds (r.removeFromBottom (BottomPanel::preferredHeight));
    arrangement.setBounds (r);
}

//==============================================================================
void MainComponent::refreshAll()
{
    arrangement.refresh();
    bottomPanel.refresh();
    library.refresh();
    menuBar.setSessionStatus (engine.getSessionName(), engine.isUntitled(), engine.hasUnsavedChanges());

    // Tells menus and shortcuts to re-query getCommandInfo (e.g. Undo enabled).
    commands.commandStatusChanged();
}

void MainComponent::changeListenerCallback (juce::ChangeBroadcaster* source)
{
    if (source == &engine)
        selection.validate();   // the selected track/region may have been undone or the session replaced

    refreshAll();
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
    menuBar.setSessionStatus (engine.getSessionName(), engine.isUntitled(), engine.hasUnsavedChanges());
}

//==============================================================================
void MainComponent::getAllCommands (juce::Array<juce::CommandID>& ids)
{
    const juce::CommandID all[] { CommandIDs::newSession, CommandIDs::openSession, CommandIDs::saveSession,
                                   CommandIDs::saveSessionAs, CommandIDs::audioSettings,
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
                                   CommandIDs::toggleLibrary, CommandIDs::zoomIn, CommandIDs::zoomOut,
                                   CommandIDs::snapToBar, CommandIDs::snapToBeat, CommandIDs::snapOff,
                                   CommandIDs::showSoundTab, CommandIDs::showEditorTab,
                                   CommandIDs::showMixerTab, CommandIDs::toggleDetails,
                                   CommandIDs::toggleFullScreen, CommandIDs::about,
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
            info.setInfo ("New Session", "Start a new, empty session", "File", 0);
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
            info.setActive (hasTrack && engine.getTracks().canPaste());
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
            info.setTicked (bottomPanel.getTab() == BottomPanel::Tab::sound);
            info.addDefaultKeypress ('1', cmd);
            break;
        case CommandIDs::showEditorTab:
            info.setInfo ("Editor", "Show the piano roll", "View", 0);
            info.setTicked (bottomPanel.getTab() == BottomPanel::Tab::editor);
            info.addDefaultKeypress ('2', cmd);
            break;
        case CommandIDs::showMixerTab:
            info.setInfo ("Mixer", "Show the mixer", "View", 0);
            info.setTicked (bottomPanel.getTab() == BottomPanel::Tab::mixer);
            info.addDefaultKeypress ('3', cmd);
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
    auto& tracks = engine.getTracks();
    const auto trackId = selection.getTrack();

    switch (info.commandID)
    {
        case CommandIDs::newSession:      newSession(); break;
        case CommandIDs::openSession:     openSession(); break;
        case CommandIDs::saveSession:     saveSession(); break;
        case CommandIDs::saveSessionAs:   saveSessionAs(); break;
        case CommandIDs::audioSettings:   showAudioSettings(); break;

        case CommandIDs::undo:            engine.undo(); break;
        case CommandIDs::redo:            engine.redo(); break;

        case CommandIDs::deleteSelection:
            if (selection.hasRegions())
                tracks.deleteRegions (selection.getRegions());
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
                tracks.setArmed (trackId, ! t->armed);
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
            if (auto rid = tracks.createChordRegion (trackId, engine.getTransportState().positionBeats))
                selection.selectRegion (trackId, rid);
            break;

        case CommandIDs::splitRegion:
        {
            auto rightHalves = tracks.splitRegions (getRegionsToSplit(), engine.getTransportState().positionBeats);

            if (! rightHalves.empty())
                selection.selectRegions (trackId, rightHalves);
            break;
        }

        case CommandIDs::duplicateRegion:
        {
            // Selecting the copies means pressing it again keeps adding copies.
            auto copies = tracks.duplicateRegions (selection.getRegions());

            if (! copies.empty())
                selection.selectRegions (trackId, copies);
            break;
        }

        case CommandIDs::cutRegions:
            tracks.copyRegions (selection.getRegions());
            tracks.deleteRegions (selection.getRegions());
            break;

        case CommandIDs::copyRegions:     tracks.copyRegions (selection.getRegions()); break;

        case CommandIDs::pasteRegions:
        {
            auto pasted = tracks.pasteRegions (trackId, engine.getTransportState().positionBeats);

            if (auto first = pasted.empty() ? std::nullopt : tracks.getRegion (pasted.front()))
                selection.selectRegions (first->trackId, pasted);
            break;
        }

        case CommandIDs::renameRegion:    arrangement.startRenaming (selection.getRegion()); break;
        case CommandIDs::deleteRegion:    tracks.deleteRegions (selection.getRegions()); break;

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

        case CommandIDs::toggleLibrary:
            libraryVisible = ! libraryVisible;
            resized();
            break;

        case CommandIDs::showSoundTab:    bottomPanel.setTab (BottomPanel::Tab::sound); break;
        case CommandIDs::showEditorTab:   bottomPanel.setTab (BottomPanel::Tab::editor); break;
        case CommandIDs::showMixerTab:    bottomPanel.setTab (BottomPanel::Tab::mixer); break;
        case CommandIDs::toggleDetails:   bottomPanel.setDetailsVisible (! bottomPanel.areDetailsVisible()); break;

        case CommandIDs::toggleFullScreen:
            if (auto* window = dynamic_cast<juce::DocumentWindow*> (getTopLevelComponent()))
                window->setFullScreen (! window->isFullScreen());
            break;

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

    for (auto& r : engine.getTracks().getRegions (selection.getTrack()))
        ids.push_back (r.id);

    return ids;
}

bool MainComponent::canSplitAtPlayhead() const
{
    const auto beat = engine.getTransportState().positionBeats;

    for (auto id : getRegionsToSplit())
        if (auto r = engine.getTracks().getRegion (id))
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
    confirmDiscardChanges ([this] { engine.newSession(); });
}

void MainComponent::openSession()
{
    confirmDiscardChanges ([this]
    {
        fileChooser = std::make_unique<juce::FileChooser> ("Open Session", getDefaultSessionFolder(),
                                                           juce::String ("*") + AudioEngine::sessionFileExtension);

        fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                  [this] (const juce::FileChooser& fc)
        {
            auto file = fc.getResult();

            if (file != juce::File() && ! engine.openSession (file))
                showError ("Couldn't open session", "\"" + file.getFileName() + "\" isn't a session Ikaros Studio can read.");
        });
    });
}

void MainComponent::saveSession (std::function<void (bool)> afterSave)
{
    if (engine.isUntitled())
    {
        saveSessionAs (std::move (afterSave));
        return;
    }

    const bool ok = engine.saveSession();

    if (! ok)
        showError ("Couldn't save session", "Check that the folder still exists and that you can write to it.");

    if (afterSave)
        afterSave (ok);
}

void MainComponent::saveSessionAs (std::function<void (bool)> afterSave)
{
    auto initial = engine.isUntitled() ? getDefaultSessionFolder().getChildFile ("My Session")
                                       : engine.getSessionFile();

    fileChooser = std::make_unique<juce::FileChooser> ("Save Session As", initial,
                                                       juce::String ("*") + AudioEngine::sessionFileExtension);

    fileChooser->launchAsync (juce::FileBrowserComponent::saveMode
                                | juce::FileBrowserComponent::canSelectFiles
                                | juce::FileBrowserComponent::warnAboutOverwriting,
                              [this, afterSave] (const juce::FileChooser& fc)
    {
        auto file = fc.getResult();
        bool ok = false;

        if (file != juce::File())
        {
            ok = engine.saveSessionAs (file);

            if (! ok)
                showError ("Couldn't save session", "Ikaros Studio couldn't write \"" + file.getFullPathName() + "\".");
        }

        if (afterSave)
            afterSave (ok);
    });
}

void MainComponent::requestQuit()
{
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
