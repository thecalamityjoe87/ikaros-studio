#include "ui/dialogs/SessionChooser.h"
#include "ui/theme/Fonts.h"
#include "ui/theme/IkarosLookAndFeel.h"
#include "ui/theme/Tokens.h"

namespace ikaros
{

using namespace tokens;

namespace
{
    constexpr int tileWidth = 200, tileGap = 16;
    constexpr int sidebarWidth = 210, headerHeight = 64, footerHeight = 72;

    struct TemplateInfo
    {
        SessionTemplate kind;
        const char* name;
        const char* description;
        Icon icon;
        juce::Colour colour;
    };

    const TemplateInfo templates[]
    {
        { SessionTemplate::empty,      "Empty Session",       "One instrument track, ready to play.",       Icon::plus,      colour::accent },
        { SessionTemplate::keyboards,  "Keyboard Collection", "Piano, electric piano and organ.",           Icon::keyboards, colour::trackBlue },
        { SessionTemplate::voice,      "Voice",               "A vocal track and a piano to sing along to.", Icon::mic,       colour::trackOrange },
        { SessionTemplate::beats,      "Beat",                "Drum kit, synth bass and a pad.",            Icon::drums,     colour::trackPurple },
        { SessionTemplate::songwriter, "Songwriter",          "Drums, bass, guitar, piano and vocals.",     Icon::guitar,    colour::trackGreen },
    };

