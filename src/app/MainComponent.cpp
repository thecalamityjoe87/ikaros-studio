#include "app/MainComponent.h"
#include "app/CommandIDs.h"
#include "ui/dialogs/AboutDialog.h"
#include "ui/dialogs/AlertDialog.h"
#include "ui/dialogs/DeviceChangePrompt.h"
#include "ui/dialogs/ExportDialog.h"
#include "ui/dialogs/NewTrackDialog.h"
#include "ui/dialogs/RecoveryPrompt.h"
#include "ui/library/ManageSoundsDialog.h"
#include "ui/library/FreesoundSetupDialog.h"
#include "ui/dialogs/SavingOverlay.h"
#include "ui/pluginwindow/PluginWindow.h"
#include "engine/PluginLibrary.h"
#include "engine/SessionViews.h"
#include "engine/TrackController.h"
#include "engine/Tuner.h"
#include "ui/theme/Tokens.h"
#include "ui/widgets/AutomationHint.h"
#include "ui/widgets/PaintProbe.h"
#include "ui/widgets/PerfOverlay.h"

namespace ikaros
{

using namespace tokens;

namespace
{
    constexpr int minLibraryWidth = 200, maxLibraryWidth = 420;

    // Dragging a panel's edge this far past its smallest size hides the panel.
    constexpr int collapseDistance = 80;

    // The bottom panel's height, remembered between runs. It's only this
    // window's business, so it's declared here rather than in Preferences.h
    // with the settings the engine reads.
    constexpr IntSetting bottomPanelHeightSetting { "bottomPanelHeight", BottomPanel::defaultHeight };
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
        bottomPanel.setTab (BottomPanel::Tab::editor);
        bottomPanel.setRegionMode (true);    // with the region's settings beside it
    };
    arrangement.onSnapChanged = [this] { commands.commandStatusChanged(); };
    arrangement.onTempoLaneToggled = [this] { commands.commandStatusChanged(); };
    arrangement.onAddTrack = [this] { showNewTrackDialog(); };   // the "Add track" row under the tracks
    bottomPanel.onViewStateChanged = [this] { commands.commandStatusChanged(); };
    bottomPanel.onCollapseRequested = [this] (bool collapse) { setBottomPanelVisible (! collapse); };
    bottomPanel.onDownloadPackRequested = [this] (const juce::String& packId) { ManageSoundsDialog::show (*this, engine, packId); };
    arrangement.onDownloadPackRequested = [this] (const juce::String& packId) { ManageSoundsDialog::show (*this, engine, packId); };
    library.onManageSoundsRequested = [this] { ManageSoundsDialog::show (*this, engine); };
    library.onFreesoundSetupRequested = [this] { FreesoundSetupDialog::show (*this, engine); };
    setUpResizers();

    // The engine asks for the window's view of the session when it saves
    // and before the session is replaced.
    engine.setViewStateSource ([this] { return createViewState(); });

    // Commands: register everything this component can do, make it the
    // default target, and let the command manager turn key presses into commands.
    commands.registerAllCommandsForTarget (this);
    commands.setFirstCommandTarget (this);
    addKeyListener (commands.getKeyMappings());

   #if JUCE_MAC
    // The app menu's own Settings... item (Cmd+,), next to About.
    macAppMenu.addCommandItem (&commands, CommandIDs::preferences);
    juce::MenuBarModel::setMacMainMenu (&menuModel, &macAppMenu);
   #endif

    engine.addListener (this);
    engine.getPreferences().addListener (this);
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

    if (auto* perf = engine.getPerfMonitor())
        setUpPerfMonitor (*perf);
    setSize (1440, 900);

    // Like GarageBand, start with the session chooser: recent sessions if
    // there are any, otherwise the templates. On first run, offer the
    // starter sounds first. After a crash, offer to recover the unsaved
    // work before anything else. (Async: the window isn't on screen yet.)
    juce::MessageManager::callAsync ([safeThis = SafePointer<MainComponent> (this)]
    {
        if (safeThis == nullptr)
            return;

        auto showChooser = [safeThis]
        {
            if (safeThis == nullptr)
                return;

            auto& engine = safeThis->engine;

            // Some people would rather go straight back to where they were.
            if (engine.getPreferences().get (settings::onStartup) == settings::OnStartup::reopenLastSession)
                if (auto recent = engine.getRecentSessions(); ! recent.empty() && engine.openSession (recent.front().file))
                    return;

            safeThis->showSessionChooser (engine.getRecentSessions().empty() ? SessionChooser::Page::newSession
                                                                            : SessionChooser::Page::recent);
        };

        RecoveryPrompt::show (*safeThis, safeThis->engine, [safeThis, showChooser] (bool recovered)
        {
            if (safeThis == nullptr || recovered)
                return;   // a recovered session is open: no chooser

            if (ManageSoundsDialog::shouldOfferStarterSounds (safeThis->engine))
                ManageSoundsDialog::showWelcome (*safeThis, safeThis->engine, showChooser);
            else
                showChooser();
        });
    });
}

