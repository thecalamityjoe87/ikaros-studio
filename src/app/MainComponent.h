#pragma once

#include "app/AppMenuModel.h"
#include "engine/AudioEngine.h"
#include "ui/arrangement/ArrangementView.h"
#include "ui/bottompanel/BottomPanel.h"
#include "ui/library/LibrarySidebar.h"
#include "ui/titlebar/MenuBar.h"
#include "ui/state/Selection.h"
#include "ui/toolbar/Toolbar.h"

namespace ikaros
{

//==============================================================================
/**
    The window's content: lays out the menu bar, toolbar, library, arrangement
    and bottom panel, and handles every application command (menus + shortcuts).

    It also drives the UI's two refresh paths:
      - model changes (tracks, regions, undo...) arrive as change messages from
        AudioEngine and trigger a full refresh();
      - fast-moving state (playhead, meters, CPU) is polled by a 30 Hz Timer.
*/
class MainComponent  : public juce::Component,
                       public juce::ApplicationCommandTarget,
                       private juce::ChangeListener,
                       private juce::Timer
{
public:
    explicit MainComponent (AudioEngine&);
    ~MainComponent() override;

    /** Asks to save unsaved changes, then quits. Called for the close button and Quit. */
    void requestQuit();

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
    ArrangementView arrangement { engine, selection };
    BottomPanel bottomPanel { engine, selection };

    juce::TooltipWindow tooltipWindow { this, 700 };
    std::unique_ptr<juce::FileChooser> fileChooser;
    bool libraryVisible = true;

    void refreshAll();
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;

    // Session commands (all asynchronous: dialogs never block the message loop)
    void newSession();
    void openSession();
    void saveSession (std::function<void (bool saved)> afterSave = nullptr);
    void saveSessionAs (std::function<void (bool saved)> afterSave = nullptr);
    void confirmDiscardChanges (std::function<void()> thenDo);
    void showAudioSettings();
    void showNewTrackDialog();
    void showAbout();
    void showError (const juce::String& title, const juce::String& message);

    static juce::File getDefaultSessionFolder();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};

} // namespace ikaros