    // "Today, 2:05 PM", "Yesterday, 9:30 AM", or "Oct 9, 2026".
    juce::String describeDate (juce::Time t)
    {
        const auto now = juce::Time::getCurrentTime();
        auto sameDay = [] (juce::Time a, juce::Time b)
        {
            return a.getYear() == b.getYear() && a.getDayOfYear() == b.getDayOfYear();
        };

        auto clock = [t]
        {
            const int hour = t.getHours() % 12 == 0 ? 12 : t.getHours() % 12;
            return juce::String (hour) + ":" + juce::String (t.getMinutes()).paddedLeft ('0', 2)
                 + (t.getHours() < 12 ? " AM" : " PM");
        };

        if (sameDay (t, now))
            return "Today, " + clock();

        if (sameDay (t, now - juce::RelativeTime::days (1)))
            return "Yesterday, " + clock();

        return t.getMonthName (true) + " " + juce::String (t.getDayOfMonth()) + ", " + juce::String (t.getYear());
    }
}

//==============================================================================
// One choice in the grid: a picture (a template's icon, or a session's
// preview), a name, and a line of detail underneath.
class SessionChooser::Tile  : public juce::Button
{
public:
    Tile (const juce::String& name, const juce::String& detailText)
        : juce::Button (name), detail (detailText)
    {
        setTitle (name);
        setDescription (detailText);
        setWantsKeyboardFocus (false);   // the chooser handles the arrow keys
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    void setTemplateArt (Icon i, juce::Colour c)    { icon = i; iconColour = c; }
    void setPreview (const juce::Image& image)       { preview = image; }

    std::function<void()> onDoubleClick;
    void mouseDoubleClick (const juce::MouseEvent&) override   { if (onDoubleClick) onDoubleClick(); }

    static int heightFor (int width)    { return artHeightFor (width) + 64; }

    void paintButton (juce::Graphics& g, bool over, bool) override
    {
        auto r = getLocalBounds().toFloat().reduced (1.0f);
        const bool on = getToggleState();

        g.setColour (on ? colour::selectedRow : (over ? colour::raised : colour::cardBg));
        g.fillRoundedRectangle (r, 12.0f);

        auto art = getLocalBounds().reduced (8).removeFromTop (artHeightFor (getWidth())).toFloat();
        paintArt (g, art);

        auto text = getLocalBounds().reduced (12, 0).withTrimmedTop ((int) art.getBottom() + 8);
        g.setColour (colour::text);
        g.setFont (fonts::uiBold (14.0f));
        g.drawText (getName(), text.removeFromTop (20), juce::Justification::centredLeft, true);
        g.setColour (colour::textSecondary);
        g.setFont (fonts::ui (12.0f));
        g.drawFittedText (detail, text.removeFromTop (30), juce::Justification::topLeft, 2, 1.0f);

        g.setColour (on ? colour::accent : colour::borderSoft);
        g.drawRoundedRectangle (r, 12.0f, on ? 2.0f : 1.0f);
    }

private:
    juce::String detail;
    std::optional<Icon> icon;
    juce::Colour iconColour;
    juce::Image preview;

    static int artHeightFor (int width)    { return (width - 16) * 5 / 8; }   // 16:10

    void paintArt (juce::Graphics& g, juce::Rectangle<float> art)
    {
        juce::Graphics::ScopedSaveState save (g);
        juce::Path shape;
        shape.addRoundedRectangle (art, 8.0f);
        g.reduceClipRegion (shape);

        if (icon.has_value())
        {
            // A template: its colour fading down, with its icon.
            g.setGradientFill (juce::ColourGradient (iconColour.withAlpha (0.35f), art.getTopLeft(),
                                                     iconColour.withAlpha (0.08f), art.getBottomRight(), false));
            g.fillRect (art);
            icons::draw (g, *icon, art, iconColour, 44.0f, 1.6f);
            return;
        }

        g.setColour (colour::wellBg);
        g.fillRect (art);

        if (preview.isValid())
        {
            // The top-left of the tracks (names and the first bars) is the
            // recognizable part, so fill the tile from there and crop the rest.
            g.drawImage (preview, art, juce::RectanglePlacement::xLeft | juce::RectanglePlacement::yTop
                                         | juce::RectanglePlacement::fillDestination);
        }
        else
        {
            // Saved before previews existed.
            icons::draw (g, Icon::waveform, art, colour::textSecondary.withAlpha (0.6f), 40.0f, 1.6f);
        }
    }
};

//==============================================================================
// A page in the chooser's sidebar.
class SessionChooser::NavItem  : public juce::Button
{
public:
    NavItem (const juce::String& name, Icon i) : juce::Button (name), icon (i)
    {
        setTitle (name);
        setRadioGroupId (1);
        setClickingTogglesState (true);
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    void paintButton (juce::Graphics& g, bool over, bool) override
    {
        auto r = getLocalBounds().toFloat();
        const bool on = getToggleState();

        if (on || over)
        {
            g.setColour (on ? colour::selectedRow : colour::raised.withAlpha (0.5f));
            g.fillRoundedRectangle (r, 8.0f);
        }

        auto content = getLocalBounds().reduced (12, 0);
        icons::draw (g, icon, content.removeFromLeft (20).toFloat(), on ? colour::accent : colour::textSecondary, 18.0f, 1.8f);
        content.removeFromLeft (10);
        g.setColour (on ? colour::text : colour::textMenu);
        g.setFont (on ? fonts::uiSemiBold (14.0f) : fonts::ui (14.0f));
        g.drawText (getName(), content, juce::Justification::centredLeft);

        if (hasKeyboardFocus (false))
            IkarosLookAndFeel::drawFocusRing (g, r, 8.0f);
    }

private:
    Icon icon;
};

//==============================================================================
void SessionChooser::show (juce::Component& parent, AudioEngine& engine, Page page, Callbacks callbacks)
{
    // Owned by its parent from here on; it deletes itself in close().
    auto* chooser = new SessionChooser (engine, std::move (callbacks));
    parent.addAndMakeVisible (chooser);
    chooser->setBounds (parent.getLocalBounds());
    chooser->showPage (page);
    chooser->grabKeyboardFocus();
}

SessionChooser::SessionChooser (AudioEngine& e, Callbacks c)
    : engine (e), callbacks (std::move (c))
{
    setTitle ("Choose a session");
    setWantsKeyboardFocus (true);
    setAlwaysOnTop (true);

    newNav = std::make_unique<NavItem> ("New Session", Icon::plus);
    recentNav = std::make_unique<NavItem> ("Recent", Icon::clock);
    newNav->onClick = [this] { showPage (Page::newSession); };
    recentNav->onClick = [this] { showPage (Page::recent); };
    addAndMakeVisible (*newNav);
    addAndMakeVisible (*recentNav);

    openOtherButton.setColours (IconButton::ghostColours());
    openOtherButton.onClick = [this]
    {
        auto onOpenOther = callbacks.onOpenOther;
        close();

        if (onOpenOther)
            onOpenOther();
    };
    addAndMakeVisible (openOtherButton);

    // The grid scrolls when there are more tiles than fit.
    viewport.setViewedComponent (&tileHolder, false);
    viewport.setScrollBarsShown (true, false);
    addAndMakeVisible (viewport);

    for (int i = 0; i < (int) std::size (templates); ++i)
    {
        auto& t = templates[i];
        auto* tile = templateTiles.add (new Tile (t.name, t.description));
        tile->setTemplateArt (t.icon, t.colour);
        tile->onClick = [this, i] { select (i); };
        tile->onDoubleClick = [this, i] { select (i); choose(); };
        tileHolder.addChildComponent (tile);
    }

    recentSessions = engine.getRecentSessions();

    for (int i = 0; i < (int) recentSessions.size(); ++i)
    {
        auto& s = recentSessions[(size_t) i];
        auto* tile = recentTiles.add (new Tile (s.name, describeDate (s.modified)));
        tile->setTooltip (s.file.getFullPathName());

        // ImageCache keeps decoded previews, so reopening the chooser is quick.
        if (s.preview.existsAsFile())
            tile->setPreview (juce::ImageCache::getFromFile (s.preview));

        tile->onClick = [this, i] { select (i); };
        tile->onDoubleClick = [this, i] { select (i); choose(); };
        tileHolder.addChildComponent (tile);
    }

    // New session details: tempo, time signature and key, all changeable later.
    tempoField.setText ("120", false);
    tempoField.setInputRestrictions (3, "0123456789");
    tempoField.setJustification (juce::Justification::centred);
    tempoField.setFont (fonts::mono (14.0f));
    tempoField.setTitle ("Tempo");
    tempoField.setColour (juce::TextEditor::outlineColourId, colour::borderSoft);
    tempoField.onReturnKey = [this] { choose(); };
    addChildComponent (tempoField);

    const char* signatures[] { "4/4", "3/4", "6/8", "2/4", "5/4", "7/8", "12/8" };

    for (int i = 0; i < (int) std::size (signatures); ++i)
        timeSignatureBox.addItem (signatures[i], i + 1);

    timeSignatureBox.setSelectedId (1, juce::dontSendNotification);
    timeSignatureBox.setTitle ("Time signature");
    addChildComponent (timeSignatureBox);

    const char* roots[] { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };

    for (int i = 0; i < 12; ++i)
        keyRootBox.addItem (roots[i], i + 1);

    keyRootBox.setSelectedId (1, juce::dontSendNotification);
    keyRootBox.setTitle ("Key");
    addChildComponent (keyRootBox);

    keyModeBox.addItem ("Major", 1);
    keyModeBox.addItem ("Minor", 2);
    keyModeBox.setSelectedId (1, juce::dontSendNotification);
    keyModeBox.setTitle ("Major or minor");
    addChildComponent (keyModeBox);

    auto cancelColours = IconButton::raisedColours();
    cancelColours.border = colour::border;
    cancelButton.setColours (cancelColours);
    cancelButton.onClick = [this] { close(); };
    addAndMakeVisible (cancelButton);

    chooseButton.setColours (IconButton::accentColours());
    chooseButton.onClick = [this] { choose(); };
    addAndMakeVisible (chooseButton);
}

SessionChooser::~SessionChooser() = default;

//==============================================================================
juce::OwnedArray<SessionChooser::Tile>& SessionChooser::currentTiles()
{
    return page == Page::newSession ? templateTiles : recentTiles;
}

int& SessionChooser::currentSelection()
{
    return page == Page::newSession ? selectedTemplate : selectedRecent;
}

void SessionChooser::showPage (Page newPage)
{
    page = newPage;
    newNav->setToggleState (page == Page::newSession, juce::dontSendNotification);
    recentNav->setToggleState (page == Page::recent, juce::dontSendNotification);

    for (auto* tile : templateTiles)
        tile->setVisible (page == Page::newSession);

    for (auto* tile : recentTiles)
        tile->setVisible (page == Page::recent);

    for (juce::Component* c : { (juce::Component*) &tempoField, (juce::Component*) &timeSignatureBox,
                                (juce::Component*) &keyRootBox, (juce::Component*) &keyModeBox })
        c->setVisible (page == Page::newSession);

    chooseButton.setText (page == Page::newSession ? "Create" : "Open");
    chooseButton.setEnabled (! currentTiles().isEmpty());

    viewport.setViewPosition (0, 0);
    select (currentSelection());
    layoutTiles();
    repaint();
}

void SessionChooser::select (int index)
{
    auto& tiles = currentTiles();

    if (tiles.isEmpty())
        return;

    index = juce::jlimit (0, tiles.size() - 1, index);
    currentSelection() = index;

    for (int i = 0; i < tiles.size(); ++i)
        tiles[i]->setToggleState (i == index, juce::dontSendNotification);

    // Keep the selected tile in view.
    auto bounds = tiles[index]->getBounds();
    auto view = viewport.getViewArea();

    if (bounds.getY() < view.getY())
        viewport.setViewPosition (0, bounds.getY());
    else if (bounds.getBottom() > view.getBottom())
        viewport.setViewPosition (0, bounds.getBottom() - view.getHeight());
}

NewSessionSettings SessionChooser::getSettings() const
{
    NewSessionSettings s;
    s.kind = templates[juce::jlimit (0, (int) std::size (templates) - 1, selectedTemplate)].kind;
    s.bpm = juce::jlimit (40.0, 240.0, tempoField.getText().getDoubleValue() > 0.0 ? tempoField.getText().getDoubleValue() : 120.0);

    auto signature = juce::StringArray::fromTokens (timeSignatureBox.getText(), "/", {});
    s.numerator = signature[0].getIntValue();
    s.denominator = signature[1].getIntValue();

    s.keyRoot = keyRootBox.getSelectedId() - 1;
    s.minor = keyModeBox.getSelectedId() == 2;
    return s;
}

void SessionChooser::choose()
{
    // Copy what we need: close() deletes this chooser.
    if (page == Page::newSession)
    {
        auto onNew = callbacks.onNew;
        auto settings = getSettings();
        close();

        if (onNew)
            onNew (settings);
    }
    else if (juce::isPositiveAndBelow (selectedRecent, (int) recentSessions.size()))
    {
        auto onOpen = callbacks.onOpen;
        auto file = recentSessions[(size_t) selectedRecent].file;
        close();

        if (onOpen)
            onOpen (file);
    }
}

void SessionChooser::close()
{
    // Remove ourselves and delete on the next message-loop turn: we may be
    // inside one of our own button callbacks right now.
    setVisible (false);
    juce::Component::unfocusAllComponents();

    juce::MessageManager::callAsync ([safeThis = SafePointer<SessionChooser> (this)]
    {
        if (safeThis != nullptr)
            delete safeThis.getComponent();
    });
}

int SessionChooser::getColumns() const
{
    return juce::jmax (1, (viewport.getMaximumVisibleWidth() + tileGap) / (tileWidth + tileGap));
}

bool SessionChooser::keyPressed (const juce::KeyPress& key)
{
    const int columns = getColumns();

    if (key == juce::KeyPress::escapeKey)     { close(); return true; }
    if (key == juce::KeyPress::returnKey)     { choose(); return true; }
    if (key == juce::KeyPress::leftKey)       { select (currentSelection() - 1); return true; }
    if (key == juce::KeyPress::rightKey)      { select (currentSelection() + 1); return true; }
    if (key == juce::KeyPress::upKey)         { select (currentSelection() - columns); return true; }
    if (key == juce::KeyPress::downKey)       { select (currentSelection() + columns); return true; }
    return false;
}

//==============================================================================
void SessionChooser::paint (juce::Graphics& g)
{
    // Dim the app behind the chooser.
    g.fillAll (juce::Colours::black.withAlpha (0.6f));

    juce::DropShadow (juce::Colours::black.withAlpha (0.6f), 30, { 0, 8 }).drawForRectangle (g, panel);
    g.setColour (colour::toolbarBg);
    g.fillRoundedRectangle (panel.toFloat(), 14.0f);

    // Sidebar, with its own background, rounded on the left only.
    {
        juce::Graphics::ScopedSaveState save (g);
        juce::Path shape;
        shape.addRoundedRectangle (panel.toFloat(), 14.0f);
        g.reduceClipRegion (shape);
        g.setColour (colour::sidebarBg);
        g.fillRect (sidebar);
        g.setColour (colour::divider);
        g.fillRect (sidebar.getRight(), sidebar.getY(), 1, sidebar.getHeight());
        g.setColour (colour::borderSoft);
        g.fillRect (footer.getX(), footer.getY(), footer.getWidth(), 1);
    }

    g.setColour (colour::border);
    g.drawRoundedRectangle (panel.toFloat().reduced (0.5f), 14.0f, 1.0f);

    g.setColour (colour::text);
    g.setFont (fonts::uiBold (13.0f));
    g.drawText ("Ikaros Studio", sidebar.reduced (20, 0).withHeight (headerHeight).withY (sidebar.getY()),
                juce::Justification::centredLeft);

    auto title = header.reduced (24, 0);
    g.setFont (fonts::uiBold (20.0f));
    g.drawText (page == Page::newSession ? "Choose a template" : "Recent sessions", title, juce::Justification::centredLeft);

    if (page == Page::recent && recentSessions.empty())
    {
        g.setColour (colour::textSecondary);
        g.setFont (fonts::ui (14.0f));
        g.drawFittedText ("Sessions you save or open will show up here.\nUse \"Open Other...\" to find one on your computer.",
                          viewport.getBounds().withHeight (80), juce::Justification::centred, 3);
    }

    if (page == Page::newSession)
    {
        // Labels for the detail fields.
        g.setColour (colour::textSecondary);
        g.setFont (fonts::ui (13.0f));

        auto label = [&g] (const juce::String& text, juce::Component& field)
        {
            g.drawText (text, field.getBounds().withX (field.getX() - 60).withWidth (54), juce::Justification::centredRight);
        };

        label ("Tempo", tempoField);
        label ("Time", timeSignatureBox);
        label ("Key", keyRootBox);
        g.drawText ("bpm", tempoField.getBounds().translated (tempoField.getWidth() + 6, 0).withWidth (40),
                    juce::Justification::centredLeft);
    }
}

void SessionChooser::resized()
{
    panel = getLocalBounds().withSizeKeepingCentre (juce::jmin (1060, getWidth() - 64),
                                                    juce::jmin (700, getHeight() - 64));
    auto r = panel;
    sidebar = r.removeFromLeft (sidebarWidth);

    auto nav = sidebar.reduced (12, 0);
    nav.removeFromTop (headerHeight);
    newNav->setBounds (nav.removeFromTop (38));
    nav.removeFromTop (4);
    recentNav->setBounds (nav.removeFromTop (38));
    openOtherButton.setBounds (sidebar.reduced (12, 16).removeFromBottom (36));

    header = r.removeFromTop (headerHeight);
    footer = r.removeFromBottom (footerHeight);
    viewport.setBounds (r.reduced (24, 0).withTrimmedBottom (12));

    auto f = footer.reduced (24, 18);
    chooseButton.setBounds (f.removeFromRight (110));
    f.removeFromRight (10);
    cancelButton.setBounds (f.removeFromRight (100));

    // Tempo [120] bpm   Time [4/4]   Key [C] [Major]
    f.removeFromLeft (60);
    tempoField.setBounds (f.removeFromLeft (56));
    f.removeFromLeft (40 + 60);
    timeSignatureBox.setBounds (f.removeFromLeft (80));
    f.removeFromLeft (60);
    keyRootBox.setBounds (f.removeFromLeft (70));
    f.removeFromLeft (8);
    keyModeBox.setBounds (f.removeFromLeft (100));

    layoutTiles();
}

void SessionChooser::layoutTiles()
{
    auto& tiles = currentTiles();
    const int columns = getColumns();
    const int tileHeight = Tile::heightFor (tileWidth);

    for (int i = 0; i < tiles.size(); ++i)
        tiles[i]->setBounds ((i % columns) * (tileWidth + tileGap), (i / columns) * (tileHeight + tileGap),
                             tileWidth, tileHeight);

    const int rows = (tiles.size() + columns - 1) / columns;
    tileHolder.setSize (viewport.getMaximumVisibleWidth(), juce::jmax (0, rows * (tileHeight + tileGap) - tileGap));
}

} // namespace ikaros
