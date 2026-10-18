#include "ui/library/LibrarySidebar.h"
#include "engine/AudioPreview.h"
#include "engine/PluginLibrary.h"
#include "engine/SoundLibrary.h"
#include "ui/state/DragTypes.h"
#include "ui/theme/Fonts.h"
#include "ui/theme/IkarosLookAndFeel.h"
#include "ui/theme/Tokens.h"

namespace ikaros
{

using namespace tokens;

namespace
{
    struct Category { const char* name; juce::Colour colour; };

    // The tiles, in grid order. The last one spans both columns.
    const Category categories[] {
        { "Keyboards",   colour::categoryKeys },
        { "Drums",       colour::categoryDrums },
        { "Bass",        colour::categoryBass },
        { "Synths",      colour::categorySynths },
        { "Guitar",      colour::categoryGuitar },
        { "Strings",     colour::categoryStrings },
        { "Woodwinds",   colour::categoryWoodwinds },
        { "Brass",       colour::categoryBrass },
        { "Loops",       colour::categoryLoops },
        { "Audio Files", colour::categoryAudio },
        { "Plug-ins",    colour::categoryPlugins },
    };

    constexpr int rowHeight = 34, labelHeight = 26, rowGap = 2;
    constexpr int tileHeight = 52, tileGap = 6;

    bool isFileCategory (const juce::String& c)   { return c == "Loops" || c == "Audio Files"; }

    juce::String describeSize (const juce::File& f)
    {
        auto bytes = f.getSize();
        return bytes > 1024 * 1024 ? juce::String ((double) bytes / (1024.0 * 1024.0), 1) + " MB"
                                   : juce::String (bytes / 1024) + " KB";
    }
}

//==============================================================================
class LibrarySidebar::CategoryTile  : public juce::Button
{
public:
    CategoryTile (const juce::String& n, juce::Colour c)
        : juce::Button (n), name (n), iconColour (c)
    {
        setTitle (n == "Plug-ins" ? n : n + " sounds");
        setMouseClickGrabsKeyboardFocus (false);
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        auto r = getLocalBounds().toFloat().reduced (0.5f);
        const bool on = getToggleState();

        g.setColour (on ? colour::selectedRow : colour::tileBg.brighter (down ? 0.08f : (over ? 0.04f : 0.0f)));
        g.fillRoundedRectangle (r, size::radiusLarge);
        g.setColour (on ? iconColour : colour::borderSoft);
        g.drawRoundedRectangle (r, size::radiusLarge, 1.0f);

        auto content = getLocalBounds().reduced (10, 8);
        icons::draw (g, icons::forCategory (name), content.removeFromTop (18).removeFromLeft (18).toFloat(), iconColour, 18.0f);
        content.removeFromTop (5);

        g.setColour (colour::text);
        g.setFont (fonts::uiSemiBold (textSize::md));
        g.drawText (name, content, juce::Justification::topLeft, true);

        if (hasKeyboardFocus (false))
            IkarosLookAndFeel::drawFocusRing (g, r, size::radiusLarge);
    }

