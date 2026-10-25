#pragma once

#include "ui/state/Selection.h"
#include "ui/widgets/IconButton.h"

namespace ikaros
{

//==============================================================================
/**
    The session chooser, like GarageBand's project chooser. Shown when the app
    starts, and for File > New Session and File > Open Session.

      New Session   templates (Empty, Keyboard Collection, Voice...) with the
                    new session's tempo, time signature and key
      Recent        recently opened sessions, each with a picture of its
                    tracks (taken when it was saved), plus "Open Other..."
                    for anything else on the computer

    Like NewTrackDialog it's drawn inside the main window over a dimmed
    background, and deletes itself when closed. It doesn't touch the current
    session itself: it reports the choice, and the owner asks about unsaved
    changes before replacing anything.
*/
class SessionChooser  : public juce::Component
{
public:
    enum class Page { newSession, recent };

    struct Callbacks
    {
        std::function<void (const NewSessionSettings&)> onNew;
        std::function<void (const juce::File&)> onOpen;
        std::function<void()> onOpenOther;   // the computer's file dialog
    };

    /** Opens the chooser over `parent` on the given page. It owns itself. */
    static void show (juce::Component& parent, AudioEngine&, Page, Callbacks);

    /** Closes the chooser if it's showing over `parent` (e.g. when a session
        is opened from the file manager instead). */
    static void dismiss (juce::Component& parent);

    ~SessionChooser() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    class Tile;
    class NavItem;

    SessionChooser (AudioEngine&, Callbacks);

    AudioEngine& engine;
    Callbacks callbacks;
    Page page = Page::newSession;

    std::unique_ptr<NavItem> newNav, recentNav;
    IconButton openOtherButton { "Open other session", Icon::folder, "Open Other..." };

    juce::Viewport viewport;
    juce::Component tileHolder;
    juce::OwnedArray<Tile> templateTiles, recentTiles;
    std::vector<RecentSession> recentSessions;
    int selectedTemplate = 0, selectedRecent = 0;

    // New session details
    juce::TextEditor tempoField;
    juce::ComboBox timeSignatureBox, keyRootBox, keyModeBox;

    IconButton cancelButton { "Cancel", std::nullopt, "Cancel" };
    IconButton chooseButton { "Choose", std::nullopt, "Choose" };

    juce::Image logo;   // the app icon, drawn beside the name at the top of the sidebar
    juce::Rectangle<int> panel, sidebar, header, footer;

    void showPage (Page);
    void select (int index);
    void layoutTiles();
    juce::OwnedArray<Tile>& currentTiles();
    int& currentSelection();
    int getColumns() const;
    NewSessionSettings getSettings() const;

    void choose();
    void close();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SessionChooser)
};

} // namespace ikaros
