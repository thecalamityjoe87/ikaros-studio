#include "ui/library/LibrarySidebar.h"
#include "ui/theme/Fonts.h"
#include "ui/theme/IkarosLookAndFeel.h"
#include "ui/theme/Tokens.h"

namespace ikaros
{

using namespace tokens;

namespace
{
    struct Preset { const char* name; const char* meta; };

    struct Category
    {
        const char* name;
        juce::uint32 colour;
        bool available;                  // false = not wired to the engine yet
        std::vector<Preset> presets;
    };

    // Placeholder library content, matching the design mockup.
    const std::vector<Category>& library()
    {
        static const std::vector<Category> cats {
            { "Keyboards",   0xff5aa9ff, true,  { { "Grand Piano", "" }, { "Bright Upright", "" }, { "Electric Piano", "" }, { "Vintage Organ", "" }, { "Soft Keys Pad", "" } } },
            { "Drums",       0xffff8f5a, true,  { { "Studio Kit", "" }, { "Vintage Kit", "" }, { "Electronic 808", "" }, { "Brush Kit", "" }, { "Hand Percussion", "" } } },
            { "Bass",        0xff4fd18b, true,  { { "Synth Bass", "" }, { "Picked Bass", "" }, { "Upright Bass", "" }, { "Sub Bass", "" } } },
            { "Synths",      0xffc78bff, true,  { { "Basic Synth", "" }, { "Warm Pad", "" }, { "Bright Lead", "" }, { "Glass Pluck", "" }, { "Arp Sequence", "" } } },
            { "Guitar",      0xffffd25a, true,  { { "Clean Electric", "" }, { "Crunch Amp", "" }, { "Acoustic Steel", "" } } },
            { "Strings",     0xffff7aa8, true,  { { "Chamber Strings", "" }, { "Cello Section", "" }, { "Pizzicato", "" } } },
            // TODO(milestone-3): loops and audio files need the sample library + audio regions.
            { "Loops",       0xffffb547, false, { { "Acoustic Strum", "120 \xc2\xb7 C" }, { "Funk Drums", "100" }, { "Lo-fi Keys", "85 \xc2\xb7 Am" }, { "Deep House Bass", "124 \xc2\xb7 F" } } },
            { "Audio Files", 0xffa3abb8, false, { } },
        };
        return cats;
    }

    constexpr int rowHeight = 38;
}

//==============================================================================
class LibrarySidebar::CategoryTile  : public juce::Button
{
public:
    CategoryTile (const juce::String& n, juce::Colour c)
        : juce::Button (n), name (n), iconColour (c)
    {
        setTitle (n + " sounds");
        setMouseClickGrabsKeyboardFocus (false);
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        auto r = getLocalBounds().toFloat().reduced (0.5f);
        const bool on = getToggleState();

        g.setColour (on ? colour::selectedRow : juce::Colour (0xff1c1f25).brighter (down ? 0.08f : (over ? 0.04f : 0.0f)));
        g.fillRoundedRectangle (r, size::radiusLarge);
        g.setColour (on ? iconColour : colour::borderSoft);
        g.drawRoundedRectangle (r, size::radiusLarge, 1.0f);

        auto content = getLocalBounds().reduced (10);
        icons::draw (g, icons::forCategory (name), content.removeFromTop (20).removeFromLeft (20).toFloat(), iconColour, 20.0f);
        content.removeFromTop (6);

        g.setColour (colour::text);
        g.setFont (fonts::uiSemiBold (13.0f));
        g.drawText (name, content, juce::Justification::topLeft, true);

        if (hasKeyboardFocus (false))
            IkarosLookAndFeel::drawFocusRing (g, r, size::radiusLarge);
    }

    const juce::String name;

private:
    juce::Colour iconColour;
};

//==============================================================================
class LibrarySidebar::PresetRow  : public juce::Button
{
public:
    PresetRow (const juce::String& cat, const juce::String& n, const juce::String& m, bool avail)
        : juce::Button (n), category (cat), name (n), meta (m), available (avail)
    {
        setTitle (n);
        setMouseClickGrabsKeyboardFocus (false);
        setTooltip (available ? "Use " + n + " on the selected track"
                              : juce::String ("Coming in a later version"));
        setMouseCursor (available ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
    }

    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        auto r = getLocalBounds().toFloat();
        const bool active = getToggleState();

        if (active)
            g.setColour (colour::accent.withAlpha (0.14f));
        else
            g.setColour (over && available ? colour::raised.withAlpha (down ? 1.0f : 0.6f) : juce::Colours::transparentBlack);

        g.fillRoundedRectangle (r, size::radius);

        auto content = getLocalBounds().reduced (10, 0);
        auto dot = content.removeFromLeft (6).toFloat().withSizeKeepingCentre (6.0f, 6.0f);
        g.setColour (active ? colour::accent : colour::borderStrong);
        g.fillEllipse (dot);
        content.removeFromLeft (8);

        auto metaText = active ? juce::String ("On track") : meta;
        g.setFont (fonts::ui (12.0f));
        g.setColour (active ? colour::accentLight : colour::textSecondary);
        g.drawText (metaText, content, juce::Justification::centredRight);

        g.setFont (fonts::ui (14.0f));
        g.setColour (available ? colour::text : colour::textSecondary);
        g.drawText (name, content, juce::Justification::centredLeft, true);

        if (hasKeyboardFocus (false))
            IkarosLookAndFeel::drawFocusRing (g, r, size::radius);
    }