    const juce::String name;

private:
    juce::Colour iconColour;
};

//==============================================================================
// One line of the list: a sound, plug-in, file, folder or a small heading.
class LibrarySidebar::Row  : public juce::Button
{
public:
    Row (LibrarySidebar& o, const Item& i) : juce::Button (i.name), owner (o), item (i)
    {
        setTitle (i.name);
        setMouseClickGrabsKeyboardFocus (false);

        const bool clickable = item.kind != Item::Kind::label;
        setMouseCursor (clickable ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
        setEnabled (clickable);

        switch (item.kind)
        {
            case Item::Kind::sound:
            case Item::Kind::pluginInstrument: setTooltip ("Click to use " + i.name + " on the selected track, or drag it onto a track"); break;
            case Item::Kind::pluginEffect:     setTooltip ("Add " + i.name + " to the selected track"); break;
            case Item::Kind::file:             setTooltip ("Click to listen. Drag onto the timeline to use it."); break;
            case Item::Kind::folder:           setTooltip ("Open " + i.name); break;
            case Item::Kind::label:            break;
        }

        onClick = [this]
        {
            if (! dragged)
                owner.activate (item);
        };
    }

    int getPreferredHeight() const    { return item.kind == Item::Kind::label ? labelHeight : rowHeight; }

    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        auto r = getLocalBounds().toFloat();

        if (item.kind == Item::Kind::label)
        {
            g.setColour (colour::textSecondary);
            g.setFont (fonts::uiBold (textSize::tiny).withExtraKerningFactor (0.1f));
            g.drawText (item.name.toUpperCase(), getLocalBounds().withTrimmedLeft (2).withTrimmedTop (8),
                        juce::Justification::centredLeft);
            return;
        }

        const bool active = getToggleState();
        const bool playing = item.kind == Item::Kind::file && owner.playingFile == item.file;

        if (active || playing)
            g.setColour (colour::accent.withAlpha (0.14f));
        else
            g.setColour (over ? colour::raised.withAlpha (down ? 1.0f : 0.6f) : juce::Colours::transparentBlack);

        g.fillRoundedRectangle (r, size::radius);

        auto content = getLocalBounds().reduced (10, 0);
        auto lead = content.removeFromLeft (16).toFloat();
        content.removeFromLeft (8);

        switch (item.kind)
        {
            case Item::Kind::file:
                icons::draw (g, playing ? Icon::stop : Icon::play, lead, playing ? colour::accent : colour::textSecondary, 12.0f);
                break;
            case Item::Kind::folder:
                icons::draw (g, Icon::folder, lead, colour::textSecondary, 15.0f);
                break;
            case Item::Kind::pluginEffect:
                icons::draw (g, Icon::plugin, lead, colour::textSecondary, 14.0f);
                break;
            default:
                g.setColour (active ? colour::accent : colour::borderStrong);
                g.fillEllipse (lead.withSizeKeepingCentre (6.0f, 6.0f));
                break;
        }

        auto detail = active ? juce::String ("On track") : item.detail;
        g.setFont (fonts::ui (textSize::sm));
        auto detailWidth = juce::jmin (content.getWidth() / 2,
                                       (int) std::ceil (juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), detail)) + 4);
        g.setColour (active ? colour::accentLight : colour::textSecondary);
        g.drawText (detail, content.removeFromRight (detailWidth), juce::Justification::centredRight, true);

        g.setFont (fonts::ui (textSize::lg));
        g.setColour (colour::text);
        g.drawText (item.name, content.withTrimmedRight (6), juce::Justification::centredLeft, true);

        if (hasKeyboardFocus (false))
            IkarosLookAndFeel::drawFocusRing (g, r, size::radius);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        dragged = false;
        juce::Button::mouseDown (e);
    }

    // Files and sounds can be dragged onto the timeline. DragAndDropContainer
    // (the main window) shows a picture of the row under the mouse while dragging.
    void mouseDrag (const juce::MouseEvent& e) override
    {
        juce::Button::mouseDrag (e);

        if (dragged || e.getDistanceFromDragStart() <= 6)
            return;

        juce::var description;

        if (item.kind == Item::Kind::file)
            description = dragtypes::describeLibraryFile (item.file);
        else if (item.kind == Item::Kind::sound)
            description = dragtypes::describeLibrarySound (item.id);
        else if (item.kind == Item::Kind::pluginInstrument)
            description = dragtypes::describeLibrarySound ("plugin:" + item.id);
        else
            return;

        if (auto* container = juce::DragAndDropContainer::findParentDragContainerFor (this))
        {
            dragged = true;
            container->startDragging (description, this);
        }
    }

    const Item item;

private:
    LibrarySidebar& owner;
    bool dragged = false;
};

