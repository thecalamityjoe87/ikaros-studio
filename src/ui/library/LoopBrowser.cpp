#include "ui/library/LoopBrowser.h"
#include "engine/AudioPreview.h"
#include "engine/FreesoundStore.h"
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
    constexpr int rowHeight = 34, rowGap = 2, actionWidth = 28, searchDelayMs = 600;

    struct TempoRange { const char* label; int min, max; };   // 0: no limit at that end

    constexpr TempoRange tempos[] {
        { "Any tempo", 0, 0 }, { "Under 90", 0, 90 }, { "90 - 110", 90, 110 },
        { "110 - 130", 110, 130 }, { "130 - 150", 130, 150 }, { "Over 150", 150, 0 },
    };

    bool fitsTempo (double bpm, const TempoRange& t)
    {
        if (t.min == 0 && t.max == 0)
            return true;

        return bpm > 0.0 && bpm >= t.min && (t.max == 0 || bpm <= t.max);
    }

    // One button: the tags it stands for, any one of which counts.
    struct FilterOption { const char* label; const char* words; };
    struct FilterGroup  { const char* name; std::vector<FilterOption> options; };

    const std::vector<FilterGroup>& filterGroups()
    {
        static const std::vector<FilterGroup> groups {
            { "Instrument", {
                { "Drums",         "drums drum beat beats kick snare hihat hat" },
                { "Percussion",    "percussion shaker conga bongo tambourine" },
                { "Bass",          "bass sub 808" },
                { "Guitar",        "guitar riff" },
                { "Keys",          "keys piano keyboard rhodes organ chords" },
                { "Synth",         "synth synthesizer arp arpeggio pad lead" },
                { "Strings",       "strings violin cello orchestral" },
                { "Brass & Winds", "brass trumpet saxophone sax flute" },
                { "Vocals",        "vocal vocals voice choir" } } },
            { "Genre", {
                { "Hip Hop",       "hiphop hip-hop trap boombap" },
                { "Electronic",    "electronic edm techno electro" },
                { "House",         "house deephouse deep-house" },
                { "Rock",          "rock punk metal" },
                { "Funk & Soul",   "funk soul disco groove" },
                { "Jazz & Blues",  "jazz blues swing" },
                { "Pop",           "pop" },
                { "Ambient",       "ambient atmospheric lofi" },
                { "Latin & World", "latin reggae afrobeat world" } } },
            { "Mood", {
                { "Chill",         "chill relaxed calm mellow" },
                { "Dark",          "dark ominous eerie" },
                { "Happy",         "happy upbeat uplifting" },
                { "Energetic",     "energetic aggressive intense" },
                { "Dreamy",        "dreamy ethereal" } } },
        };

        return groups;
    }

    // A loop fits a button if one of its tags is a word of it, or a word of its name starts like one.
    bool matchesOption (const LibraryFile& loop, const FilterOption& option)
    {
        juce::String cleaned;

        for (auto c : loop.name.toLowerCase())
            cleaned += juce::CharacterFunctions::isLetterOrDigit (c) ? c : (juce::juce_wchar) ' ';

        const auto nameWords = juce::StringArray::fromTokens (cleaned, " ", {});

        for (auto& word : juce::StringArray::fromTokens (option.words, " ", {}))
        {
            if (loop.tags.contains (word))
                return true;

            for (auto& n : nameWords)
                if (n.startsWith (word))
                    return true;
        }

        return false;
    }
}

//==============================================================================
class LoopBrowser::Row  : public juce::Button
{
public:
    Row (LoopBrowser& o, const Item& i) : juce::Button (i.name), item (i), owner (o)
    {
        setTitle (i.name);
        setMouseClickGrabsKeyboardFocus (false);
        setMouseCursor (juce::MouseCursor::PointingHandCursor);

        onClick = [this]
        {
            if (dragged)
                return;

            if (hasAction() && getMouseXYRelative().x >= getWidth() - actionWidth - 10)
                owner.runAction (item);
            else
                owner.activate (item);
        };
    }

    int getPreferredHeight() const    { return rowHeight; }

