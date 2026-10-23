#include "ui/library/LibrarySidebar.h"
#include "engine/AudioPreview.h"
#include "engine/PackStore.h"
#include "engine/PluginLibrary.h"
#include "engine/SoundLibrary.h"
#include "ui/library/PreviewButton.h"
#include "ui/state/DragTypes.h"
#include "ui/theme/Fonts.h"
#include "ui/theme/IkarosLookAndFeel.h"
#include "ui/theme/Tokens.h"
#include "ui/theme/TrackColours.h"

namespace ikaros
{

using namespace tokens;

namespace
{
    // The tiles, in grid order: the category each one opens, and its short
    // label (the tiles are narrow). Each is drawn in its family colour, the
    // same as its tracks'.
    struct Category { const char* name; const char* label; };

    const Category categories[] {
        { "Keyboards",   "Keys" },
        { "Drums",       "Drums" },
        { "Bass",        "Bass" },
        { "Synths",      "Synths" },
        { "Guitar",      "Guitar" },
        { "Strings",     "Strings" },
        { "Woodwinds",   "Woodwinds" },
        { "Brass",       "Brass" },
        { "Loops",       "Loops" },
        { "Audio Files", "Audio" },
        { "Plug-ins",    "Plug-ins" },
    };

    constexpr int tileColumns = 4, tileGap = 6;
    constexpr int labelHeight = 26, rowGap = 2;
    constexpr int crumbHeight = 24;

    bool isFileCategory (const juce::String& c)   { return c == "Loops" || c == "Audio Files"; }

    juce::String describeBytes (juce::int64 bytes)
    {
        constexpr double mb = 1024.0 * 1024.0, gb = mb * 1024.0;

        if (bytes >= (juce::int64) gb)  return juce::String ((double) bytes / gb, 1) + " GB";
        if (bytes >= (juce::int64) mb)  return juce::String ((double) bytes / mb, 1) + " MB";
        return juce::String (bytes / 1024) + " KB";
    }

    // "1 sound", "11 sounds"
    juce::String countOf (int n, const juce::String& thing)
    {
        return juce::String (n) + " " + thing + (n == 1 ? "" : "s");
    }

    int textWidth (const juce::Font& font, const juce::String& text)
    {
        return (int) std::ceil (juce::GlyphArrangement::getStringWidth (font, text));
    }
}

//==============================================================================
// A category tile: its icon in the family colour, over a short label.
class LibrarySidebar::CategoryTile  : public juce::Button
{
public:
    CategoryTile (const juce::String& n, const juce::String& l, Family f)
        : juce::Button (n), name (n), label (l), family (f)
    {
        setTitle (n);
        setTooltip (n == "Loops" ? juce::String ("Browse loops")
                                 : n == "Audio Files" ? juce::String ("Audio files on this computer")
                                 : n == "Plug-ins" ? juce::String ("Plug-in instruments and effects") : n);
        setMouseClickGrabsKeyboardFocus (false);
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        auto r = getLocalBounds().toFloat().reduced (0.5f);
        const bool on = getToggleState();

        // The chosen category is neutral: selected fill, grey border.
        g.setColour (on ? theme().selectedBg : theme().tileBg.brighter (down ? 0.08f : (over ? 0.04f : 0.0f)));
        g.fillRoundedRectangle (r, size::radius);
        g.setColour (on ? theme().selectedBorder : theme().tileBorder);
        g.drawRoundedRectangle (r, size::radius, 1.0f);

        auto content = getLocalBounds().reduced (2, 7);
        auto labelArea = content.removeFromBottom (14);
        icons::draw (g, icons::forCategory (name), content.toFloat(), theme().ink (family), 18.0f);

        // "Woodwinds" only just fits a tile: squash it a little rather than
        // shrink it below the 11px minimum.
        g.setColour (on ? theme().text : theme().textSecondary);
        g.setFont (fonts::ui (textSize::xs));
        g.drawFittedText (label, labelArea, juce::Justification::centred, 1, 0.85f);

        if (hasKeyboardFocus (false))
            IkarosLookAndFeel::drawFocusRing (g, r, size::radius);
    }

