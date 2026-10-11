#pragma once

#include "ui/widgets/IconButton.h"
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
      Loops, Audio Files click to preview, drag onto the timeline to make a
                         region. Audio Files browses folders on the computer.
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
    juce::String scanText;
    int knownPluginCount = -1;

    juce::String selectedCategory { "Keyboards" };
    juce::String heading, emptyText;
    juce::Rectangle<int> headingArea, toolsArea;
    juce::File playingFile;

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