    juce::String getTooltip() override
    {
        if (item.kind == Item::Kind::more)
            return "Show more loops";

        if (hasAction() && getMouseXYRelative().x >= getWidth() - actionWidth - 10)
            return item.removable ? "Remove this loop from your library" : "Add this loop to your library";

        return item.kind == Item::Kind::loop || item.added ? "Click to listen. Drag onto the timeline to use it."
                                                           : "Click to listen. Press + to add it to your library.";
    }

    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        auto r = getLocalBounds().toFloat();

        if (item.kind == Item::Kind::more)
        {
            g.setColour (over ? colour::text : colour::textSecondary);
            g.setFont (fonts::uiSemiBold (textSize::md));
            g.drawText (item.name, getLocalBounds(), juce::Justification::centred);
            return;
        }

        const bool playing = owner.playingFile == item.file;

        if (playing)
            g.setColour (colour::accent.withAlpha (0.14f));
        else
            g.setColour (over ? colour::raised.withAlpha (down ? 1.0f : 0.6f) : juce::Colours::transparentBlack);

        g.fillRoundedRectangle (r, size::radius);

        auto content = getLocalBounds().reduced (10, 0);
        icons::draw (g, playing ? Icon::stop : Icon::play, content.removeFromLeft (16).toFloat(),
                     playing ? colour::accent : colour::textSecondary, 12.0f);
        content.removeFromLeft (8);

        // In your own loops only some rows have a bin, so it shows on hover and the details stay lined up.
        const bool busy = hasAction() && owner.engine.getFreesound().isBusy (item.id);

        if (hasAction() && (over || busy || item.kind == Item::Kind::result))
        {
            auto zone = content.removeFromRight (actionWidth).toFloat();
            const bool onAction = over && getMouseXYRelative().x >= getWidth() - actionWidth - 10;

            if (busy)
            {
                g.setColour (colour::textSecondary);
                g.setFont (fonts::ui (textSize::lg));
                g.drawText ("...", zone.toNearestInt(), juce::Justification::centredRight);
            }
            else if (item.removable)
            {
                icons::draw (g, Icon::trash, zone.removeFromRight (15.0f), onAction ? colour::record : colour::textSecondary, 15.0f);
            }
            else
            {
                icons::draw (g, Icon::plus, zone.removeFromRight (14.0f), onAction ? colour::accent : colour::textSecondary, 14.0f);
            }
        }

        g.setFont (fonts::ui (textSize::sm));
        auto detailWidth = juce::jmin (content.getWidth() / 2,
                                       (int) std::ceil (juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), item.detail)) + 4);
        g.setColour (item.added ? colour::effectOn : colour::textSecondary);
        g.drawText (item.detail, content.removeFromRight (detailWidth), juce::Justification::centredRight, true);

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

    void mouseMove (const juce::MouseEvent& e) override
    {
        juce::Button::mouseMove (e);

        if (hasAction())
            repaint();   // the + or bin lights up under the mouse
    }

    // Loops in the library can be dragged onto the timeline.
    void mouseDrag (const juce::MouseEvent& e) override
    {
        juce::Button::mouseDrag (e);

        if (dragged || e.getDistanceFromDragStart() <= 6)
            return;

        if (item.kind == Item::Kind::more || (item.kind == Item::Kind::result && ! item.added))
            return;

        if (auto* container = juce::DragAndDropContainer::findParentDragContainerFor (this))
        {
            dragged = true;
            container->startDragging (dragtypes::describeLibraryFile (item.file), this);
        }
    }

    const Item item;

private:
    LoopBrowser& owner;
    bool dragged = false;

    bool hasAction() const    { return item.kind == Item::Kind::result || item.removable; }
};