    const juce::String name;

private:
    const juce::String label;
    const Family family;   // the icon's colour, looked up when painting
};

//==============================================================================
// The breadcrumb's "All sounds": plain muted text that brightens on hover.
class LibrarySidebar::CrumbButton  : public juce::Button
{
public:
    CrumbButton() : juce::Button ("All sounds")
    {
        setTooltip ("Back to all sounds");
        setMouseClickGrabsKeyboardFocus (false);
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    static juce::Font getFont()     { return fonts::ui (textSize::md); }
    int getIdealWidth() const       { return textWidth (getFont(), getButtonText()) + 2; }

    void paintButton (juce::Graphics& g, bool over, bool) override
    {
        g.setColour (over ? theme().text : theme().textSecondary);
        g.setFont (getFont());
        g.drawText (getButtonText(), getLocalBounds(), juce::Justification::centredLeft, false);

        if (hasKeyboardFocus (false))
            IkarosLookAndFeel::drawFocusRing (g, getLocalBounds().toFloat(), size::radiusXs);
    }
};

//==============================================================================
// One line of the list: a sound, a pack to download, a plug-in instrument, a file, a
// folder or a small heading.
class LibrarySidebar::Row  : public juce::Button
{
public:
    Row (LibrarySidebar& o, const Item& i) : juce::Button (i.name), item (i), owner (o)
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
            case Item::Kind::pack:             setTooltip ("Download " + i.name + " (" + i.detail + ")"); break;
            case Item::Kind::file:             setTooltip ("Click to listen. Drag onto the timeline to use it."); break;
            case Item::Kind::folder:           setTooltip ("Open " + i.name); break;
            case Item::Kind::label:            break;
        }

        if (item.kind == Item::Kind::sound || item.kind == Item::Kind::file)
        {
            // The round preview button, the same as the loop browser's (see
            // PreviewButton). A sound previews its instrument; a file plays
            // (or stops), the same as clicking its row.
            previewButton = std::make_unique<PreviewButton> (i.name);
            previewButton->onClick = [this]
            {
                if (item.kind == Item::Kind::sound)
                    owner.previewSound (item.id);
                else
                    owner.activate (item);
            };

            // Leaving the preview button doesn't send the row a mouseExit,
            // so the button tells the row whenever its own hover state changes.
            previewButton->onStateChange = [this] { updatePreviewButton(); };
            addChildComponent (*previewButton);
            updatePreviewButton();
        }

        onClick = [this]
        {
            if (! dragged)
                owner.activate (item);
        };
    }

    int getPreferredHeight() const    { return item.kind == Item::Kind::label ? labelHeight : size::libraryRowHeight; }

    /** Shows the preview button while the row (or the button itself) is
        hovered, or while this row's item plays, with a stop icon then. */
    void updatePreviewButton()
    {
        if (previewButton == nullptr)
            return;

        if (item.kind == Item::Kind::file)
            previewButton->setState (owner.playingFile == item.file);
        else
            previewButton->setState (owner.previewingSoundId == item.id, owner.canPreview (item.id));

        previewButton->updateVisibility (*this);
        repaint();
    }

    void mouseEnter (const juce::MouseEvent& e) override    { juce::Button::mouseEnter (e); updatePreviewButton(); }
    void mouseExit (const juce::MouseEvent& e) override     { juce::Button::mouseExit (e); updatePreviewButton(); }

    void resized() override
    {
        if (previewButton != nullptr)
            previewButton->setBounds (getRoundButtonArea());
    }