//==============================================================================
LibrarySidebar::LibrarySidebar (AudioEngine& e, Selection& s)
    : engine (e), selection (s), loopBrowser (e)
{
    setTitle ("Sound library");
    setFocusContainerType (juce::Component::FocusContainerType::focusContainer);

    searchField.setTextToShowWhenEmpty ("Search sounds and loops", colour::textSecondary);
    searchField.setFont (fonts::ui (textSize::lg));
    searchField.setTitle ("Search sounds");
    searchField.setIndents (36, 9);   // room for the magnifier (see paintOverChildren)
    searchField.onTextChange = [this] { rebuildList(); };
    searchField.onEscapeKey = [this] { searchField.clear(); rebuildList(); };
    addAndMakeVisible (searchField);

    for (auto& c : categories)
    {
        auto* tile = tiles.add (new CategoryTile (c.name, c.colour));
        tile->onClick = [this, n = juce::String (c.name)]
        {
            if (n == "Loops")
                openLoopBrowser();
            else
                selectCategory (n);
        };
        addAndMakeVisible (tile);
    }

    listViewport.setViewedComponent (&listHolder, false);   // false = we own listHolder, not the viewport
    listViewport.setScrollBarsShown (true, false);
    addAndMakeVisible (listViewport);

    // Tools above the list (only some categories show them).
    for (auto* b : { &upButton, &chooseFolderButton, &scanButton })
    {
        auto colours = IconButton::raisedColours();
        colours.border = colour::border;
        b->setColours (colours);
        b->setCornerRadius (6.0f);
        b->setFont (fonts::ui (textSize::sm));
        b->setIconSize (16.0f);
        addChildComponent (b);
    }

    upButton.setTooltip ("Up one folder");
    upButton.onClick = [this]
    {
        if (audioFolder.getParentDirectory() != audioFolder)
        {
            audioFolder = audioFolder.getParentDirectory();
            rebuildList();
        }
    };

    chooseFolderButton.setTooltip ("Choose a folder of audio files");
    chooseFolderButton.onClick = [this] { chooseFolder(); };

    scanButton.setTooltip ("Look for new plug-ins");
    scanButton.onClick = [this] { showScanMenu(); };

    auto moreColours = IconButton::raisedColours();
    moreColours.border = colour::border;
    moreSoundsButton.setColours (moreColours);
    moreSoundsButton.setFont (fonts::uiSemiBold (textSize::md));
    moreSoundsButton.setIconSize (16.0f);
    moreSoundsButton.setTooltip ("Download more instruments and loops");
    moreSoundsButton.onClick = [this]
    {
        const bool noPacks = selectedCategory == "Audio Files" || selectedCategory == "Plug-ins";

        if (onMoreSoundsRequested)
            onMoreSoundsRequested (noPacks ? juce::String() : selectedCategory);
    };
    addAndMakeVisible (moreSoundsButton);

    auto music = juce::File::getSpecialLocation (juce::File::userMusicDirectory);
    audioFolder = music.isDirectory() ? music : juce::File::getSpecialLocation (juce::File::userHomeDirectory);

    loopBrowser.onFreesoundSetupRequested = [this] { if (onFreesoundSetupRequested) onFreesoundSetupRequested(); };
    addChildComponent (loopBrowser);

    selectCategory (selectedCategory);
}

LibrarySidebar::~LibrarySidebar()
{
    engine.getPreview().stop();
}

//==============================================================================
void LibrarySidebar::selectCategory (const juce::String& name)
{
    selectedCategory = name;

    for (auto* t : tiles)
        t->setToggleState (t->name == name, juce::dontSendNotification);

    engine.getPreview().stop();
    searchField.clear();
    rebuildList();
    resized();
}

void LibrarySidebar::openLoopBrowser()
{
    engine.getPreview().stop();
    loopBrowser.open();
    repaint();
}

bool LibrarySidebar::hasTools() const
{
    return searchField.isEmpty() ? (selectedCategory == "Audio Files" || selectedCategory == "Plug-ins")
                                 : selectedCategory == "Audio Files";
}

std::vector<LibrarySidebar::Item> LibrarySidebar::listFolder (const juce::String& query) const
{
    std::vector<Item> folders, files;

    for (auto& f : audioFolder.findChildFiles (juce::File::findFilesAndDirectories | juce::File::ignoreHiddenFiles, false))
    {
        if (query.isNotEmpty() && ! f.getFileName().containsIgnoreCase (query))
            continue;

        if (f.isDirectory())
            folders.push_back ({ Item::Kind::folder, {}, f.getFileName(), {}, f });
        else if (engine.getTracks().regions().isSupportedAudioFile (f))
            files.push_back ({ Item::Kind::file, {}, f.getFileNameWithoutExtension(), describeSize (f), f });
    }

    auto byName = [] (const Item& a, const Item& b) { return a.name.compareNatural (b.name) < 0; };
    std::sort (folders.begin(), folders.end(), byName);
    std::sort (files.begin(), files.end(), byName);
    folders.insert (folders.end(), files.begin(), files.end());
    return folders;
}