//==============================================================================
LoopBrowser::LoopBrowser (AudioEngine& e)
    : engine (e)
{
    setTitle ("Loop browser");
    setOpaque (true);
    setFocusContainerType (juce::Component::FocusContainerType::focusContainer);

    auto grey = IconButton::raisedColours();
    grey.border = colour::border;

    for (auto* b : { &backButton, &mineTab, &freesoundTab, &tempoButton })
    {
        b->setColours (grey);
        b->setCornerRadius (6.0f);
        b->setFont (fonts::uiSemiBold (textSize::sm));
        b->setIconSize (14.0f);
        addAndMakeVisible (b);
    }

    for (int i = 0; i < (int) filterGroups().size(); ++i)
    {
        const char* name = filterGroups()[(size_t) i].name;
        auto* tab = filterTabs.add (new IconButton (name, std::nullopt, name));
        tab->setColours (grey);
        tab->setCornerRadius (6.0f);
        tab->setFont (fonts::uiSemiBold (textSize::xs));
        tab->setTooltip (juce::String ("Choose a ") + juce::String (name).toLowerCase() + " to narrow the loops. Click again to fold it away.");
        tab->onClick = [this, i]
        {
            filterTab = filterTab == i ? -1 : i;
            buildChips();
            updateClearButton();
            resized();
            repaint();
        };
        addAndMakeVisible (tab);
    }

    buildChips();

    clearButton.setColours (grey);
    clearButton.setCornerRadius (6.0f);
    clearButton.setIconSize (12.0f);
    clearButton.setTooltip ("Clear all filters: instrument, genre, mood, tempo and search");
    clearButton.onClick = [this] { clearFilters(); };
    addAndMakeVisible (clearButton);

    setupButton.setColours (IconButton::accentColours());
    setupButton.setFont (fonts::uiSemiBold (textSize::md));
    addChildComponent (setupButton);

    backButton.onClick = [this] { close(); };
    setupButton.onClick = [this] { if (onFreesoundSetupRequested) onFreesoundSetupRequested(); };
    mineTab.setTooltip ("The loops in your library");
    mineTab.onClick = [this] { setScope (Scope::mine); };
    freesoundTab.setTooltip ("Search freesound.org for free loops");
    freesoundTab.onClick = [this] { setScope (Scope::freesound); };
    tempoButton.setTooltip ("Only show loops at a tempo");
    tempoButton.onClick = [this] { showTempoMenu(); };

    searchField.setFont (fonts::ui (textSize::lg));
    searchField.setTitle ("Search loops");
    searchField.setIndents (36, 9);   // room for the magnifier (see paintOverChildren)
    searchField.onTextChange = [this]
    {
        if (scope == Scope::mine)
        {
            mineQuery = searchField.getText();
            rebuild();
        }
        else
        {
            freesoundQuery = searchField.getText();
            updateClearButton();
            startTimer (searchDelayMs);   // not on every key: Freesound limits how often it can be asked
        }
    };
    searchField.onReturnKey = [this] { if (scope == Scope::freesound) searchFreesound(); };
    searchField.onEscapeKey = [this]
    {
        if (searchField.isEmpty())
            close();
        else
            searchField.clear();
    };
    addAndMakeVisible (searchField);

    viewport.setViewedComponent (&holder, false);   // false: we own holder, not the viewport
    viewport.setScrollBarsShown (true, false);
    addAndMakeVisible (viewport);
    setupButton.toFront (false);   // it sits over the empty list
    auto& store = engine.getFreesound();
    store.addChangeListener (this);
    store.onPreviewReady = [this] (const juce::File& file)
    {
        if (isVisible())
        {
            engine.getPreview().play (file);
            update();
        }
    };

    setVisible (false);
    setScope (Scope::mine);
}

LoopBrowser::~LoopBrowser()
{
    auto& store = engine.getFreesound();
    store.removeChangeListener (this);
    store.onPreviewReady = nullptr;
}

//==============================================================================
void LoopBrowser::open()
{
    setVisible (true);
    toFront (true);
    searchField.grabKeyboardFocus();

    if (scope == Scope::freesound && freesoundNeedsSearch())
        searchFreesound();

    rebuild();
}

void LoopBrowser::close()
{
    stopTimer();
    engine.getPreview().stop();
    setVisible (false);
    juce::Component::unfocusAllComponents();
}

void LoopBrowser::refresh()
{
    if (isVisible() && (int) engine.getSounds().getLoops().size() != knownLoopCount)
        rebuild();
}