    const juce::String category, name, meta;
    const bool available;
};

//==============================================================================
LibrarySidebar::LibrarySidebar (AudioEngine& e, Selection& s)
    : engine (e), selection (s)
{
    setTitle ("Sound library");
    setFocusContainerType (juce::Component::FocusContainerType::focusContainer);

    searchField.setTextToShowWhenEmpty ("Search sounds and loops", colour::textSecondary);
    searchField.setFont (fonts::ui (14.0f));
    searchField.setTitle ("Search sounds");
    searchField.setIndents (32, 9);
    searchField.setColour (juce::TextEditor::outlineColourId, colour::borderSoft);
    searchField.onTextChange = [this] { rebuildPresetList(); };
    searchField.onEscapeKey = [this] { searchField.clear(); rebuildPresetList(); };
    addAndMakeVisible (searchField);

    for (auto& c : library())
    {
        auto* tile = tiles.add (new CategoryTile (c.name, juce::Colour (c.colour)));
        tile->onClick = [this, n = juce::String (c.name)] { selectCategory (n); };
        addAndMakeVisible (tile);
    }

    presetViewport.setViewedComponent (&presetList, false);   // false = we own presetList, not the viewport
    presetViewport.setScrollBarsShown (true, false);
    addAndMakeVisible (presetViewport);

    selectCategory (selectedCategory);
}

LibrarySidebar::~LibrarySidebar() = default;

void LibrarySidebar::selectCategory (const juce::String& name)
{
    selectedCategory = name;

    for (auto* t : tiles)
        t->setToggleState (t->name == name, juce::dontSendNotification);

    searchField.clear();
    rebuildPresetList();
}

void LibrarySidebar::rebuildPresetList()
{
    presetRows.clear();

    auto query = searchField.getText().trim();
    heading = query.isEmpty() ? selectedCategory.toUpperCase() : juce::String ("RESULTS");

    for (auto& c : library())
    {
        if (query.isEmpty() && selectedCategory != c.name)
            continue;

        for (auto& p : c.presets)
        {
            juce::String name (juce::CharPointer_UTF8 (p.name));

            if (query.isNotEmpty() && ! name.containsIgnoreCase (query))
                continue;

            auto* row = presetRows.add (new PresetRow (c.name, name, juce::CharPointer_UTF8 (p.meta), c.available));

            row->onClick = [this, row]
            {
                if (row->available && selection.getTrack() != 0)
                    engine.getTracks().setSound (selection.getTrack(), row->category, row->name);
            };

            presetList.addAndMakeVisible (row);
        }
    }

    refresh();
    layoutPresetList();
    repaint (headingArea);
}

void LibrarySidebar::refresh()
{
    auto track = engine.getTracks().getTrack (selection.getTrack());

    for (auto* row : presetRows)
        row->setToggleState (track.has_value() && track->soundName == row->name && track->category == row->category,
                             juce::dontSendNotification);
}

void LibrarySidebar::layoutPresetList()
{
    auto w = presetViewport.getMaximumVisibleWidth();
    presetList.setSize (w, juce::jmax (1, presetRows.size() * (rowHeight + 2)));

    int y = 0;

    for (auto* row : presetRows)
    {
        row->setBounds (0, y, w, rowHeight);
        y += rowHeight + 2;
    }
}

void LibrarySidebar::paint (juce::Graphics& g)
{
    g.fillAll (colour::sidebarBg);
    g.setColour (colour::divider);
    g.fillRect (getLocalBounds().removeFromRight (1));

    // Magnifier inside the search field (the TextEditor's left indent leaves room).
    icons::draw (g, Icon::search, searchField.getBounds().removeFromLeft (32).toFloat(), colour::textSecondary, 16.0f, 2.0f);

    g.setColour (colour::textSecondary);
    g.setFont (fonts::uiBold (11.0f).withExtraKerningFactor (0.1f));
    g.drawText (heading, headingArea, juce::Justification::centredLeft);

    if (presetRows.isEmpty())
    {
        g.setFont (fonts::ui (13.0f));
        g.drawText (selectedCategory == "Audio Files" ? "Recording and audio import come later." : "No matches",
                    presetViewport.getBounds().removeFromTop (rowHeight).withTrimmedLeft (2),
                    juce::Justification::centredLeft);
    }
}

void LibrarySidebar::resized()
{
    auto r = getLocalBounds().withTrimmedRight (1).reduced (12);

    searchField.setBounds (r.removeFromTop (36));
    r.removeFromTop (12);

    const int tileH = 62, gap = 6;
    const int tileW = (r.getWidth() - gap) / 2;

    for (int i = 0; i < tiles.size(); ++i)
    {
        int row = i / 2, col = i % 2;
        tiles[i]->setBounds (r.getX() + col * (tileW + gap), r.getY() + row * (tileH + gap), tileW, tileH);
    }

    r.removeFromTop (((tiles.size() + 1) / 2) * (tileH + gap) + 6);
    headingArea = r.removeFromTop (20).withTrimmedLeft (2);
    r.removeFromTop (4);
    presetViewport.setBounds (r);
    layoutPresetList();
}

} // namespace ikaros
