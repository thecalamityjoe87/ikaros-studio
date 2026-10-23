#pragma once

#include "ui/widgets/IconButton.h"
#include "ui/library/LoopBrowser.h"
#include "ui/state/Selection.h"

namespace ikaros
{

//==============================================================================
/**
    The sound library on the left: a search field, a 4-column grid of
    category tiles, a breadcrumb ("All sounds › Keyboards") with a count, and
    the list for the chosen category.

      Sound categories   click a sound to use it on the selected track (on an
                         audio track, or with nothing selected, it makes a new
                         instrument track). Hovering shows a round button that
                         previews it. Packs from the pack index that aren't
                         installed are listed too, with a download button.
      All sounds         (the breadcrumb's root) every sound, grouped by category
      Audio              click to preview, drag onto the timeline to make a
                         region. It browses folders on the computer.
      Loops              opens the loop browser, a page over the whole
                         sidebar (see LoopBrowser)
      Plug-ins           (also Mix > Plug-ins..., see showPlugins)
                         third-party instruments (click to use), with the
                         scan's progress and a button to scan again. Plug-in
                         effects aren't listed: they're in the track's
                         effects menu

    Typing in the search field searches every category (in Audio, the
    folder being shown).
*/
class LibrarySidebar  : public juce::Component,
                        private juce::ChangeListener   // the pack store: index loaded, download progress
{
public:
    LibrarySidebar (AudioEngine&, Selection&);
    ~LibrarySidebar() override;

    /** The model or selection changed: highlights the selected track's sound,
        and picks up new plug-ins. */
    void refresh();

    /** ~30 Hz: scan progress and which file is previewing. */
    void update();

    /** Shows the plug-in list (Mix > Plug-ins...): third-party instruments
        and the plug-in scan. */
    void showPlugins();

    /** The "Manage Sounds" button: the owner opens the Manage Sounds window
        (installed packs, updates, removing, and everything to download). */
    std::function<void()> onManageSoundsRequested;

    /** The loop browser needs an API key (or a new one): the owner opens the setup window. */
    std::function<void()> onFreesoundSetupRequested;

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;

private:
    class CategoryTile;
    class CrumbButton;
    class Row;

    // One line of the list.
    struct Item
    {
        enum class Kind { sound, pack, pluginInstrument, file, folder, label };

        Kind kind = Kind::label;
        juce::String id;           // sounds: SoundInfo::id; packs: StorePack::id; plug-ins: PluginInfo::id
        juce::String name, detail; // packs: detail is the download size
        juce::File file;           // files and folders
    };

    AudioEngine& engine;
    Selection& selection;

    juce::TextEditor searchField;
    juce::OwnedArray<CategoryTile> tiles;   // OwnedArray deletes its objects when it's destroyed
    std::unique_ptr<CrumbButton> allSoundsButton;   // the breadcrumb's "All sounds", when there's somewhere to go back from
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

    // Sound packs (download, update, remove), at the bottom.
    IconButton manageSoundsButton { "Manage sounds", Icon::package, "Manage Sounds" };
    juce::String scanText;
    int knownPluginCount = -1;
    int knownSoundCount = -1;   // sounds and loops, so a pack installed or removed updates the list

    // A category name, "Audio Files", "Plug-ins", or empty for All sounds.
    juce::String selectedCategory { "Keyboards" };
    juce::String crumbText, countText, emptyText;   // the breadcrumb's last part, "11 sounds"
    juce::Rectangle<int> crumbArea, toolsArea;
    juce::File playingFile;
    juce::String previewingSoundId;   // the sound whose preview is playing (see previewSound)
    juce::File previewingSoundFile;   // the sample it's playing

    // The not-installed packs the list shows, and their download states, so
    // a store change that alters them rebuilds the list (others just repaint).
    juce::String packSignature;

    // Drawn over everything else while it's open. Declared last, so it's on top.
    LoopBrowser loopBrowser;

    void openLoopBrowser();
    void selectCategory (const juce::String&);
    void showAllSounds();
    void rebuildList();
    std::vector<Item> collectItems() const;
    std::vector<Item> listFolder (const juce::String& query) const;
    void layoutList();
    bool hasTools() const;

    void activate (const Item&);
    void useSound (const juce::String& soundId);
    void previewSound (const juce::String& soundId);
    bool canPreview (const juce::String& soundId) const;
    void updatePreviewButtons();
    void installPack (const juce::String& packId);
    std::vector<StorePack> getPacksToOffer() const;
    juce::String describePacksToOffer() const;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void showScanMenu();
    void chooseFolder();
    void updateScanText();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LibrarySidebar)
};

} // namespace ikaros