void LoopBrowser::update()
{
    if (! isVisible())
        return;

    auto playing = engine.getPreview().getPlayingFile();

    if (playing != playingFile)
    {
        playingFile = playing;

        for (auto* row : rows)
            row->repaint();
    }
}

bool LoopBrowser::usingFreesoundWithoutKey() const
{
    return scope == Scope::freesound && ! engine.getFreesound().hasKey();
}

//==============================================================================
void LoopBrowser::setScope (Scope s)
{
    // Each side remembers what was typed in it.
    (scope == Scope::mine ? mineQuery : freesoundQuery) = searchField.getText();
    scope = s;
    stopTimer();
    engine.getPreview().stop();

    mineTab.setToggleState (scope == Scope::mine, juce::dontSendNotification);
    freesoundTab.setToggleState (scope == Scope::freesound, juce::dontSendNotification);
    searchField.setText (scope == Scope::mine ? mineQuery : freesoundQuery, false);
    searchField.setTextToShowWhenEmpty (scope == Scope::mine ? "Search your loops" : "Search Freesound loops",
                                        colour::textSecondary);

    if (scope == Scope::freesound && freesoundNeedsSearch())
        searchFreesound();   // the most downloaded, to start with

    rebuild();
    resized();
}

bool LoopBrowser::freesoundNeedsSearch() const
{
    auto& store = engine.getFreesound();
    return store.hasKey() && (store.getStatus() == FreesoundStore::Status::idle || searchSignature() != lastFreesoundSearch);
}

// The choices that change what Freesound is asked, so the list is searched again only when one did.
juce::String LoopBrowser::searchSignature() const
{
    return freesoundQuery.trim() + "|" + juce::String (tempoIndex) + "|" + juce::String (chosen[0]) + juce::String (chosen[1]) + juce::String (chosen[2]);
}

FreesoundFilters LoopBrowser::currentFilters() const
{
    FreesoundFilters filters;
    filters.minBpm = tempos[tempoIndex].min;
    filters.maxBpm = tempos[tempoIndex].max;

    for (int g = 0; g < 3; ++g)
        if (chosen[g] >= 0)
            filters.anyOf.push_back (juce::StringArray::fromTokens (filterGroups()[(size_t) g].options[(size_t) chosen[g]].words, " ", {}));

    return filters;
}

bool LoopBrowser::fitsFilters (const LibraryFile& loop) const
{
    if (! fitsTempo (loop.bpm, tempos[tempoIndex]))
        return false;

    for (int g = 0; g < 3; ++g)
        if (chosen[g] >= 0 && ! matchesOption (loop, filterGroups()[(size_t) g].options[(size_t) chosen[g]]))
            return false;

    return true;
}

juce::StringArray LoopBrowser::chosenLabels() const
{
    juce::StringArray labels;

    for (int g = 0; g < 3; ++g)
        if (chosen[g] >= 0)
            labels.add (filterGroups()[(size_t) g].options[(size_t) chosen[g]].label);

    if (tempoIndex != 0)
        labels.add (tempos[tempoIndex].label);

    return labels;
}

void LoopBrowser::buildChips()
{
    chips.clear();

    for (int i = 0; i < filterTabs.size(); ++i)
        filterTabs[i]->setToggleState (i == filterTab, juce::dontSendNotification);

    if (filterTab < 0)
        return;

    auto grey = IconButton::raisedColours();
    grey.border = colour::border;
    auto& options = filterGroups()[(size_t) filterTab].options;

    for (int i = 0; i < (int) options.size(); ++i)
    {
        const char* label = options[(size_t) i].label;
        auto* chip = chips.add (new IconButton (label, std::nullopt, label));
        chip->setColours (grey);
        chip->setCornerRadius (6.0f);
        chip->setFont (fonts::ui (textSize::sm));
        chip->setToggleState (chosen[filterTab] == i, juce::dontSendNotification);
        chip->onClick = [this, i]
        {
            chosen[filterTab] = chosen[filterTab] == i ? -1 : i;
            filterChanged();
        };
        addAndMakeVisible (chip);
    }
}