std::vector<LibrarySidebar::Item> LibrarySidebar::collectItems() const
{
    std::vector<Item> items;
    const auto query = searchField.getText().trim();
    auto matches = [&query] (const juce::String& name) { return query.isEmpty() || name.containsIgnoreCase (query); };
    auto& plugins = engine.getPlugins();

    // Audio Files searches only the folder it shows.
    if (selectedCategory == "Audio Files")
        return listFolder (query);

    auto addSounds = [&] (const juce::String& category, bool withLabel)
    {
        bool labelled = ! withLabel;

        for (auto& s : engine.getSounds().getSounds())
        {
            if (s.category == category && matches (s.name))
            {
                if (! labelled)
                    items.push_back ({ Item::Kind::label, {}, category });

                labelled = true;
                items.push_back ({ Item::Kind::sound, s.id, s.name, s.detail });
            }
        }
    };

    auto addLoops = [&] (bool withLabel)
    {
        bool labelled = ! withLabel;

        for (auto& l : engine.getSounds().getLoops())
        {
            if (matches (l.name))
            {
                if (! labelled)
                    items.push_back ({ Item::Kind::label, {}, "Loops" });

                labelled = true;
                items.push_back ({ Item::Kind::file, {}, l.name, l.detail, l.file });
            }
        }
    };

    auto addPlugins = [&] (bool instruments)
    {
        bool labelled = false;

        for (auto& p : instruments ? plugins.getInstruments() : plugins.getEffects())
        {
            if (matches (p.name) || matches (p.manufacturer))
            {
                if (! labelled)
                    items.push_back ({ Item::Kind::label, {}, instruments ? "Plug-in instruments" : "Plug-in effects" });

                labelled = true;
                items.push_back ({ instruments ? Item::Kind::pluginInstrument : Item::Kind::pluginEffect,
                                   p.id, p.name, p.manufacturer.isNotEmpty() ? p.manufacturer : p.format });
            }
        }
    };

    if (query.isNotEmpty())
    {
        // Search everything, grouped by category.
        for (auto& c : categories)
            if (! isFileCategory (c.name) && juce::String (c.name) != "Plug-ins")
                addSounds (c.name, true);

        addLoops (true);
        addPlugins (true);
        addPlugins (false);
    }
    else if (selectedCategory == "Plug-ins")
    {
        addPlugins (true);
        addPlugins (false);
    }
    else
    {
        addSounds (selectedCategory, false);
    }

    return items;
}

//==============================================================================
void LibrarySidebar::rebuildList()
{
    rows.clear();
    const auto query = searchField.getText().trim();

    heading = query.isNotEmpty() && selectedCategory != "Audio Files" ? juce::String ("RESULTS")
            : selectedCategory == "Audio Files" ? audioFolder.getFileName().toUpperCase()
                                                : selectedCategory.toUpperCase();

    for (auto& item : collectItems())
        listHolder.addAndMakeVisible (rows.add (new Row (*this, item)));

    if (! rows.isEmpty())
        emptyText = {};
    else if (query.isNotEmpty())
        emptyText = "No matches";
    else if (selectedCategory == "Audio Files")
        emptyText = "No audio files in this folder.";
    else if (selectedCategory == "Plug-ins")
        emptyText = "No plug-ins found yet. Install VST3 or LV2 plug-ins, then scan again.";
    else
        emptyText = "No sounds here yet.";

    knownPluginCount = (int) (engine.getPlugins().getInstruments().size() + engine.getPlugins().getEffects().size());
    knownSoundCount = (int) (engine.getSounds().getSounds().size() + engine.getSounds().getLoops().size());
    updateScanText();
    refresh();
    layoutList();
    resized();
    repaint();
}

