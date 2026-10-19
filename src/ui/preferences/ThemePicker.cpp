#include "ui/preferences/ThemePicker.h"
#include "ui/theme/Fonts.h"
#include "ui/theme/IkarosLookAndFeel.h"
#include "ui/theme/Tokens.h"

namespace ikaros
{

using namespace tokens;

namespace
{
    constexpr int cardGap = 12, cardPadding = 8, labelHeight = 22;

    /** A tiny picture of the main window in a palette: the title strip, the
        library sidebar, and the dark arrangement with a few regions on it.
        Drawn from the palette passed in, not theme(), so each card shows its
        own theme whichever one is active. */
    void drawPreview (juce::Graphics& g, juce::Rectangle<float> area, const ThemePalette& p)
    {
        auto r = area;

        g.setColour (p.titleBarBg);
        g.fillRect (r.removeFromTop (r.getHeight() * 0.14f));

        auto sidebar = r.removeFromLeft (r.getWidth() * 0.28f);
        g.setColour (p.librarySidebarBg);
        g.fillRect (sidebar);

        // A few rows standing for the library's tiles and sounds.
        g.setColour (p.tileBorder);
        auto rows = sidebar.reduced (sidebar.getWidth() * 0.15f, sidebar.getHeight() * 0.12f);

        for (int i = 0; i < 4; ++i)
            g.fillRoundedRectangle (rows.removeFromTop (rows.getHeight() / (float) (4 - i)).withHeight (3.0f), 1.5f);

        // The arrangement: dark in both themes, with three regions.
        g.setColour (p.arrangement.laneBg);
        g.fillRect (r);

        using F = Family;
        const struct { F family; float start, length; } regions[] { { F::drums, 0.06f, 0.62f },
                                                                    { F::audio, 0.30f, 0.60f },
                                                                    { F::strings, 0.14f, 0.44f } };
        auto lanes = r.reduced (0.0f, r.getHeight() * 0.1f);
        const float laneHeight = lanes.getHeight() / 3.0f;

        for (auto& region : regions)
        {
            auto lane = lanes.removeFromTop (laneHeight).reduced (0.0f, laneHeight * 0.18f);
            auto bar = lane.withX (r.getX() + r.getWidth() * region.start).withWidth (r.getWidth() * region.length);
            g.setColour (p.family (region.family));
            g.fillRoundedRectangle (bar, 2.0f);
        }
    }
}

//==============================================================================
// One choice: the preview with its name under it.
class ThemePicker::Card  : public juce::Button
{
public:
    Card (const juce::String& name, int choiceIn) : juce::Button (name), choice (choiceIn)
    {
        setTitle (name + " theme");
        setClickingTogglesState (true);
        setRadioGroupId (1);   // choosing one card turns the others off
        setWantsKeyboardFocus (true);
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    const int choice;

    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        const auto r = getLocalBounds().toFloat().reduced (1.0f);
        const bool on = getToggleState();

        // Like the library's category tiles: the chosen one stands out.
        g.setColour (on ? theme().selectedBg : theme().tileBg.brighter (down ? 0.08f : (over ? 0.04f : 0.0f)));
        g.fillRoundedRectangle (r, size::radius);
        g.setColour (on ? theme().selectedBorder : theme().tileBorder);
        g.drawRoundedRectangle (r, size::radius, on ? 2.0f : 1.0f);

        auto content = r.reduced ((float) cardPadding);
        const auto label = content.removeFromBottom ((float) labelHeight);
        const auto preview = content;

        // Clip to the preview's rounded corners, then draw it. Match system
        // is split corner to corner: light above the diagonal, dark below.
        {
            juce::Graphics::ScopedSaveState save (g);
            juce::Path shape;
            shape.addRoundedRectangle (preview, size::radiusXs);
            g.reduceClipRegion (shape);

            if (choice == settings::ThemeChoice::light)
            {
                drawPreview (g, preview, lightPalette());
            }
            else if (choice == settings::ThemeChoice::dark)
            {
                drawPreview (g, preview, darkPalette());
            }
            else
            {
                drawPreview (g, preview, darkPalette());

                juce::Path lightHalf;
                lightHalf.addTriangle (preview.getTopLeft(), preview.getTopRight(), preview.getBottomLeft());
                g.reduceClipRegion (lightHalf);
                drawPreview (g, preview, lightPalette());
            }
        }

        g.setColour (theme().border);
        g.drawRoundedRectangle (preview, size::radiusXs, 1.0f);

        g.setColour (on ? theme().text : theme().textSecondary);
        g.setFont (on ? fonts::uiSemiBold (textSize::md) : fonts::ui (textSize::md));
        g.drawText (getName(), label, juce::Justification::centredBottom, false);

        if (hasKeyboardFocus (false))
            IkarosLookAndFeel::drawFocusRing (g, r, size::radius);
    }
};

//==============================================================================
ThemePicker::ThemePicker (Preferences& p) : preferences (p)
{
    setTitle ("Theme");
    setFocusContainerType (juce::Component::FocusContainerType::focusContainer);

    // In the order of the enum, so a card's index is its setting.
    cards.add (new Card ("Light", settings::ThemeChoice::light));
    cards.add (new Card ("Dark", settings::ThemeChoice::dark));
    cards.add (new Card ("Match system", settings::ThemeChoice::matchSystem));

    const int current = preferences.get (settings::themeChoice);

    for (auto* card : cards)
    {
        card->setToggleState (card->choice == current, juce::dontSendNotification);
        card->onClick = [this, card] { preferences.set (settings::themeChoice, card->choice); };
        addAndMakeVisible (card);
    }

    // A value the file holds but we don't offer: show Match system, the default.
    if (current < 0 || current >= cards.size())
        cards.getLast()->setToggleState (true, juce::dontSendNotification);

    // Last: setSize calls resized(), which needs the cards.
    setSize (100, preferredHeight);   // the page sets the width
}

ThemePicker::~ThemePicker() = default;

void ThemePicker::resized()
{
    if (cards.isEmpty())
        return;

    auto r = getLocalBounds();
    const int cardWidth = (r.getWidth() - cardGap * (cards.size() - 1)) / cards.size();

    for (auto* card : cards)
    {
        card->setBounds (r.removeFromLeft (cardWidth));
        r.removeFromLeft (cardGap);
    }
}

} // namespace ikaros
