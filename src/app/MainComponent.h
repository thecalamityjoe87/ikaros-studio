#pragma once

#include "app/AppMenuModel.h"
#include "app/SurfaceHandler.h"
#include "engine/AudioEngine.h"
#include "ui/arrangement/ArrangementView.h"
#include "ui/bottompanel/BottomPanel.h"
#include "ui/dialogs/SessionChooser.h"
#include "ui/library/LibrarySidebar.h"
#include "ui/preferences/PreferencesDialog.h"
#include "ui/titlebar/MenuBar.h"
#include "ui/state/Selection.h"
#include "ui/toolbar/Toolbar.h"
#include "ui/widgets/ResizeHandle.h"

namespace ikaros
{

//==============================================================================
/**
    The window's content: lays out the menu bar, toolbar, library, arrangement
    and bottom panel, and handles every application command (menus + shortcuts).

    It also drives the UI's two refresh paths:
      - model changes (tracks, regions, undo...) arrive from AudioEngine with
        what kind of thing changed (ModelChanges), and only the parts of the
        window that show it re-read the engine;
      - fast-moving state (playhead, meters, CPU) is polled by a 30 Hz Timer.
*/
class MainComponent  : public juce::Component,
                       public juce::ApplicationCommandTarget,
                       public juce::DragAndDropContainer,   // library files dragged onto the timeline
                       private AudioEngine::Listener,        // session changes
                       private Preferences::Listener,        // settings that change the window
                       private juce::ChangeListener,         // selection changes
                       private juce::Timer
{
public:
    explicit MainComponent (AudioEngine&);
    ~MainComponent() override;

    /** Asks to save unsaved changes, then quits. Called for the close button and Quit. */
    void requestQuit();

    /** Opens a session the system handed us: double-clicked in the file
        manager, or named on the command line. */
    void openFileFromSystem (const juce::File&);

    /** The smallest the window can be, in its own units (it grows with the interface size). */
    static constexpr int minWindowWidth = 1100, minWindowHeight = 700;

    /** Sets the interface size (1.0 is 100%), or the biggest one the screen
        has room for. Works while the app runs. */
    static void setInterfaceScale (float);

    /** For the custom title bar: what's at a point in this component's coordinates. */
    WindowControlKind getWindowControlAt (juce::Point<float>) const;

    void paint (juce::Graphics&) override;
    void resized() override;

    // ApplicationCommandTarget
    juce::ApplicationCommandTarget* getNextCommandTarget() override  { return nullptr; }
    void getAllCommands (juce::Array<juce::CommandID>&) override;
    void getCommandInfo (juce::CommandID, juce::ApplicationCommandInfo&) override;
    bool perform (const InvocationInfo&) override;

private:
    AudioEngine& engine;
    juce::ApplicationCommandManager commands;
    Selection selection { engine };
    AppMenuModel menuModel { commands };

    MenuBar menuBar { &menuModel };
    Toolbar toolbar { engine, commands };
    LibrarySidebar library { engine, selection };
    ArrangementView arrangement { engine, selection, commands };
    BottomPanel bottomPanel { engine, selection };
    SurfaceHandler controlSurface { engine, selection, commands };   // a Mackie Control controller, if one is plugged in

    juce::TooltipWindow tooltipWindow { this, 700 };
    std::unique_ptr<juce::FileChooser> fileChooser;
    juce::PopupMenu macAppMenu;   // macOS: Settings... in the app menu

    // Panel layout. The sizes are kept while a panel is hidden, so it comes
    // back the way it was. Each session remembers its own (see createViewState).
    ResizeHandle libraryResizer { ResizeHandle::Orientation::vertical };
    ResizeHandle bottomResizer { ResizeHandle::Orientation::horizontal };
    bool libraryVisible = true;
    bool bottomPanelVisible = true;
    int libraryWidth = tokens::size::sidebarWidth;
    int bottomPanelHeight = BottomPanel::defaultHeight;   // read from the settings in setUpResizers
    int resizeStartSize = 0;   // the panel's size when a resize drag started

    void refreshAll();

    // What the menu bar shows about the session, so it's only told when that changes.
    struct SessionStatus
    {
        juce::String name;
        bool untitled = true, unsaved = false;
        bool operator== (const SessionStatus&) const = default;   // C++20: compares every member
    };

    std::optional<SessionStatus> shownSessionStatus;
    RegionId lastSelectedRegion = 0;   // to notice a Drummer region being picked (see changeListenerCallback)

    // How the window looks for this session: panels, tabs, the LCD, zoom and
    // scroll, the playhead and the selected track. The engine saves it with
    // the session and remembers it when the session closes.
    juce::ValueTree createViewState();
    void applyViewState (const juce::ValueTree&);
    juce::ValueTree appliedViewState;   // the opened session's view, once applied (see sessionChanged)
    void updateSessionStatus();
    void setLibraryVisible (bool);
    void setBottomPanelVisible (bool);
    void setUpResizers();
    int getMaxBottomPanelHeight() const;

    /** Split works on the selected regions, or if none are selected, on the
        selected track's regions. */
    std::vector<RegionId> getRegionsToSplit() const;
    bool canSplitAtPlayhead() const;
    void sessionChanged (const ModelChanges&) override;
    void preferenceChanged (const juce::String& key) override;
    void applyInterfaceScale();
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;

    // Session commands (all asynchronous: dialogs never block the message loop)
    void newSession();          // the session chooser's templates
    void openSession();         // the session chooser's recent sessions
    void closeSession();        // back to the session chooser
    void showSessionChooser (SessionChooser::Page);
    void openOtherSession();    // the computer's file dialog
    void openSessionFile (const juce::File&);
    void writeSessionPreview();
    void saveSession (std::function<void (bool saved)> afterSave = nullptr);
    void saveSessionAs (std::function<void (bool saved)> afterSave = nullptr);
    void save (const juce::File& bundle, std::function<void (bool saved)> afterSave);
    bool saving = false;        // a save is being written (see SavingOverlay)
    void confirmDiscardChanges (std::function<void()> thenDo);
    void showPreferences (PreferencesDialog::Page = PreferencesDialog::Page::general);
    void showNewTrackDialog();
    void showAbout();
    void showError (const juce::String& title, const juce::String& message);

    /** After a take: explains why recording stopped by itself, or hints
        that the input was too loud or the computer didn't keep up. */
    void showRecordingReport (const RecordingReport&);

    bool deviceChangeOpen = false;      // the "switch to this audio device?" question is showing

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};

} // namespace ikaros