void LibrarySidebar::refresh()
{
    loopBrowser.refresh();

    // New plug-ins arrived (a scan found some): show them.
    const auto pluginCount = (int) (engine.getPlugins().getInstruments().size() + engine.getPlugins().getEffects().size());

    if (pluginCount != knownPluginCount && (selectedCategory == "Plug-ins" || searchField.getText().isNotEmpty()))
    {
        rebuildList();
        return;
    }

    // A pack was installed or removed: show its sounds.
    const auto soundCount = (int) (engine.getSounds().getSounds().size() + engine.getSounds().getLoops().size());

    if (soundCount != knownSoundCount && selectedCategory != "Audio Files" && selectedCategory != "Plug-ins")
    {
        rebuildList();
        return;
    }

    auto track = engine.getTracks().getTrack (selection.getTrack());

    for (auto* row : rows)
    {
        const bool isSound = row->item.kind == Item::Kind::sound || row->item.kind == Item::Kind::pluginInstrument;
        const auto id = row->item.kind == Item::Kind::pluginInstrument ? "plugin:" + row->item.id : row->item.id;
        row->setToggleState (isSound && track.has_value() && track->soundId == id, juce::dontSendNotification);
    }
}

void LibrarySidebar::update()
{
    if (! isShowing())
        return;

    loopBrowser.update();

    auto playing = engine.getPreview().getPlayingFile();

    if (playing != playingFile)
    {
        playingFile = playing;

        for (auto* row : rows)
            if (row->item.kind == Item::Kind::file)
                row->repaint();
    }

    if (selectedCategory == "Plug-ins")
    {
        auto old = scanText;
        updateScanText();

        if (scanText != old)
            repaint (toolsArea);
    }
}

void LibrarySidebar::updateScanText()
{
    auto& plugins = engine.getPlugins();
    auto status = plugins.getScanStatus();

    if (status.scanning)
    {
        scanText = status.total > 0 ? "Checking " + juce::String (status.done + 1) + " of " + juce::String (status.total) + "..."
                                    : juce::String ("Looking for plug-ins...");
    }
    else
    {
        const auto skipped = plugins.getSkippedPlugins().size();
        scanText = juce::String (knownPluginCount) + (knownPluginCount == 1 ? " plug-in" : " plug-ins");

        if (skipped > 0)
            scanText << ", " << skipped << " skipped";
    }

    scanButton.setEnabled (! status.scanning);
}

void LibrarySidebar::layoutList()
{
    auto w = listViewport.getMaximumVisibleWidth();
    int y = 0;

    for (auto* row : rows)
    {
        row->setBounds (0, y, w, row->getPreferredHeight());
        y += row->getPreferredHeight() + rowGap;
    }

    listHolder.setSize (w, juce::jmax (1, y));
}

//==============================================================================
void LibrarySidebar::activate (const Item& item)
{
    auto& tracks = engine.getTracks();

    switch (item.kind)
    {
        case Item::Kind::sound:             useSound (item.id); break;
        case Item::Kind::pluginInstrument:  useSound ("plugin:" + item.id); break;

        case Item::Kind::pluginEffect:
        {
            auto track = tracks.getTrack (selection.getTrack());

            if (! track.has_value())
                return;

            if (auto added = tracks.effects().addEffect (track->id, "plugin:" + item.id))
                tracks.effects().showPluginWindow (track->id, added);
            break;
        }

        case Item::Kind::file:
        {
            auto& preview = engine.getPreview();

            if (preview.getPlayingFile() == item.file)
                preview.stop();
            else
                preview.play (item.file);

            update();
            break;
        }

        case Item::Kind::folder:
            audioFolder = item.file;
            searchField.clear();
            rebuildList();
            break;

        case Item::Kind::label:
            break;
    }
}

void LibrarySidebar::useSound (const juce::String& soundId)
{
    auto& tracks = engine.getTracks();
    auto track = tracks.getTrack (selection.getTrack());

    if (track.has_value() && track->kind == TrackKind::instrument)
    {
        tracks.effects().setSound (track->id, soundId);
        return;
    }

    // No instrument track selected: make one with this sound, in one undo step.
    if (auto id = tracks.addInstrumentTrack())
    {
        tracks.effects().setSound (id, soundId, false);
        selection.selectTrack (id);
    }
}

void LibrarySidebar::showScanMenu()
{
    auto& plugins = engine.getPlugins();
    juce::PopupMenu menu;
    menu.addItem ("Scan for New Plug-ins", [&plugins] { plugins.scan(); });
    menu.addItem ("Scan All Plug-ins Again", [&plugins] { plugins.scan (true); });

    auto skipped = plugins.getSkippedPlugins();

    if (! skipped.isEmpty())
    {
        menu.addSeparator();
        menu.addSectionHeader ("Skipped because they crashed or froze:");

        for (auto& name : skipped)
            menu.addItem (name, false, false, nullptr);

        menu.addItem ("Try Skipped Plug-ins Again", [&plugins] { plugins.retrySkippedPlugins(); });
    }

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&scanButton));
}