MainComponent::~MainComponent()
{
    // The app is quitting: the session opens like this next time.
    engine.rememberViewState();
    engine.setViewStateSource (nullptr);

    stopTimer();
    engine.removeListener (this);
    engine.getPreferences().removeListener (this);

    if (perfKeys != nullptr)
        removeKeyListener (perfKeys.get());
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
    IKAROS_PERF_PAINT ("MainComponent");
    g.fillAll (theme().windowBg);
}

void MainComponent::paintOverChildren (juce::Graphics&)
{
    // Everything in this repaint has been drawn by now (see PaintProbe).
    paintprobe::frameEnd();
}

//==============================================================================
// Debug only (IKAROS_PERF=1, see engine/PerfMonitor.h).
void MainComponent::setUpPerfMonitor (PerfMonitor& perf)
{
    perf.setUiSampler ([] { return paintprobe::takeSample(); });

    // Always on top of the other children, dialogs included; showing unless
    // IKAROS_PERF_OVERLAY=0.
    perfOverlay = std::make_unique<PerfOverlay> (perf);
    perfOverlay->setAlwaysOnTop (true);
    addChildComponent (*perfOverlay);
    perfOverlay->setVisible (juce::SystemStats::getEnvironmentVariable ("IKAROS_PERF_OVERLAY", "1") != "0");

    // A KeyListener sees key presses that reach this component (from it or
    // any child that doesn't use them) before the command shortcuts do.
    struct ToggleKey  : juce::KeyListener
    {
        juce::Component& overlay;
        explicit ToggleKey (juce::Component& o) : overlay (o) {}

        bool keyPressed (const juce::KeyPress& key, juce::Component*) override
        {
            const auto mods = key.getModifiers();

            if (juce::CharacterFunctions::toUpperCase ((juce::juce_wchar) key.getKeyCode()) != 'P'
                 || ! mods.isCtrlDown() || ! mods.isShiftDown())
                return false;

            overlay.setVisible (! overlay.isVisible());
            return true;
        }
    };

    perfKeys = std::make_unique<ToggleKey> (*perfOverlay);
    addKeyListener (perfKeys.get());
}

void MainComponent::scrollTimelineForPerf (double beats)
{
    auto position = arrangement.getViewPosition();
    position.scrollBeats = std::fmod (position.scrollBeats + beats, 64.0);
    arrangement.setViewPosition (position);
}

void MainComponent::closeSessionChooserForPerf()
{
    SessionChooser::dismiss (*this);
}

void MainComponent::resized()
{
    // Dialogs drawn over the window (session chooser, new track, saving) cover all of it.
    for (auto* child : getChildren())
        if (dynamic_cast<SessionChooser*> (child) != nullptr || dynamic_cast<NewTrackDialog*> (child) != nullptr
             || dynamic_cast<SavingOverlay*> (child) != nullptr || dynamic_cast<ManageSoundsDialog*> (child) != nullptr
             || dynamic_cast<AboutDialog*> (child) != nullptr || dynamic_cast<ExportDialog*> (child) != nullptr
             || dynamic_cast<PreferencesDialog*> (child) != nullptr)
            child->setBounds (getLocalBounds());

    auto r = getLocalBounds();
    menuBar.setBounds (r.removeFromTop (size::menuBarHeight));
    toolbar.setBounds (r.removeFromTop (size::toolbarHeight));

    if (perfOverlay != nullptr)
        perfOverlay->setTopRightPosition (getWidth() - 12, r.getY() + 12);

    library.setVisible (libraryVisible);

    if (libraryVisible)
    {
        libraryWidth = juce::jlimit (minLibraryWidth, maxLibraryWidth, libraryWidth);
        library.setBounds (r.removeFromLeft (libraryWidth));
    }

    const int bottomHeight = bottomPanelVisible ? juce::jlimit (BottomPanel::minHeight,
                                                                getMaxBottomPanelHeight(), bottomPanelHeight)
                                                : BottomPanel::collapsedHeight;
    bottomPanel.setBounds (r.removeFromBottom (bottomHeight));
    arrangement.setBounds (r);

    // The library's handle straddles its edge, on top of everything else. It
    // stays out while it's being dragged, even when the drag has hidden the
    // library, so dragging back out can show it again.
    const int half = ResizeHandle::thickness / 2;
    libraryResizer.setVisible (libraryVisible || libraryResizer.isDragging());
    libraryResizer.setBounds (r.getX() - half, r.getY(), ResizeHandle::thickness, bottomPanel.getBottom() - r.getY());

    // The bottom panel's handle is the grip strip along its top edge (the
    // panel says where; its bounds are in the panel's own coordinates).
    bottomResizer.setBounds (bottomPanel.getHandleBounds() + bottomPanel.getPosition());
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

    // Bottom panel: drag the grip on its top edge. Well below the smallest
    // height it collapses to a slim strip; dragging up (or clicking the
    // strip) opens it again.
    bottomPanelHeight = engine.getPreferences().get (bottomPanelHeightSetting);
    bottomResizer.setShowsGrip (true);
    bottomResizer.onDragStart = [this]
    {
        resizeStartSize = bottomPanelVisible ? bottomPanel.getHeight() : BottomPanel::collapsedHeight;
    };
    bottomResizer.onDrag = [this] (int offset)
    {
        const int height = resizeStartSize - offset;   // dragging up makes it taller
        const bool show = height > BottomPanel::minHeight - collapseDistance;

        if (show)
            bottomPanelHeight = juce::jlimit (BottomPanel::minHeight, getMaxBottomPanelHeight(), height);

        if (show != bottomPanelVisible)
            setBottomPanelVisible (show);
        else
            resized();
    };
    bottomResizer.onDragEnd = [this]
    {
        engine.getPreferences().set (bottomPanelHeightSetting, bottomPanelHeight);
        commands.commandStatusChanged();
    };
    bottomResizer.onClick = [this] { setBottomPanelVisible (true); };   // the collapsed strip opens the panel
    bottomResizer.onDoubleClick = [this]
    {
        bottomPanelHeight = BottomPanel::defaultHeight;
        engine.getPreferences().set (bottomPanelHeightSetting, bottomPanelHeight);
        setBottomPanelVisible (true);
        resized();
    };

    addAndMakeVisible (libraryResizer);
    addAndMakeVisible (bottomResizer);
}