    void paintButton (juce::Graphics& g, bool, bool down) override
    {
        auto r = getLocalBounds().toFloat();

        if (item.kind == Item::Kind::label)
        {
            g.setColour (theme().textSecondary);
            g.setFont (fonts::uiBold (textSize::xs).withExtraKerningFactor (0.1f));
            g.drawText (item.name.toUpperCase(), getLocalBounds().withTrimmedLeft (size::libraryRowInset).withTrimmedTop (8),
                        juce::Justification::centredLeft);
            return;
        }

        // The selected fill means "on the selected track". Playing is shown
        // by the preview button's stop icon, the same for sounds and files.
        const bool active = getToggleState();
        const bool over = isMouseOver (true);   // true over the preview button too

        if (active)
            g.setColour (theme().selectedBg);
        else
            g.setColour (over ? (down ? theme().rowPressed : theme().rowHover) : juce::Colours::transparentBlack);

        g.fillRoundedRectangle (r, size::radius);

        auto content = getLocalBounds().reduced (size::libraryRowInset, 0);

        // Folders have a small icon in front.
        switch (item.kind)
        {
            case Item::Kind::folder:
                icons::draw (g, Icon::folder, content.removeFromLeft (16).toFloat(), theme().textSecondary, 15.0f);
                content.removeFromLeft (8);
                break;
            case Item::Kind::sound:
            case Item::Kind::pack:
            case Item::Kind::pluginInstrument:
            case Item::Kind::file:
            case Item::Kind::label:
                break;
        }

        // On the right: the preview or download button (when there is one),
        // then the type tag, the "On track" chip or the pack's size.
        if ((previewButton != nullptr && previewButton->isVisible()) || item.kind == Item::Kind::pack)
            content.setRight (getRoundButtonArea().getX() - 8);

        if (item.kind == Item::Kind::pack)
            paintPackStatus (g, content);
        else if (active)
            paintOnTrackChip (g, content);
        else
            paintTag (g, content, item.detail);

        g.setFont (fonts::ui (textSize::lg));
        g.setColour (item.kind == Item::Kind::pack ? theme().textSecondary : theme().text);   // not installed: a little muted
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
    std::unique_ptr<PreviewButton> previewButton;   // sounds and files
    bool dragged = false;

    juce::Rectangle<int> getRoundButtonArea() const
    {
        return PreviewButton::getBoundsIn (getLocalBounds());   // a pack's download button goes in the same place
    }

    // Right-aligned small text: "Sampled", "Synth", a plug-in's maker, a file's size.
    void paintTag (juce::Graphics& g, juce::Rectangle<int>& content, const juce::String& text) const
    {
        g.setFont (fonts::ui (textSize::sm));
        const auto w = juce::jmin (content.getWidth() / 2, textWidth (g.getCurrentFont(), text) + 4);
        g.setColour (theme().textSecondary);
        g.drawText (text, content.removeFromRight (w), juce::Justification::centredRight, true);
    }

    // The selected track's sound.
    void paintOnTrackChip (juce::Graphics& g, juce::Rectangle<int>& content) const
    {
        const juce::String text ("On track");
        const auto font = fonts::uiSemiBold (textSize::xs);
        const auto w = textWidth (font, text) + 14;
        auto chip = content.removeFromRight (w).withSizeKeepingCentre (w, 20).toFloat();

        g.setColour (theme().neutralActive);
        g.fillRoundedRectangle (chip, chip.getHeight() / 2.0f);
        g.setColour (theme().selectedBorder);
        g.drawRoundedRectangle (chip.reduced (0.5f), chip.getHeight() / 2.0f, 1.0f);
        g.setColour (theme().text);
        g.setFont (font);
        g.drawText (text, chip, juce::Justification::centred, false);
    }

    // A pack that isn't installed: its size and a round download button, or
    // its progress while it downloads.
    void paintPackStatus (juce::Graphics& g, juce::Rectangle<int>& content) const
    {
        const auto pack = owner.engine.getPackStore().findPack (item.id);
        const auto state = pack.has_value() ? pack->state : StorePack::State::notInstalled;
        const bool busy = state == StorePack::State::downloading || state == StorePack::State::installing;

        const auto status = state == StorePack::State::downloading ? juce::String (juce::roundToInt (pack->progress * 100.0f)) + "%"
                          : state == StorePack::State::installing  ? juce::String ("Installing...")
                                                                   : item.detail;
        paintTag (g, content, status);

        auto button = getRoundButtonArea().toFloat();
        g.setColour (theme().buttonFill.brighter (isMouseOver() && ! busy ? 0.1f : 0.0f));
        g.fillEllipse (button);
        g.setColour (theme().buttonBorder);
        g.drawEllipse (button.reduced (0.5f), 1.0f);

        if (busy)
        {
            // A progress ring around the button.
            juce::Path arc;
            const auto progress = state == StorePack::State::installing ? 1.0f : pack->progress;
            arc.addCentredArc (button.getCentreX(), button.getCentreY(), button.getWidth() / 2.0f - 1.0f, button.getHeight() / 2.0f - 1.0f,
                               0.0f, 0.0f, juce::MathConstants<float>::twoPi * juce::jlimit (0.02f, 1.0f, progress), true);
            g.setColour (theme().text);
            g.strokePath (arc, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        else
        {
            icons::draw (g, Icon::download, button, theme().textSecondary, 13.0f);
        }
    }
};

//==============================================================================
LibrarySidebar::LibrarySidebar (AudioEngine& e, Selection& s)
    : engine (e), selection (s), loopBrowser (e)
{
    setTitle ("Sound library");
    setFocusContainerType (juce::Component::FocusContainerType::focusContainer);

    searchField.setTextToShowWhenEmpty ("Search sounds and loops", theme().textSecondary);
    searchField.setFont (fonts::ui (textSize::lg));
    searchField.setTitle ("Search sounds");
    searchField.setIndents (36, 9);   // room for the magnifier (see paintOverChildren)
    searchField.onTextChange = [this] { rebuildList(); };
    searchField.onEscapeKey = [this] { searchField.clear(); rebuildList(); };
    addAndMakeVisible (searchField);

    for (auto& c : categories)
    {
        auto* tile = tiles.add (new CategoryTile (c.name, c.label, colour::familyOf (c.name)));
        tile->onClick = [this, n = juce::String (c.name)]
        {
            if (n == "Loops")
                openLoopBrowser();
            else
                selectCategory (n);
        };
        addAndMakeVisible (tile);
    }

    allSoundsButton = std::make_unique<CrumbButton>();
    allSoundsButton->onClick = [this] { showAllSounds(); };
    addChildComponent (*allSoundsButton);

    listViewport.setViewedComponent (&listHolder, false);   // false = we own listHolder, not the viewport
    listViewport.setScrollBarsShown (true, false);
    addAndMakeVisible (listViewport);

    // Tools above the list (only some categories show them).
    for (auto* b : { &upButton, &chooseFolderButton, &scanButton })
    {
        b->setColours (IconButton::borderedColours);
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

    manageSoundsButton.setColours (IconButton::borderedColours);
    manageSoundsButton.setFont (fonts::uiSemiBold (textSize::md));
    manageSoundsButton.setIconSize (16.0f);
    manageSoundsButton.setTooltip ("Download, update and remove sound packs");
    manageSoundsButton.onClick = [this]
    {
        if (onManageSoundsRequested)
            onManageSoundsRequested();
    };
    addAndMakeVisible (manageSoundsButton);

    auto music = juce::File::getSpecialLocation (juce::File::userMusicDirectory);
    audioFolder = music.isDirectory() ? music : juce::File::getSpecialLocation (juce::File::userHomeDirectory);

    loopBrowser.onFreesoundSetupRequested = [this] { if (onFreesoundSetupRequested) onFreesoundSetupRequested(); };
    addChildComponent (loopBrowser);

    // Packs that aren't installed are listed with a download button, so the
    // list of what's on offer is fetched (in the background) when the
    // library first appears, not only when Manage Sounds is opened.
    auto& store = engine.getPackStore();
    store.addChangeListener (this);

    if (store.getIndexStatus() == PackStore::IndexStatus::notLoaded)
        store.refreshIndex();

    selectCategory (selectedCategory);
}

LibrarySidebar::~LibrarySidebar()
{
    engine.getPackStore().removeChangeListener (this);
    engine.getPreview().stop();
}

//==============================================================================
void LibrarySidebar::selectCategory (const juce::String& name)
{
    selectedCategory = name;

    for (auto* t : tiles)
        t->setToggleState (t->name == name, juce::dontSendNotification);

    engine.getPreview().stop();
    previewingSoundId = {};
    searchField.clear();
    rebuildList();
    resized();
}

void LibrarySidebar::showAllSounds()
{
    selectCategory ({});
}

void LibrarySidebar::showPlugins()
{
    if (loopBrowser.isVisible())
        loopBrowser.close();

    selectCategory ("Plug-ins");
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
            files.push_back ({ Item::Kind::file, {}, f.getFileNameWithoutExtension(), describeBytes (f.getSize()), f });
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

    const auto packsToOffer = getPacksToOffer();

    auto addSounds = [&] (const juce::String& category, bool withLabel)
    {
        bool labelled = ! withLabel;

        auto addLabel = [&]
        {
            if (! labelled)
                items.push_back ({ Item::Kind::label, {}, category });

            labelled = true;
        };

        for (auto& s : engine.getSounds().getSounds())
        {
            if (s.category == category && matches (s.name))
            {
                addLabel();
                items.push_back ({ Item::Kind::sound, s.id, s.name, s.detail });
            }
        }

        // Then this category's packs that aren't installed, to download.
        for (auto& p : packsToOffer)
        {
            if (p.category == category && matches (p.name))
            {
                addLabel();
                items.push_back ({ Item::Kind::pack, p.id, p.name, describeBytes (p.downloadSize) });
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

    // Only instruments: plug-in effects are added from the track's effects
    // menu, like the built-in ones.
    auto addPlugins = [&] (bool withLabel)
    {
        bool labelled = ! withLabel;

        for (auto& p : plugins.getInstruments())
        {
            if (matches (p.name) || matches (p.manufacturer))
            {
                if (! labelled)
                    items.push_back ({ Item::Kind::label, {}, "Plug-in instruments" });

                labelled = true;
                items.push_back ({ Item::Kind::pluginInstrument,
                                   p.id, p.name, p.manufacturer.isNotEmpty() ? p.manufacturer : p.format });
            }
        }
    };

    if (query.isNotEmpty())
    {
        // Search everything, grouped by category.
        for (auto& c : categories)
            if (! isFileCategory (c.name))
                addSounds (c.name, true);

        addLoops (true);
        addPlugins (true);
    }
    else if (selectedCategory == "Plug-ins")
    {
        addPlugins (false);
    }
    else if (selectedCategory.isEmpty())
    {
        // All sounds, grouped by category.
        for (auto& c : categories)
            if (! isFileCategory (c.name))
                addSounds (c.name, true);
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

    for (auto& item : collectItems())
        listHolder.addAndMakeVisible (rows.add (new Row (*this, item)));

    // The breadcrumb's last part, and the count on the right. Only what's
    // there to use is counted: not headings, folders or packs to download.
    int count = 0;

    for (auto* row : rows)
        if (row->item.kind != Item::Kind::label && row->item.kind != Item::Kind::folder && row->item.kind != Item::Kind::pack)
            ++count;

    if (selectedCategory == "Audio Files")
    {
        crumbText = "Audio Files";
        countText = countOf (count, "file");
    }
    else if (query.isNotEmpty())
    {
        crumbText = "Results";
        countText = countOf (count, "result");
    }
    else if (selectedCategory == "Plug-ins")
    {
        crumbText = "Plug-ins";
        countText = countOf (count, "instrument");
    }
    else
    {
        crumbText = selectedCategory;   // empty for All sounds, which is the root
        countText = countOf (count, "sound");
    }

    allSoundsButton->setVisible (crumbText.isNotEmpty());

    if (! rows.isEmpty())
        emptyText = {};
    else if (query.isNotEmpty())
        emptyText = "No matches";
    else if (selectedCategory == "Audio Files")
        emptyText = "No audio files in this folder.";
    else if (selectedCategory == "Plug-ins")
        emptyText = "No plug-in instruments found yet. Install VST3 or LV2 instruments, then scan again.";
    else
        emptyText = "No sounds here yet.";

    knownPluginCount = (int) engine.getPlugins().getInstruments().size();
    knownSoundCount = (int) (engine.getSounds().getSounds().size() + engine.getSounds().getLoops().size());
    packSignature = describePacksToOffer();
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
    const auto pluginCount = (int) engine.getPlugins().getInstruments().size();

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

        // The sound's preview ended, or something else started playing.
        if (previewingSoundId.isNotEmpty() && playing != previewingSoundFile)
            previewingSoundId = {};

        updatePreviewButtons();
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
        // How many there are is the breadcrumb's count; this only mentions
        // the ones the scan had to skip (the Scan menu lists them).
        const auto skipped = plugins.getSkippedPlugins().size();
        scanText = skipped > 0 ? juce::String (skipped) + " skipped" : juce::String();
    }

    scanButton.setEnabled (! status.scanning);
}

void LibrarySidebar::layoutList()
{
    // Size the holder first: whether the viewport needs a scrollbar (and so
    // how wide the rows can be) depends on how tall the list is.
    int height = 0;

    for (auto* row : rows)
        height += row->getPreferredHeight() + rowGap;

    listHolder.setSize (listViewport.getWidth(), juce::jmax (1, height));

    const auto w = listViewport.getMaximumVisibleWidth();
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

        case Item::Kind::pack:
            installPack (item.id);
            break;

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

bool LibrarySidebar::canPreview (const juce::String& soundId) const
{
    auto* instrument = engine.getSounds().findSampledInstrument (soundId);
    return instrument != nullptr && ! instrument->zones.empty();
}

void LibrarySidebar::previewSound (const juce::String& soundId)
{
    auto& preview = engine.getPreview();

    // A second click stops it.
    if (previewingSoundId == soundId)
    {
        preview.stop();
        previewingSoundId = {};
        updatePreviewButtons();
        return;
    }

    previewingSoundId = {};

    if (canPreview (soundId))
    {
        // A sampled instrument made of zones: play its sample nearest middle C,
        // as recorded. AudioPreview plays files outside the session, the same
        // way the library previews loops.
        const auto& zones = engine.getSounds().findSampledInstrument (soundId)->zones;
        const auto nearest = std::min_element (zones.begin(), zones.end(), [] (const auto& a, const auto& b)
        {
            return std::abs (a.root - 60) < std::abs (b.root - 60);
        });

        if (preview.play (nearest->file))
        {
            previewingSoundId = soundId;
            previewingSoundFile = nearest->file;
            playingFile = nearest->file;   // so update() doesn't take it for another file starting
        }
    }
    else
    {
        // TODO(milestone-8): preview built-in synths and SFZ instruments too.
        // They have no single sample file to play: a preview needs a short
        // phrase played through the instrument itself (for example rendered
        // offline from a throwaway te::Edit, and cached per sound). That's a
        // bigger job than this UI pass, so for now the button only stops
        // whatever is previewing, and its tooltip says preview isn't available.
        preview.stop();
    }

    updatePreviewButtons();
}

void LibrarySidebar::updatePreviewButtons()
{
    for (auto* row : rows)
        row->updatePreviewButton();   // does nothing on rows without one
}

//==============================================================================
std::vector<StorePack> LibrarySidebar::getPacksToOffer() const
{
    // Packs in the index that aren't on this computer, including those
    // downloading now. (An installed pack's sounds are listed already, even
    // while an update downloads.)
    std::vector<StorePack> packs;

    for (auto& p : engine.getPackStore().getPacks())
        if (p.installedVersion.isEmpty() && p.availableVersion.isNotEmpty())
            packs.push_back (p);

    return packs;
}

juce::String LibrarySidebar::describePacksToOffer() const
{
    juce::String description;

    for (auto& p : getPacksToOffer())
        description << p.id << ':' << (int) p.state << ';';

    return description;
}

void LibrarySidebar::installPack (const juce::String& packId)
{
    auto& store = engine.getPackStore();
    auto pack = store.findPack (packId);

    if (pack.has_value() && pack->state == StorePack::State::notInstalled)
        store.install (packId);
}

void LibrarySidebar::changeListenerCallback (juce::ChangeBroadcaster*)
{
    // The pack store changed: the index arrived, or a download moved on. If
    // the packs on offer (or their states) changed, list them again;
    // otherwise it's just progress, so repaint their rows.
    if (describePacksToOffer() != packSignature)
    {
        rebuildList();
        return;
    }

    for (auto* row : rows)
        if (row->item.kind == Item::Kind::pack)
            row->repaint();
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
                 theme().textSecondary, 15.0f, 1.8f);
}

void LibrarySidebar::paint (juce::Graphics& g)
{
    g.fillAll (theme().librarySidebarBg);
    g.setColour (theme().divider);
    g.fillRect (getLocalBounds().removeFromRight (1));

    // The chosen category tile's shadow (light mode only). It falls outside
    // the tile, so it's drawn here, under the tiles, rather than by the tile.
    for (auto* t : tiles)
        if (t->isVisible() && t->getToggleState())
            juce::DropShadow (theme().selectedTileShadow, 4, { 0, 1 }).drawForRectangle (g, t->getBounds());

    // The count on the right, then the breadcrumb: "All sounds" (a button
    // when there's somewhere to go back from) › the category.
    auto crumb = crumbArea;
    g.setColour (theme().textSecondary);
    g.setFont (fonts::ui (textSize::sm));
    crumb.removeFromRight (textWidth (g.getCurrentFont(), countText) + 8);
    g.drawText (countText, crumbArea, juce::Justification::centredRight, false);

    if (crumbText.isEmpty())
    {
        g.setColour (theme().text);
        g.setFont (fonts::uiSemiBold (textSize::md));
        g.drawText ("All sounds", crumb, juce::Justification::centredLeft, true);
    }
    else
    {
        crumb.setLeft (allSoundsButton->getRight());
        const juce::String separator (juce::CharPointer_UTF8 ("  \xe2\x80\xba  "));   // "›"
        g.setFont (CrumbButton::getFont());
        g.drawText (separator, crumb.removeFromLeft (textWidth (g.getCurrentFont(), separator)), juce::Justification::centred, false);

        g.setColour (theme().text);
        g.setFont (fonts::uiSemiBold (textSize::md));
        g.drawText (crumbText, crumb, juce::Justification::centredLeft, true);
    }

    // Tools above the list: the folder being shown, or the plug-in scan.
    g.setColour (theme().textSecondary);
    g.setFont (fonts::ui (textSize::sm));

    if (upButton.isVisible())
        g.drawText (audioFolder.getFileName(), toolsArea.withLeft (upButton.getRight() + 8).withRight (chooseFolderButton.getX() - 8),
                    juce::Justification::centredLeft, true);

    if (scanButton.isVisible())
        g.drawText (scanText, toolsArea.withRight (scanButton.getX() - 6), juce::Justification::centredLeft, true);

    if (rows.isEmpty())
    {
        g.setFont (fonts::ui (textSize::md));
        g.drawFittedText (emptyText, listViewport.getBounds().removeFromTop (50).withTrimmedLeft (size::libraryRowInset),
                          juce::Justification::topLeft, 3, 1.0f);
    }
}

void LibrarySidebar::resized()
{
    loopBrowser.setBounds (getLocalBounds());

    auto r = getLocalBounds().withTrimmedRight (1).reduced (12);

    searchField.setBounds (r.removeFromTop (36));
    r.removeFromTop (12);

    manageSoundsButton.setBounds (r.removeFromBottom (34));
    r.removeFromBottom (10);

    // Category tiles, 4 to a row. The width rarely divides evenly, so the
    // last column takes the spare pixels and the grid's right edge lines up
    // with the search field's.
    const int tileW = (r.getWidth() - (tileColumns - 1) * tileGap) / tileColumns;

    for (int i = 0; i < tiles.size(); ++i)
    {
        const int row = i / tileColumns, col = i % tileColumns;
        const int x = r.getX() + col * (tileW + tileGap);
        tiles[i]->setBounds (x, r.getY() + row * (size::libraryTileHeight + tileGap),
                             col == tileColumns - 1 ? r.getRight() - x : tileW, size::libraryTileHeight);
    }

    const int tileRows = (tiles.size() + tileColumns - 1) / tileColumns;
    r.removeFromTop (tileRows * (size::libraryTileHeight + tileGap) - tileGap + 12);

    // The breadcrumb and count, inset like the rows' text so they line up
    // with the names and tags below. (layoutList moves the count in when
    // the list has a scrollbar.)
    crumbArea = r.removeFromTop (crumbHeight).reduced (size::libraryRowInset, 0);
    allSoundsButton->setBounds (crumbArea.withWidth (allSoundsButton->getIdealWidth()));
    r.removeFromTop (4);

    // Tools: folder navigation for Audio Files, scanning for Plug-ins.
    const bool folderTools = hasTools() && selectedCategory == "Audio Files";
    const bool scanTools = hasTools() && selectedCategory == "Plug-ins";
    upButton.setVisible (folderTools);
    chooseFolderButton.setVisible (folderTools);
    scanButton.setVisible (scanTools);

    if (hasTools())
    {
        toolsArea = r.removeFromTop (28);
        auto tools = toolsArea;

        if (folderTools)
        {
            upButton.setBounds (tools.removeFromLeft (28));
            chooseFolderButton.setBounds (tools.removeFromRight (84));
        }
        else
        {
            toolsArea = toolsArea.withTrimmedLeft (size::libraryRowInset);
            scanButton.setBounds (tools.removeFromRight (72));
        }

        r.removeFromTop (6);
    }
    else
    {
        toolsArea = {};
    }

    listViewport.setBounds (r);
    layoutList();

    // With a scrollbar, the rows (and their tags) end before it: keep the
    // count lined up with them.
    crumbArea.setRight (listViewport.getX() + listViewport.getMaximumVisibleWidth() - size::libraryRowInset);
}

} // namespace ikaros