void LibrarySidebar::chooseFolder()
{
    folderChooser = std::make_unique<juce::FileChooser> ("Choose a Folder of Audio Files", audioFolder);
    folderChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                                [safeThis = SafePointer<LibrarySidebar> (this)] (const juce::FileChooser& fc)
    {
        if (safeThis != nullptr && fc.getResult().isDirectory())
        {
            safeThis->audioFolder = fc.getResult();
            safeThis->searchField.clear();
            safeThis->rebuildList();
        }
    });
}

//==============================================================================
void LibrarySidebar::paintOverChildren (juce::Graphics& g)
{
    if (loopBrowser.isVisible())
        return;

    // Magnifier inside the search field (its left indent leaves room). Drawn
    // over the children: the TextEditor fills its own background, so
    // anything paint() drew there would be hidden.
    icons::draw (g, Icon::search, searchField.getBounds().removeFromLeft (34).toFloat().translated (2.0f, 0.0f),
                 colour::textSecondary, 15.0f, 1.8f);
}

void LibrarySidebar::paint (juce::Graphics& g)
{
    g.fillAll (colour::sidebarBg);
    g.setColour (colour::divider);
    g.fillRect (getLocalBounds().removeFromRight (1));

    g.setColour (colour::textSecondary);
    g.setFont (fonts::uiBold (textSize::xs).withExtraKerningFactor (0.1f));
    g.drawText (heading, headingArea, juce::Justification::centredLeft, true);

    if (selectedCategory == "Plug-ins" && searchField.isEmpty())
    {
        g.setFont (fonts::ui (textSize::sm));
        g.drawText (scanText, toolsArea.withRight (scanButton.getX() - 6), juce::Justification::centredLeft, true);
    }

    if (rows.isEmpty())
    {
        g.setFont (fonts::ui (textSize::md));
        g.drawFittedText (emptyText, listViewport.getBounds().removeFromTop (50).withTrimmedLeft (2),
                          juce::Justification::topLeft, 3, 1.0f);
    }
}

void LibrarySidebar::resized()
{
    loopBrowser.setBounds (getLocalBounds());

    auto r = getLocalBounds().withTrimmedRight (1).reduced (12);

    searchField.setBounds (r.removeFromTop (36));
    r.removeFromTop (12);

    moreSoundsButton.setBounds (r.removeFromBottom (34));
    r.removeFromBottom (10);

    const int tileW = (r.getWidth() - tileGap) / 2;

    for (int i = 0; i < tiles.size(); ++i)
    {
        const int row = i / 2, col = i % 2;
        const bool last = i == tiles.size() - 1 && tiles.size() % 2 == 1;   // an odd last tile spans the row
        tiles[i]->setBounds (r.getX() + col * (tileW + tileGap), r.getY() + row * (tileHeight + tileGap),
                             last ? r.getWidth() : tileW, tileHeight);
    }

    r.removeFromTop (((tiles.size() + 1) / 2) * (tileHeight + tileGap) + 6);
    headingArea = r.removeFromTop (20).withTrimmedLeft (2);

    // Tools: folder navigation for Audio Files, scanning for Plug-ins.
    const bool folderTools = hasTools() && selectedCategory == "Audio Files";
    const bool scanTools = hasTools() && selectedCategory == "Plug-ins";
    upButton.setVisible (folderTools);
    chooseFolderButton.setVisible (folderTools);
    scanButton.setVisible (scanTools);

    if (hasTools())
    {
        r.removeFromTop (4);
        toolsArea = r.removeFromTop (28).withTrimmedLeft (2);
        auto tools = toolsArea;

        if (folderTools)
        {
            upButton.setBounds (tools.removeFromLeft (28));
            tools.removeFromLeft (6);
            chooseFolderButton.setBounds (tools.removeFromLeft (84));
        }
        else
        {
            scanButton.setBounds (tools.removeFromRight (72));
        }
    }
    else
    {
        toolsArea = {};
    }

    r.removeFromTop (4);
    listViewport.setBounds (r);
    layoutList();
}

} // namespace ikaros