void LoopBrowser::filterChanged()
{
    for (int i = 0; i < chips.size(); ++i)
        chips[i]->setToggleState (filterTab >= 0 && chosen[filterTab] == i, juce::dontSendNotification);

    if (scope == Scope::freesound)
    {
        stopTimer();
        searchFreesound();
    }

    rebuild();
}

void LoopBrowser::clearFilters()
{
    std::fill (std::begin (chosen), std::end (chosen), -1);
    tempoIndex = 0;
    filterTab = -1;
    (scope == Scope::mine ? mineQuery : freesoundQuery) = {};
    searchField.setText ({}, false);
    buildChips();
    filterChanged();
    resized();   // the grid folded and the tempo button's label changed
}

// The X can clear anything: an open tab, a chosen button, the tempo or the search.
void LoopBrowser::updateClearButton()
{
    clearButton.setEnabled (filterTab >= 0 || ! chosenLabels().isEmpty() || searchField.getText().isNotEmpty());
}

void LoopBrowser::searchFreesound()
{
    auto& store = engine.getFreesound();

    if (store.hasKey())
    {
        lastFreesoundSearch = searchSignature();
        store.search (freesoundQuery, currentFilters());
    }
}

void LoopBrowser::timerCallback()
{
    stopTimer();
    searchFreesound();
}

void LoopBrowser::showTempoMenu()
{
    juce::PopupMenu menu;

    for (int i = 0; i < (int) std::size (tempos); ++i)
        menu.addItem (i + 1, tempos[i].label, true, i == tempoIndex);

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&tempoButton),
                        [safeThis = SafePointer<LoopBrowser> (this)] (int choice)
    {
        if (safeThis == nullptr || choice == 0)
            return;

        safeThis->tempoIndex = choice - 1;

        if (safeThis->scope == Scope::freesound)
        {
            safeThis->stopTimer();
            safeThis->searchFreesound();
        }

        safeThis->rebuild();
        safeThis->resized();
    });
}

void LoopBrowser::changeListenerCallback (juce::ChangeBroadcaster*)
{
    if (! isVisible())
        return;

    // A key was just accepted in the setup window: start with the most downloaded loops.
    auto& store = engine.getFreesound();

    if (scope == Scope::freesound && store.hasKey() && store.getStatus() == FreesoundStore::Status::idle)
        searchFreesound();

    rebuild();
    resized();
}

//==============================================================================
std::vector<LoopBrowser::Item> LoopBrowser::collectItems() const
{
    auto& store = engine.getFreesound();
    std::vector<Item> items;

    if (scope == Scope::mine)
    {
        const auto query = mineQuery.trim();

        for (auto& l : engine.getSounds().getLoops())
        {
            if ((query.isEmpty() || l.name.containsIgnoreCase (query)) && fitsFilters (l))
            {
                const auto id = store.findAddedId (l.file);
                items.push_back ({ Item::Kind::loop, id, l.name, l.detail, l.file, false, id.isNotEmpty() });
            }
        }

        return items;
    }

    for (auto& s : store.getResults())
    {
        const bool added = store.isAdded (s.id);
        items.push_back ({ Item::Kind::result, s.id, s.name, s.describe(), store.getFileFor (s.id), added, added });
    }

    if (store.canLoadMore())
        items.push_back ({ Item::Kind::more, {}, "Load more loops" });

    return items;
}