int MainComponent::getMaxBottomPanelHeight() const
{
    // Up to half the window (the arrangement keeps the other half).
    return juce::jmax (BottomPanel::minHeight, getHeight() / 2);
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
// The session's view state. Each value is a property of a VIEW tree:
//
//   <VIEW libraryVisible="1" libraryWidth="280" bottomPanelVisible="1"
//         bottomPanelHeight="320" bottomTab="editor" mixerShown="0"
//         masterMode="0" inspectorVisible="1" lcdShowsTime="0"
//         playheadBeats="16.0" selectedTrack="1042" pixelsPerBeat="24.5"
//         scrollBeats="8.0" scrollY="0"/>
//
// A property that's missing (a view saved by an earlier version) leaves
// that part of the window as it is.
namespace view
{
    const juce::Identifier libraryVisible { "libraryVisible" }, libraryWidth { "libraryWidth" },
                           bottomPanelVisible { "bottomPanelVisible" }, bottomPanelHeight { "bottomPanelHeight" },
                           bottomTab { "bottomTab" }, mixerShown { "mixerShown" }, masterMode { "masterMode" },
                           inspectorVisible { "inspectorVisible" }, lcdShowsTime { "lcdShowsTime" },
                           playheadBeats { "playheadBeats" }, selectedTrack { "selectedTrack" },
                           pixelsPerBeat { "pixelsPerBeat" }, scrollBeats { "scrollBeats" }, scrollY { "scrollY" };

    // Tabs are stored by name, so reordering the enum doesn't change old views.
    constexpr std::pair<BottomPanel::Tab, const char*> tabNames[] =
    {
        { BottomPanel::Tab::sound, "sound" }, { BottomPanel::Tab::editor, "editor" },
        { BottomPanel::Tab::eq, "eq" },       { BottomPanel::Tab::pedals, "pedals" },
    };
}

juce::ValueTree MainComponent::createViewState()
{
    juce::ValueTree v (SessionViews::viewType);

    // nullptr: no undo manager. The view is never an undoable edit.
    v.setProperty (view::libraryVisible, libraryVisible, nullptr);
    v.setProperty (view::libraryWidth, libraryWidth, nullptr);
    v.setProperty (view::bottomPanelVisible, bottomPanelVisible, nullptr);
    v.setProperty (view::bottomPanelHeight, bottomPanelHeight, nullptr);

    for (auto [tab, name] : view::tabNames)   // C++17 structured binding: unpacks each pair
        if (tab == bottomPanel.getTab())
            v.setProperty (view::bottomTab, juce::String (name), nullptr);

    v.setProperty (view::mixerShown, bottomPanel.isMixerShown(), nullptr);
    v.setProperty (view::masterMode, bottomPanel.isMasterMode(), nullptr);
    v.setProperty (view::inspectorVisible, bottomPanel.isInspectorVisible(), nullptr);
    v.setProperty (view::lcdShowsTime, toolbar.getLcd().isShowingTime(), nullptr);
    v.setProperty (view::playheadBeats, engine.getTransportState().positionBeats, nullptr);

    // juce::var holds signed 64-bit numbers; the ID fits either way.
    v.setProperty (view::selectedTrack, (juce::int64) selection.getTrack(), nullptr);

    const auto position = arrangement.getViewPosition();
    v.setProperty (view::pixelsPerBeat, position.pixelsPerBeat, nullptr);
    v.setProperty (view::scrollBeats, position.scrollBeats, nullptr);
    v.setProperty (view::scrollY, position.scrollY, nullptr);
    return v;
}

// A session without a view of its own (a new one, or one saved before
// views were) starts like a new GarageBand project. The bottom panel's
// height is the last one dragged, which is more a preference than a
// session's own. No selectedTrack: the engine has picked the first one.
static juce::ValueTree createDefaultViewState (int bottomPanelHeight)
{
    juce::ValueTree v (SessionViews::viewType);
    v.setProperty (view::libraryVisible, true, nullptr);
    v.setProperty (view::libraryWidth, size::sidebarWidth, nullptr);
    v.setProperty (view::bottomPanelVisible, true, nullptr);
    v.setProperty (view::bottomPanelHeight, bottomPanelHeight, nullptr);
    v.setProperty (view::bottomTab, "sound", nullptr);
    v.setProperty (view::mixerShown, false, nullptr);
    v.setProperty (view::masterMode, false, nullptr);
    v.setProperty (view::inspectorVisible, true, nullptr);
    v.setProperty (view::lcdShowsTime, false, nullptr);
    v.setProperty (view::playheadBeats, 0.0, nullptr);
    v.setProperty (view::pixelsPerBeat, 0.0, nullptr);   // 0 = the starting zoom
    v.setProperty (view::scrollBeats, 0.0, nullptr);
    v.setProperty (view::scrollY, 0, nullptr);
    return v;
}

void MainComponent::applyViewState (const juce::ValueTree& opened)
{
    const auto v = opened.getNumProperties() > 0
                     ? opened
                     : createDefaultViewState (engine.getPreferences().get (bottomPanelHeightSetting));

    libraryVisible = v.getProperty (view::libraryVisible, libraryVisible);
    libraryWidth = v.getProperty (view::libraryWidth, libraryWidth);
    bottomPanelHeight = v.getProperty (view::bottomPanelHeight, bottomPanelHeight);
    setBottomPanelVisible (v.getProperty (view::bottomPanelVisible, bottomPanelVisible));
    resized();

    if (const auto transport = engine.getTransportState(); ! transport.playing)
        engine.setPositionBeats (v.getProperty (view::playheadBeats, transport.positionBeats));

    // The track first: the bottom panel's tabs are about it (only a guitar
    // track has the Pedals tab, say). A track that's gone keeps the first.
    const auto track = (TrackId) (juce::int64) v.getProperty (view::selectedTrack, 0);
    const auto tracks = engine.getTracks().getTracks();

    if (std::any_of (tracks.begin(), tracks.end(), [track] (const TrackInfo& t) { return t.id == track; }))
        selection.selectTrack (track);

    bottomPanel.setInspectorVisible (v.getProperty (view::inspectorVisible, bottomPanel.isInspectorVisible()));
    bottomPanel.setMasterMode (v.getProperty (view::masterMode, bottomPanel.isMasterMode()));

    const auto tabName = v[view::bottomTab].toString();

    for (auto [tab, name] : view::tabNames)
        if (tabName == name)
            bottomPanel.setTab (tab);   // after Master: Master has no Pedals tab

    if (v.getProperty (view::mixerShown, false))
        bottomPanel.setMixerShown (true);

    toolbar.getLcd().setShowsTime (v.getProperty (view::lcdShowsTime, toolbar.getLcd().isShowingTime()));

    if (v.hasProperty (view::pixelsPerBeat))
        arrangement.setViewPosition ({ v[view::pixelsPerBeat], v[view::scrollBeats], v[view::scrollY] });

    commands.commandStatusChanged();   // the View menu's tick marks
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

        // A session was just opened or created (a new view tree), not saved:
        // show it the way it was left, or the default layout for a new one.
        // ValueTree's != compares which tree it is, not what's in it.
        if (auto opened = engine.getViewState(); opened != appliedViewState)
        {
            appliedViewState = opened;
            applyViewState (opened);
        }

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

    // Picking a Drummer region opens the Drummer editor, like GarageBand.
    if (selection.getRegion() != lastSelectedRegion)
    {
        lastSelectedRegion = selection.getRegion();

        if (engine.getTracks().drummer().isDrummerRegion (lastSelectedRegion))
        {
            setBottomPanelVisible (true);
            bottomPanel.setTab (BottomPanel::Tab::editor);
        }
    }
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
    controlSurface.update();
    updateSessionStatus();   // "unsaved" isn't broadcast, so check it here (cheap: it only tells the menu bar on a change)

    if (auto report = engine.takeRecordingReport())
        showRecordingReport (*report);

    // One question at a time, and not in the middle of a take.
    if (! deviceChangeOpen && ! engine.getTransportState().recording)
    {
        if (auto prompt = engine.takeAudioDevicePrompt())
        {
            deviceChangeOpen = true;
            showDeviceChangePrompt (engine, *prompt, this,
                                    [safeThis = SafePointer<MainComponent> (this)]
                                    {
                                        if (safeThis != nullptr)
                                            safeThis->deviceChangeOpen = false;
                                    });
        }
    }
}

//==============================================================================
void MainComponent::getAllCommands (juce::Array<juce::CommandID>& ids)
{
    const juce::CommandID all[] { CommandIDs::newSession, CommandIDs::openSession, CommandIDs::saveSession,
                                   CommandIDs::saveSessionAs, CommandIDs::closeSession,
                                   CommandIDs::exportSong, CommandIDs::exportStems,
                                   CommandIDs::undo, CommandIDs::redo, CommandIDs::deleteSelection, CommandIDs::preferences,
                                   CommandIDs::newTrack, CommandIDs::newAudioTrack, CommandIDs::deleteTrack,
                                   CommandIDs::muteTrack, CommandIDs::soloTrack, CommandIDs::armTrack,
                                   CommandIDs::duplicateTrackSettings, CommandIDs::duplicateTrack,
                                   CommandIDs::renameTrack, CommandIDs::moveTrackUp, CommandIDs::moveTrackDown,
                                   CommandIDs::newRegion, CommandIDs::splitRegion, CommandIDs::duplicateRegion,
                                   CommandIDs::cutRegions, CommandIDs::copyRegions, CommandIDs::pasteRegions,
                                   CommandIDs::renameRegion, CommandIDs::deleteRegion, CommandIDs::convertToMidiRegion,
                                   CommandIDs::makeInstrumentFromRegion,
                                   CommandIDs::togglePlay, CommandIDs::toggleRecord, CommandIDs::goToStart, CommandIDs::toggleLoop,
                                   CommandIDs::toggleMetronome, CommandIDs::countInOff, CommandIDs::countInOneBar,
                                   CommandIDs::countInTwoBars, CommandIDs::toggleMidiCycleTakes,
                                   CommandIDs::toggleLibrary, CommandIDs::toggleBottomPanel, CommandIDs::zoomIn, CommandIDs::zoomOut, CommandIDs::zoomReset,
                                   CommandIDs::snapToBar, CommandIDs::snapToBeat, CommandIDs::snapOff,
                                   CommandIDs::showSoundTab, CommandIDs::showEditorTab, CommandIDs::showEqTab,
                                   CommandIDs::showMixerTab, CommandIDs::toggleAutomation, CommandIDs::toggleTempoLane,
                                   CommandIDs::compactTracks, CommandIDs::tallTracks, CommandIDs::toggleInspector,
                                   CommandIDs::toggleFullScreen, CommandIDs::manageSounds, CommandIDs::freesoundKey, CommandIDs::about,
                                   CommandIDs::showPlugins, CommandIDs::toggleTuner, CommandIDs::closeTuner,
                                   CommandIDs::showPedalboardTab,
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
        case CommandIDs::exportSong:
            info.setInfo ("Export Song...", "Save the song as a WAV, FLAC or OGG file", "File", 0);
            info.addDefaultKeypress ('e', cmd);
            info.setActive (engine.getExporter().getSongEndBeats() > 0.0);
            break;
        case CommandIDs::exportStems:
            info.setInfo ("Export Stems...", "Save each track as its own audio file", "File", 0);
            info.addDefaultKeypress ('e', cmd | shift);
            info.setActive (engine.getExporter().getSongEndBeats() > 0.0);
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
        case CommandIDs::preferences:
            info.setInfo (juce::String (PreferencesDialog::getName()) + "...", "Change the app's settings", "Edit", 0);
            info.addDefaultKeypress (',', cmd);
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
            // On a Drummer track it adds a Drummer region instead.
            if (hasTrack && track->drummer)
                info.setInfo ("New Drummer Region at Playhead", "Add an 8-bar Drummer region", "Region", 0);
            else
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
        case CommandIDs::convertToMidiRegion:
            info.setInfo ("Convert to MIDI Region", "Turn the Drummer region into a MIDI region whose notes you can edit",
                          "Region", 0);
            info.setActive (engine.getTracks().drummer().isDrummerRegion (selection.getRegion()));
            break;
        case CommandIDs::makeInstrumentFromRegion:
        {
            info.setInfo ("Make Instrument from Region", "Play the selected audio region's sound on the keyboard, on a new instrument track",
                          "Region", 0);
            const auto region = engine.getTracks().regions().getRegion (selection.getRegion());
            info.setActive (selection.getRegions().size() == 1 && region.has_value() && ! region->isMidi);
            break;
        }
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

            // Without auto-arm, pressing it with nothing armed explains what to do.
            info.setActive (hasTrack || ! engine.getPreferences().get (settings::autoArmTrack));
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
        // The Count-in submenu: one of the three is ticked.
        case CommandIDs::countInOff:
            info.setInfo ("Off", "Start recording straight away", "Transport", 0);
            info.setTicked (engine.getCountInBars() == 0);
            break;

        case CommandIDs::countInOneBar:
            info.setInfo ("1 Bar", "Play one bar of clicks before recording starts", "Transport", 0);
            info.setTicked (engine.getCountInBars() == 1);
            break;

        case CommandIDs::countInTwoBars:
            info.setInfo ("2 Bars", "Play two bars of clicks before recording starts", "Transport", 0);
            info.setTicked (engine.getCountInBars() == 2);
            break;

        case CommandIDs::toggleMidiCycleTakes:
            info.setInfo ("Keep MIDI Loop Passes as Takes",
                          "When recording MIDI with the loop on, keep each pass as a take instead of merging them", "Transport", 0);
            info.setTicked (engine.getMidiCycleTakes());
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
        case CommandIDs::zoomReset:
            info.setInfo ("Reset Zoom", "Go back to the starting zoom (16 bars across)", "View", 0);
            info.addDefaultKeypress ('0', cmd);
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
            info.setInfo ("Editor", "Show the selected region close up: the piano roll or the audio editor", "View", 0);
            info.setTicked (! bottomPanel.isMixerShown() && bottomPanel.getTab() == BottomPanel::Tab::editor);
            info.addDefaultKeypress ('2', cmd);
            break;
        case CommandIDs::compactTracks:
            info.setInfo ("Compact Tracks", "Show more tracks at once", "View", 0);
            info.setTicked (! arrangement.areTracksTall());
            break;
        case CommandIDs::tallTracks:
            info.setInfo ("Tall Tracks", "Give each track more room", "View", 0);
            info.setTicked (arrangement.areTracksTall());
            break;
        case CommandIDs::toggleTempoLane:
            info.setInfo ("Show Tempo Lane", "Show the tempo and time-signature changes under the ruler", "View", 0);
            info.setTicked (arrangement.isTempoLaneShown());
            break;
        case CommandIDs::toggleTuner:
            info.setInfo ("Tuner", "Turn the transport readout into a tuner for the selected track's input", "View", 0);
            info.setTicked (engine.getTuner().isActive());
            info.addDefaultKeypress ('k', cmd);
            break;
        case CommandIDs::closeTuner:
            // Disabled while the tuner is off, so Escape goes on to whatever
            // else uses it.
            info.setInfo ("Close Tuner", "Back to the position and session settings", "View", 0);
            info.setActive (engine.getTuner().isActive());
            info.addDefaultKeypress (juce::KeyPress::escapeKey, 0);
            break;
        case CommandIDs::toggleAutomation:
            // No shortcut: plain A (GarageBand's) plays a note on the computer keyboard here.
            info.setInfo ("Show Automation", "Show every track's automation", "View", 0);
            info.setTicked (engine.getTracks().automation().areAllLanesShown());
            break;
        case CommandIDs::showPedalboardTab:
            // Any audio track: picking it shows the track's pedalboard if it was hidden.
            info.setInfo ("Pedalboard", "Show the selected audio track's pedals", "View", 0);
            info.setActive (hasTrack && track->kind == TrackKind::audio);
            info.setTicked (! bottomPanel.isMixerShown() && bottomPanel.getTab() == BottomPanel::Tab::pedals);
            info.addDefaultKeypress ('5', cmd);
            break;
        case CommandIDs::showMixerTab:
            info.setInfo ("Mixer", "Show every track's fader at once", "View", 0);
            info.setTicked (bottomPanel.isMixerShown());
            info.addDefaultKeypress ('4', cmd);
            break;
        case CommandIDs::toggleInspector:
            info.setInfo ("Show Inspector", "Show or hide the bottom panel's inspector column", "View", 0);
            info.setTicked (bottomPanel.isInspectorVisible());
            break;

        case CommandIDs::toggleFullScreen:
            info.setInfo ("Full Screen", "Toggle full screen", "Window", 0);
           #if JUCE_MAC
            info.addDefaultKeypress ('f', cmd | juce::ModifierKeys::ctrlModifier);
           #else
            info.addDefaultKeypress (juce::KeyPress::F11Key, 0);
           #endif
            break;
        case CommandIDs::manageSounds:
            info.setInfo ("Manage Sounds...", "Download, update and remove sound packs", "Sounds", 0);
            break;
        case CommandIDs::showPlugins:
            info.setInfo ("Plug-ins...", "Show plug-in instruments and effects in the library, and scan for new ones", "Mix", 0);
            break;
        case CommandIDs::freesoundKey:
            info.setInfo ("Freesound API Key...", "Set up or change your Freesound API key", "Sounds", 0);
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
    if (saving || engine.getExporter().isRunning())
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
        case CommandIDs::exportSong:      ExportDialog::show (*this, engine, false); break;
        case CommandIDs::exportStems:     ExportDialog::show (*this, engine, true); break;

        case CommandIDs::undo:
        case CommandIDs::redo:
        {
            const auto before = selection.getContents();
            info.commandID == CommandIDs::undo ? engine.undo() : engine.redo();
            selection.selectAfterUndoOrRedo (before);
            break;
        }

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
        {
            const auto beat = engine.getTransportState().positionBeats;
            const auto rid = tracks.drummer().isDrummerTrack (trackId) ? tracks.drummer().addDrummerRegionAt (trackId, beat)
                                                                       : tracks.regions().createChordRegion (trackId, beat);

            if (rid != 0)
                selection.selectRegion (trackId, rid);
            break;
        }

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

        case CommandIDs::convertToMidiRegion:
            // The region stays selected and is now a MIDI region: the
            // Editor shows it in the piano roll.
            tracks.drummer().convertToMidiRegion (selection.getRegion());
            setBottomPanelVisible (true);
            bottomPanel.setTab (BottomPanel::Tab::editor);
            break;

        case CommandIDs::makeInstrumentFromRegion:
            // The new track is selected, with its Sound tab showing: its
            // keyboard plays the sound straight away.
            if (const auto id = tracks.makeInstrumentFromRegion (selection.getRegion()); id != 0)
            {
                selection.selectTrack (id);
                setBottomPanelVisible (true);
                bottomPanel.setTab (BottomPanel::Tab::sound);
            }
            break;
        case CommandIDs::deleteRegion:    tracks.regions().deleteRegions (selection.getRegions()); break;

        case CommandIDs::togglePlay:      engine.togglePlay(); break;
        case CommandIDs::toggleRecord:    engine.toggleRecord(); break;
        case CommandIDs::goToStart:       engine.goToStart(); break;
        case CommandIDs::toggleLoop:      engine.setLooping (! engine.getTransportState().looping); break;
        case CommandIDs::toggleMetronome: engine.setMetronomeEnabled (! engine.getTransportState().metronome); break;
        case CommandIDs::countInOff:      engine.setCountInBars (0); break;
        case CommandIDs::countInOneBar:   engine.setCountInBars (1); break;
        case CommandIDs::countInTwoBars:  engine.setCountInBars (2); break;
        case CommandIDs::toggleMidiCycleTakes: engine.setMidiCycleTakes (! engine.getMidiCycleTakes()); break;

        case CommandIDs::zoomIn:          arrangement.zoomIn(); break;
        case CommandIDs::zoomOut:         arrangement.zoomOut(); break;
        case CommandIDs::zoomReset:       arrangement.resetZoom(); break;
        case CommandIDs::toggleTuner:     engine.getTuner().setActive (! engine.getTuner().isActive()); break;
        case CommandIDs::closeTuner:      engine.getTuner().setActive (false); break;
        case CommandIDs::snapToBar:       arrangement.setSnap (ArrangementView::Snap::bar); break;
        case CommandIDs::snapToBeat:      arrangement.setSnap (ArrangementView::Snap::beat); break;
        case CommandIDs::snapOff:         arrangement.setSnap (ArrangementView::Snap::off); break;

        case CommandIDs::toggleLibrary:     setLibraryVisible (! libraryVisible); break;
        case CommandIDs::toggleBottomPanel: setBottomPanelVisible (! bottomPanelVisible); break;

        // Picking a tab (or the inspector) also opens the bottom panel if it's hidden.
        case CommandIDs::showSoundTab:    setBottomPanelVisible (true); bottomPanel.setTab (BottomPanel::Tab::sound); break;
        case CommandIDs::showEqTab:       setBottomPanelVisible (true); bottomPanel.setTab (BottomPanel::Tab::eq); break;
        case CommandIDs::showEditorTab:   setBottomPanelVisible (true); bottomPanel.setTab (BottomPanel::Tab::editor); break;
        case CommandIDs::showPedalboardTab:
            tracks.effects().setPedalboardShown (trackId, true);   // (does nothing if it's showing already)
            setBottomPanelVisible (true);
            bottomPanel.setTab (BottomPanel::Tab::pedals);
            break;
        case CommandIDs::toggleTempoLane: arrangement.setTempoLaneShown (! arrangement.isTempoLaneShown()); break;
        case CommandIDs::compactTracks:   arrangement.setTracksTall (false); break;
        case CommandIDs::tallTracks:      arrangement.setTracksTall (true); break;
        case CommandIDs::toggleAutomation:
        {
            auto& automation = engine.getTracks().automation();
            automation.setAllLanesShown (! automation.areAllLanesShown());
            break;
        }
        case CommandIDs::showMixerTab:    setBottomPanelVisible (true); bottomPanel.setMixerShown (! bottomPanel.isMixerShown()); break;

        case CommandIDs::toggleInspector:
            setBottomPanelVisible (true);
            bottomPanel.setInspectorVisible (! bottomPanel.isInspectorVisible());
            break;

        case CommandIDs::toggleFullScreen:
            if (auto* window = dynamic_cast<juce::DocumentWindow*> (getTopLevelComponent()))
                window->setFullScreen (! window->isFullScreen());
            break;

        case CommandIDs::manageSounds:      ManageSoundsDialog::show (*this, engine); break;

        // Plug-ins have no library tile: this shows the library's plug-in list.
        case CommandIDs::showPlugins:
            setLibraryVisible (true);
            library.showPlugins();
            break;
        case CommandIDs::freesoundKey:    FreesoundSetupDialog::show (*this, engine); break;
        case CommandIDs::preferences:     showPreferences(); break;
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
    // Like GarageBand: choose Software Instrument, Audio or Guitar or Bass first.
    NewTrackDialog::show (*this, engine, [this] (TrackId id)
    {
        // A new Drummer track comes with a region: pick it, so its editor opens.
        auto& tracks = engine.getTracks();
        const auto regions = tracks.drummer().isDrummerTrack (id) ? tracks.regions().getRegions (id) : std::vector<RegionInfo>();

        if (! regions.empty())
            selection.selectRegion (id, regions.front().id);
        else
            selection.selectTrack (id);
    });
}

//==============================================================================
void MainComponent::confirmDiscardChanges (std::function<void()> thenDo)
{
    if (! engine.hasUnsavedChanges())
    {
        thenDo();
        return;
    }

    auto options = juce::MessageBoxOptions()
                       .withTitle ("Save changes?")
                       .withMessage ("Do you want to save the changes to \"" + engine.getSessionName() + "\"?")
                       .withButton ("Save")
                       .withButton ("Don't Save")
                       .withButton ("Cancel")
                       .withAssociatedComponent (this);

    // The callback gets 1 for the first button, 2 for the second, and
    // 0 for the last one (Cancel). SafePointer guards against this component
    // being deleted while the dialog is open.
    AlertDialog::show (options, [safeThis = SafePointer<MainComponent> (this), thenDo] (int result)
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
        fileChooser = std::make_unique<juce::FileChooser> ("Open Session", engine.getSessionFolder(),
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
    auto initial = engine.isUntitled() ? engine.getSessionFolder().getChildFile (engine.getSessionName() == "Untitled Session"
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
                           .withTitle ("Replace session?")
                           .withMessage ("A session named \"" + target.getFileNameWithoutExtension()
                                           + "\" already exists in " + target.getParentDirectory().getFullPathName()
                                           + ". Replace it?")
                           .withButton ("Replace")
                           .withButton ("Cancel")
                           .withAssociatedComponent (this);

        AlertDialog::show (options, [save, afterSave] (int result)
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

void MainComponent::showPreferences (PreferencesDialog::Page page)
{
    PreferencesDialog::show (*this, engine, commands, page, [this] { ManageSoundsDialog::show (*this, engine); });
}

// Some settings change something in this window the moment they change.
void MainComponent::preferenceChanged (const juce::String& key)
{
    const bool all = key.isEmpty();   // reset

    if (all || key == settings::snapEnabled.key || key == settings::snapGrid.key)
        arrangement.applySnapPreference();

    if (all || key == settings::interfaceScale.key)
        applyInterfaceScale();

    if (all || key == settings::autoArmTrack.key)
        commands.commandStatusChanged();   // Record is available with no track selected, or not
}

void MainComponent::setInterfaceScale (float requested)
{
    auto& desktop = juce::Desktop::getInstance();
    desktop.setGlobalScaleFactor (requested);

    // Too big for the screen? The window can't get smaller than its minimum
    // size, so settle for the biggest scale where that still fits.
    if (auto* display = desktop.getDisplays().getPrimaryDisplay())
    {
        const auto area = display->userArea;   // in the units of the scale just set
        const float fits = juce::jmin ((float) area.getWidth() / (float) minWindowWidth,
                                       (float) area.getHeight() / (float) minWindowHeight);

        if (fits < 1.0f)
            desktop.setGlobalScaleFactor (juce::jmax (1.0f, requested * fits));
    }
}

void MainComponent::applyInterfaceScale()
{
    setInterfaceScale (engine.getPreferences().getInterfaceScale());

    // The window keeps its size on screen, so in the new units it may be smaller than allowed.
    if (auto* window = getTopLevelComponent())
        window->setSize (juce::jmax (window->getWidth(), minWindowWidth), juce::jmax (window->getHeight(), minWindowHeight));
}

void MainComponent::showAbout()
{
    AboutDialog::show (*this, engine);
}

void MainComponent::showRecordingReport (const RecordingReport& report)
{
    using Reason = RecordingStopReason;
    const juce::String kept = report.seconds > 0.5 ? " What you recorded up to then has been kept." : "";
    const juce::String tooLoud = report.clipped ? "\n\nYour input was also too loud in places (it distorted)." : "";

    switch (report.reason)
    {
        case Reason::user:
            break;

        case Reason::notStarted:
            showError ("Not Enough Disk Space",
                       "Recording needs at least 200 MB of free disk space. Free up some space, then try again.");
            return;

        case Reason::loopTooShort:
            showError ("Loop Too Short",
                       "Recording with the loop on needs a loop at least 2 seconds long. "
                       "Make the loop longer, or turn the loop off to record.");
            return;

        case Reason::noTrackArmed:
        {
            // Point at the selected track's record button (or the toolbar's, with no track to point at).
            auto* target = arrangement.getArmButton (selection.getTrack());
            HintBubble::show (target != nullptr ? *target : toolbar.getRecordButton(), "Arm a track to record");
            return;
        }

        case Reason::deviceStopped:
            showError ("Recording Stopped",
                       "The audio device stopped responding. Was it unplugged?" + kept
                         + "\n\nCheck that it's connected, then choose it in " + PreferencesDialog::getName() + " > Audio & MIDI." + tooLoud);
            return;

        case Reason::deviceChanged:
            showError ("Recording Stopped", "The audio device or its settings changed while recording." + kept + tooLoud);
            return;

        case Reason::sampleRateChanged:
            showError ("Recording Stopped",
                       "The audio device switched to a different sample rate while recording." + kept
                         + " Recordings at either rate play back correctly." + tooLoud);
            return;

        case Reason::diskFull:
            showError ("Recording Stopped",
                       "The disk is almost full, so recording stopped before it ran out of space." + kept
                         + " Free up some space to record more." + tooLoud);
            return;

        case Reason::diskWriteFailed:
            showError ("Recording Stopped",
                       "The recording couldn't be written to disk (it may be full)." + kept + tooLoud);
            return;
    }

    // A normal take: gentle hints, not dialogs. MIDI takes can't clip or drop out.
    if (! report.audio)
        return;

    if (report.clipped)
    {
        HintBubble::show (toolbar.getRecordButton(),
                          "Your input is too loud: parts of that take distorted. Turn down the gain on your "
                          "audio interface, or move back a little from the mic, and try again.");
    }
    else if (report.dropouts > 0)
    {
        HintBubble::show (toolbar.getRecordButton(),
                          juce::String (report.dropouts) + (report.dropouts == 1 ? " dropout" : " dropouts")
                            + " during that take: the computer couldn't keep up, which can leave clicks or gaps. "
                              "A larger buffer size helps.",
                          "Audio Settings", [this] { showPreferences (PreferencesDialog::Page::audioMidi); });
    }
}

void MainComponent::showError (const juce::String& title, const juce::String& message)
{
    AlertDialog::show (juce::MessageBoxOptions()
                       .withTitle (title)
                       .withMessage (message)
                       .withButton ("OK")
                       .withAssociatedComponent (this));
}

} // namespace ikaros
