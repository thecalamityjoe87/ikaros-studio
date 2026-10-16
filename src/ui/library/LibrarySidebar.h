#pragma once

#include "ui/widgets/IconButton.h"
#include "ui/library/LoopBrowser.h"
#include "ui/state/Selection.h"

namespace ikaros
{

//==============================================================================
/**
    The sound library on the left: a search field, a grid of category tiles,
    and the list for the chosen category.

      Sound categories   click a sound to use it on the selected track (on an
                         audio track, or with nothing selected, it makes a new
                         instrument track)
      Audio Files        click to preview, drag onto the timeline to make a
                         region. It browses folders on the computer.
      Loops              opens the loop browser, a page over the whole
                         sidebar (see LoopBrowser)
      Plug-ins           third-party instruments (click to use) and effects
                         (click to add to the selected track), with the scan's
                         progress and a button to scan again

    Typing in the search field searches every category (in Audio Files, the
    folder being shown).
*/
class LibrarySidebar  : public juce::Component
{
public:
    LibrarySidebar (AudioEngine&, Selection&);
    ~LibrarySidebar() override;

    /** The model or selection changed: highlights the selected track's sound,
        and picks up new plug-ins. */
    void refresh();

    /** ~30 Hz: scan progress and which file is previewing. */
    void update();

    /** The "More Sounds..." button: the owner opens the More Sounds window,
        with the category being browsed (empty for Audio Files and Plug-ins). */
    std::function<void (const juce::String& category)> onMoreSoundsRequested;

    /** The loop browser needs an API key (or a new one): the owner opens the setup window. */
    std::function<void()> onFreesoundSetupRequested;

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;

private:
    class CategoryTile;
    class Row;

    // One line of the list.
    struct Item
    {
        enum class Kind { sound, pluginInstrument, pluginEffect, file, folder, label };

        Kind kind = Kind::label;
        juce::String id;           // sounds: SoundInfo::id; plug-ins: PluginInfo::id
        juce::String name, detail;
        juce::File file;           // files and folders
    };

    AudioEngine& engine;
    Selection& selection;

    juce::TextEditor searchField;
    juce::OwnedArray<CategoryTile> tiles;   // OwnedArray deletes its objects when it's destroyed
    juce::Viewport listViewport;
    juce::Component listHolder;
    juce::OwnedArray<Row> rows;

    // Audio Files: the folder being shown.
    juce::File audioFolder;
    IconButton upButton { "Up one folder", Icon::chevronUp };
    IconButton chooseFolderButton { "Choose folder", std::nullopt, "Choose..." };
    std::unique_ptr<juce::FileChooser> folderChooser;

    // Plug-ins: scan status and the scan menu.
    IconButton scanButton { "Scan for plug-ins", std::nullopt, "Scan..." };

    // Downloadable sound packs, at the bottom.
    IconButton moreSoundsButton { "More sounds", Icon::download, "More Sounds..." };
    juce::String scanText;
    int knownPluginCount = -1;
    int knownSoundCount = -1;   // sounds and loops, so a pack installed or removed updates the list

    juce::String selectedCategory { "Keyboards" };
    juce::String heading, emptyText;
    juce::Rectangle<int> headingArea, toolsArea;
    juce::File playingFile;

    // Drawn over everything else while it's open. Declared last, so it's on top.
    LoopBrowser loopBrowser;

    void openLoopBrowser();
    void selectCategory (const juce::String&);
    void rebuildList();
    std::vector<Item> collectItems() const;
    std::vector<Item> listFolder (const juce::String& query) const;
    void layoutList();
    bool hasTools() const;

    void activate (const Item&);
    void useSound (const juce::String& soundId);
    void showScanMenu();
    void chooseFolder();
    void updateScanText();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LibrarySidebar)
};

} // namespace ikaros