void LoopBrowser::rebuild()
{
    auto& store = engine.getFreesound();
    rows.clear();

    for (auto& item : collectItems())
        holder.addAndMakeVisible (rows.add (new Row (*this, item)));

    knownLoopCount = (int) engine.getSounds().getLoops().size();

    const auto filters = chosenLabels();
    const bool filtered = ! filters.isEmpty();

    if (scope == Scope::mine)
    {
        auto parts = juce::StringArray (juce::String (rows.size()) + (rows.size() == 1 ? " loop" : " loops"));
        parts.addArray (filters);
        heading = parts.joinIntoString (juce::String::fromUTF8 (" \xc2\xb7 ")).toUpperCase();
    }
    else if (store.getNotice().isNotEmpty())
    {
        heading = "COULDN'T GET THAT LOOP";
    }
    else
    {
        auto parts = juce::StringArray ("Freesound");
        parts.addArray (filters);
        heading = parts.joinIntoString (juce::String::fromUTF8 (" \xc2\xb7 ")).toUpperCase();
    }

    if (! rows.isEmpty())
        emptyText = {};
    else if (scope == Scope::mine)
        emptyText = mineQuery.trim().isNotEmpty() || filtered ? juce::String ("No loops match.")
                                                                      : juce::String ("No loops yet. Search Freesound for free ones.");
    else if (! store.hasKey())
        emptyText = "Search Freesound's free loops right here. It takes a free API key, and we'll show you how to get one.";
    else if (store.getStatus() == FreesoundStore::Status::searching)
        emptyText = "Searching Freesound...";
    else if (store.getStatus() == FreesoundStore::Status::failed)
        emptyText = store.getMessage() + (store.keyWasRefused() ? " Choose Sounds > Freesound API Key to enter a new one." : "");
    else if (store.getStatus() == FreesoundStore::Status::ready)
        emptyText = "No loops found. Try different words or another tempo.";
    else
        emptyText = "Type what you're looking for.";

    const bool noKey = usingFreesoundWithoutKey();
    setupButton.setVisible (noKey);
    searchField.setEnabled (! noKey);
    tempoButton.setText (tempoIndex == 0 ? juce::String ("Tempo") : juce::String ("Tempo: ") + tempos[tempoIndex].label);
    updateClearButton();

    layoutList();
    repaint();
}

void LoopBrowser::layoutList()
{
    const auto w = viewport.getMaximumVisibleWidth();
    int y = 0;

    for (auto* row : rows)
    {
        row->setBounds (0, y, w, row->getPreferredHeight());
        y += row->getPreferredHeight() + rowGap;
    }

    holder.setSize (w, juce::jmax (1, y));
}

//==============================================================================
void LoopBrowser::activate (const Item& item)
{
    auto& preview = engine.getPreview();
    auto& store = engine.getFreesound();

    if (item.kind == Item::Kind::more)
    {
        store.loadMore();
        return;
    }

    if (preview.getPlayingFile() == item.file)
    {
        preview.stop();
        update();
    }
    else if (item.kind == Item::Kind::loop)
    {
        preview.play (item.file);
        update();
    }
    else if (auto sound = store.findResult (item.id))
    {
        store.preview (*sound);   // plays when it has downloaded
    }
}

void LoopBrowser::runAction (const Item& item)
{
    auto& store = engine.getFreesound();

    if (item.removable)
    {
        confirmRemove (item);
    }
    else if (auto sound = store.findResult (item.id))
    {
        store.addToLibrary (*sound);
    }
}

void LoopBrowser::confirmRemove (const Item& item)
{
    auto options = juce::MessageBoxOptions()
                       .withIconType (juce::MessageBoxIconType::QuestionIcon)
                       .withTitle ("Remove \"" + item.name + "\"?")
                       .withMessage ("The loop is deleted from this computer. You can add it from Freesound again.")
                       .withButton ("Remove")
                       .withButton ("Cancel")
                       .withAssociatedComponent (this);

    // The answer arrives later (no modal loops), so check we still exist.
    juce::AlertWindow::showAsync (options, [safeThis = SafePointer<LoopBrowser> (this), item] (int result)
    {
        if (safeThis == nullptr || result != 1)
            return;

        auto& engine = safeThis->engine;

        if (engine.getPreview().getPlayingFile() == item.file)
            engine.getPreview().stop();

        if (! engine.getFreesound().removeFromLibrary (item.id))
            juce::AlertWindow::showAsync (juce::MessageBoxOptions()
                                              .withIconType (juce::MessageBoxIconType::WarningIcon)
                                              .withTitle ("Couldn't remove the loop")
                                              .withMessage ("Check that its file isn't open elsewhere, then try again.")
                                              .withButton ("OK")
                                              .withAssociatedComponent (safeThis.getComponent()),
                                          nullptr);
    });
}

