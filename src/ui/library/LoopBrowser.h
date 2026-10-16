#pragma once

#include "engine/AudioEngine.h"
#include "engine/FreesoundStore.h"
#include "ui/widgets/IconButton.h"

namespace ikaros
{

//==============================================================================
/**
    The loop browser: a page that covers the library sidebar, like GarageBand's
    Loop Browser, opened by the Loops tile.

      My Loops    every loop in the library (the app's, downloaded packs', and
                  the ones added from Freesound), searched as you type
      Freesound   free loops from freesound.org (CC0 and CC BY only), searched
                  a moment after typing stops. Until the user has an API key
                  it offers to set one up (see FreesoundSetupDialog).

    Both can be filtered like GarageBand's: tabs for Instrument, Genre and
    Mood open a grid of buttons (one choice per tab, and the choices add up),
    and Tempo narrows by speed. Loops are matched on their tags and name; on
    Freesound the choices become tag searches. Clicking the open tab again
    folds the buttons away. The X clears every filter, the search included.

    The API key is changed from Sounds > Freesound API Key.

    Click a loop to listen, drag a library loop
    onto the timeline. A Freesound result has a + to add it to the library.
    A loop that came from Freesound has a bin to remove it again.

    Back returns to the sidebar. Deletes nothing it didn't download.
*/
class LoopBrowser  : public juce::Component,
                     private juce::ChangeListener,
                     private juce::Timer
{
public:
    explicit LoopBrowser (AudioEngine&);
    ~LoopBrowser() override;

    /** The user needs a Freesound API key (or wants another): the owner opens the setup window. */
    std::function<void()> onFreesoundSetupRequested;

    void open();
    void close();

    /** The model changed: picks up loops added or removed. */
    void refresh();

    /** ~30 Hz: which loop is playing. */
    void update();

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;

private:
    class Row;

    enum class Scope { mine, freesound };

    struct Item
    {
        enum class Kind { loop, result, more };

        Kind kind = Kind::loop;
        juce::String id;           // a Freesound number: results, and library loops that came from Freesound
        juce::String name, detail;
        juce::File file;           // what plays: the library file, or a result's preview
        bool added = false;        // a result that's in the library
        bool removable = false;    // came from Freesound, so it can be deleted
    };

    AudioEngine& engine;
    Scope scope = Scope::mine;
    int tempoIndex = 0;
    int filterTab = -1;                // the tab whose buttons show: 0 Instrument, 1 Genre, 2 Mood, -1 none
    int chosen[3] { -1, -1, -1 };      // the button chosen in each tab, or -1
    juce::String lastFreesoundSearch;  // what the Freesound list was searched with
    juce::String mineQuery, freesoundQuery;
    int knownLoopCount = -1;
    juce::File playingFile;

    IconButton backButton { "Back to the library", Icon::chevronLeft, "Library" };
    IconButton mineTab { "My loops", std::nullopt, "My Loops" };
    IconButton freesoundTab { "Search Freesound", std::nullopt, "Freesound" };
    juce::TextEditor searchField;
    juce::OwnedArray<IconButton> filterTabs, chips;
    IconButton clearButton { "Clear all filters", Icon::windowClose };
    IconButton tempoButton { "Filter by tempo", std::nullopt, "Tempo" };
    IconButton setupButton { "Set up Freesound", std::nullopt, "Set Up Freesound..." };

    juce::Viewport viewport;
    juce::Component holder;
    juce::OwnedArray<Row> rows;
    juce::String heading, emptyText;
    juce::Rectangle<int> headingArea, footerArea, emptyArea;

    void setScope (Scope);
    void buildChips();
    void filterChanged();
    void clearFilters();
    void updateClearButton();
    FreesoundFilters currentFilters() const;
    juce::String searchSignature() const;
    bool freesoundNeedsSearch() const;
    bool fitsFilters (const LibraryFile&) const;
    juce::StringArray chosenLabels() const;
    void rebuild();
    void layoutList();
    std::vector<Item> collectItems() const;
    void searchFreesound();
    void showTempoMenu();
    void activate (const Item&);
    void runAction (const Item&);
    void confirmRemove (const Item&);
    bool usingFreesoundWithoutKey() const;

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LoopBrowser)
};

} // namespace ikaros