//==============================================================================
void LoopBrowser::paintOverChildren (juce::Graphics& g)
{
    // Magnifier inside the search field (its left indent leaves room).
    icons::draw (g, Icon::search, searchField.getBounds().removeFromLeft (34).toFloat().translated (2.0f, 0.0f),
                 searchField.isEnabled() ? colour::textSecondary : colour::borderStrong, 15.0f, 1.8f);
}

void LoopBrowser::paint (juce::Graphics& g)
{
    g.fillAll (colour::sidebarBg);
    g.setColour (colour::divider);
    g.fillRect (getLocalBounds().removeFromRight (1));

    g.setColour (colour::textSecondary);
    g.setFont (fonts::uiBold (textSize::xs).withExtraKerningFactor (0.1f));
    g.drawText (heading, headingArea, juce::Justification::centredLeft, true);

    if (rows.isEmpty())
    {
        g.setFont (fonts::ui (textSize::md));
        g.drawFittedText (emptyText, emptyArea, juce::Justification::topLeft, 5, 1.0f);
    }

    if (scope == Scope::freesound)
    {
        g.setColour (colour::textSecondary);
        g.setFont (fonts::ui (textSize::xs));
        g.drawFittedText ("Loops from freesound.org, shared under CC0 or CC BY. Each one is credited to its author.",
                          footerArea, juce::Justification::centredLeft, 3, 1.0f);
    }
}

void LoopBrowser::resized()
{
    auto r = getLocalBounds().withTrimmedRight (1).reduced (12);

    backButton.setBounds (r.removeFromTop (30).removeFromLeft (92));
    r.removeFromTop (10);

    auto tabs = r.removeFromTop (30);
    const int half = (tabs.getWidth() - 6) / 2;
    mineTab.setBounds (tabs.removeFromLeft (half));
    tabs.removeFromLeft (6);
    freesoundTab.setBounds (tabs);
    r.removeFromTop (10);

    searchField.setBounds (r.removeFromTop (36));
    r.removeFromTop (8);

    // The tabs and Clear share the row equally; the last one takes any odd pixels.
    auto filterRow = r.removeFromTop (26);
    juce::Array<juce::Component*> filterButtons;

    for (auto* tab : filterTabs)
        filterButtons.add (tab);

    filterButtons.add (&clearButton);
    const int filterGap = 4;
    const int filterWidth = (filterRow.getWidth() - filterGap * (filterButtons.size() - 1)) / filterButtons.size();

    for (int i = 0; i < filterButtons.size(); ++i)
    {
        filterButtons[i]->setBounds (i == filterButtons.size() - 1 ? filterRow : filterRow.removeFromLeft (filterWidth));
        filterRow.removeFromLeft (filterGap);
    }

    // The open tab's buttons, in two columns (three when there's room).
    if (! chips.isEmpty())
    {
        r.removeFromTop (6);
        const int columns = r.getWidth() >= 300 ? 3 : 2, gap = 4, chipHeight = 26;
        const int lines = (chips.size() + columns - 1) / columns;
        auto grid = r.removeFromTop (lines * chipHeight + (lines - 1) * gap);
        const int chipWidth = (grid.getWidth() - gap * (columns - 1)) / columns;

        for (int i = 0; i < chips.size(); ++i)
            chips[i]->setBounds (grid.getX() + (i % columns) * (chipWidth + gap),
                                 grid.getY() + (i / columns) * (chipHeight + gap), chipWidth, chipHeight);
    }

    r.removeFromTop (6);
    tempoButton.setBounds (r.removeFromTop (28));
    r.removeFromTop (12);

    headingArea = r.removeFromTop (20).withTrimmedLeft (2);

    if (scope == Scope::freesound)
    {
        footerArea = r.removeFromBottom (44).withTrimmedLeft (2);
        r.removeFromBottom (10);
    }

    r.removeFromTop (4);
    viewport.setBounds (r);
    emptyArea = r.removeFromTop (88).withTrimmedLeft (2);
    setupButton.setBounds (r.withTrimmedTop (4).removeFromTop (34));
    layoutList();
}

} // namespace ikaros
