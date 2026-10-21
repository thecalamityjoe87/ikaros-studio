#include "ui/dialogs/AddPedalDialog.h"
#include "engine/PedalStyles.h"
#include "ui/bottompanel/PedalView.h"
#include "ui/theme/Fonts.h"
#include "ui/theme/IkarosLookAndFeel.h"
#include "ui/theme/Tokens.h"
#include "ui/widgets/CachedShadow.h"

namespace ikaros
{

using namespace tokens;

namespace
{
    // The pedals are shown at 70% of their size on the board.
    constexpr float previewScale = 0.7f;
    constexpr int tileWidth = (int) (PedalView::width * previewScale);     // 109
    constexpr int tileHeight = (int) (PedalView::height * previewScale);   // 151
    constexpr int tileMargin = 6;      // room around a pedal for its shadow and the hover/focus ring
    constexpr int columnGap = 6, rowGap = 8;   // between tiles (on top of their margins)
    constexpr int headingHeight = 30;
    constexpr int maxPanelHeight = 620;

    constexpr int padding = 28;        // inside the panel
    constexpr int titleHeight = 26, titleGap = 14;
    constexpr int footerHeight = 36, footerGap = 16;

    constexpr int cellWidth = tileWidth + tileMargin * 2;
    constexpr int cellHeight = tileHeight + tileMargin * 2;
}

//==============================================================================
// One pedal: its picture, as a button.
class AddPedalDialog::Tile  : public juce::Button
{
public:
    Tile (const PedalStyle& s, juce::Image image) : juce::Button (s.name), style (s), picture (std::move (image))
    {
        setTitle (style.name);
        setDescription (style.typeLabel);
        setTooltip (style.name + " (" + style.typeLabel + ")");
        setWantsKeyboardFocus (true);
        setMouseCursor (juce::MouseCursor::PointingHandCursor);

        // Keep what paintButton drew (the blurred shadow, the picture) in an
        // image, so scrolling the list just copies it instead of drawing
        // every pedal again. A hover or focus change draws it once more.
        setBufferedToImage (true);
    }

    const PedalStyle& style;   // a row of the static table, so it outlives us

    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        auto pedal = getLocalBounds().reduced (tileMargin).toFloat();

        // Hovered: the pedal lifts a little and gets an accent ring.
        if (over && ! down)
            pedal = pedal.translated (0.0f, -2.0f);

        // One cached shadow for each look, so hovering doesn't blur again.
        const juce::DropShadow shadow (juce::Colours::black.withAlpha (over ? 0.45f : 0.3f), over ? 8 : 5, { 0, over ? 4 : 2 });
        (over ? hoverShadow : restingShadow).drawForRoundedRectangle (g, shadow, pedal, size::radiusCard * previewScale * 2.0f);

        // The picture was drawn at the screen's scale, so this is pixel for
        // pixel. drawImage uses the current colour's opacity, which the
        // shadow left at 30%: back to fully opaque first.
        g.setOpacity (1.0f);
        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        g.drawImage (picture, pedal);

        if (over || down)
        {
            g.setColour (theme().accent);
            g.drawRoundedRectangle (pedal.expanded (2.5f), size::radiusCard * previewScale * 2.0f + 2.0f, 2.0f);
        }

        if (hasKeyboardFocus (false))
            IkarosLookAndFeel::drawFocusRing (g, pedal.expanded (2.0f), size::radiusCard);
    }

    bool keyPressed (const juce::KeyPress& key) override
    {
        if (key == juce::KeyPress::returnKey || key == juce::KeyPress::spaceKey)
        {
            triggerClick();
            return true;
        }

        return juce::Button::keyPressed (key);   // the arrow keys go on to the dialog
    }

    void focusGained (FocusChangeType) override
    {
        repaint();

        // Keep the focused pedal in view when the list scrolls.
        if (auto* viewport = findParentComponentOfClass<juce::Viewport>())
        {
            const auto area = getBoundsInParent();
            auto view = viewport->getViewArea();

            if (area.getX() < view.getX())              view.setX (area.getX());
            else if (area.getRight() > view.getRight()) view.setX (area.getRight() - view.getWidth());

            if (area.getY() < view.getY())                view.setY (area.getY());
            else if (area.getBottom() > view.getBottom()) view.setY (area.getBottom() - view.getHeight());

            viewport->setViewPosition (view.getPosition());
        }
    }

    void focusLost (FocusChangeType) override    { repaint(); }

private:
    juce::Image picture;
    CachedShadow restingShadow, hoverShadow;
};

//==============================================================================
void AddPedalDialog::show (juce::Component& parent, AudioEngine& engine, std::function<void (const juce::String&)> onChosen)
{
    // Owned by its parent from here on; it deletes itself in close().
    auto* dialog = new AddPedalDialog (engine, std::move (onChosen));

    // The picture of what's behind, before the dialog covers it, at the
    // screen's scale so it stays sharp.
    float screenScale = 1.0f;

    if (auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
        screenScale = (float) display->scale;

    dialog->backdropArea = parent.getLocalBounds();
    dialog->backdrop = parent.createComponentSnapshot (dialog->backdropArea, true, screenScale);

    {
        // Dimmed once here, not on every paint.
        juce::Graphics g (dialog->backdrop);
        g.fillAll (theme().scrim);
    }

    parent.addAndMakeVisible (dialog);
    dialog->setBounds (parent.getLocalBounds());

    if (! dialog->tiles.isEmpty())
        dialog->tiles.getFirst()->grabKeyboardFocus();
}

AddPedalDialog::AddPedalDialog (AudioEngine& engine, std::function<void (const juce::String&)> chosen)
    : onChosen (std::move (chosen))
{
    setTitle ("Add a pedal");
    setWantsKeyboardFocus (true);
    setAlwaysOnTop (true);

    // Draw the pictures at the screen's scale, so they're sharp on
    // high-resolution screens.
    float screenScale = 1.0f;

    if (auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
        screenScale = (float) display->scale;

    for (auto& style : getPedalStyles())
    {
        auto* tile = tiles.add (new Tile (style, PedalView::createPreview (engine, style, previewScale * screenScale)));
        const auto type = style.pluginType;
        tile->onClick = [this, type] { choose (type); };
        content.addAndMakeVisible (tile);
    }

    content.setOpaque (true);
    viewport.setViewedComponent (&content, false);   // false: we own `content`, not the viewport
    viewport.setScrollBarsShown (true, true);        // both ways, when the list doesn't fit
    addAndMakeVisible (viewport);

    cancelButton.setColours (IconButton::borderedColours);
    cancelButton.onClick = [this] { close(); };
    addAndMakeVisible (cancelButton);
}

AddPedalDialog::~AddPedalDialog() = default;

void AddPedalDialog::choose (const juce::String& type)
{
    auto callback = onChosen;
    close();

    if (callback)
        callback (type);
}

void AddPedalDialog::close()
{
    // Remove ourselves from the parent and delete on the next message-loop
    // turn: we may be inside one of our own button callbacks right now.
    setVisible (false);
    juce::Component::unfocusAllComponents();

    juce::MessageManager::callAsync ([safeThis = SafePointer<AddPedalDialog> (this)]
    {
        if (safeThis != nullptr)
            delete safeThis.getComponent();
    });
}

juce::Rectangle<int> AddPedalDialog::layOutTiles()
{
    // Each group is one row under its heading, as long as it needs: the
    // list scrolls sideways for the longest.
    content.headings.clear();
    int y = 0, column = 0, width = 0;
    juce::String group;

    for (auto* tile : tiles)
    {
        if (tile->style.group != group)
        {
            group = tile->style.group;

            if (column > 0)
                y += cellHeight + rowGap;

            column = 0;
            content.headings.push_back ({ group, 0, { 0, y, 0, headingHeight } });
            y += headingHeight;
        }

        tile->setBounds (column * (cellWidth + columnGap), y, cellWidth, cellHeight);
        width = juce::jmax (width, tile->getRight());
        ++content.headings.back().count;
        ++column;
    }

    for (auto& heading : content.headings)
        heading.area.setWidth (width);

    return { width, y + cellHeight };
}

void AddPedalDialog::resized()
{
    // Opaque while the picture covers the dialog. After the window is
    // resized it doesn't fit any more: the app shows through again (and
    // redraws behind the dialog, as it would without the picture).
    setOpaque (backdrop.isValid() && getBounds() == backdropArea);

    // Wide enough for the longest row and the up-and-down scroll bar, so
    // it only scrolls sideways in a narrow window; at most maxPanelHeight
    // tall, and the list scrolls up and down inside it.
    const auto listSize = layOutTiles();
    const auto available = getLocalBounds().reduced (24);
    const int wanted = listSize.getWidth() + viewport.getScrollBarThickness() + padding * 2;
    panel = getLocalBounds().withSizeKeepingCentre (juce::jmin (wanted, available.getWidth()),
                                                    juce::jmin (maxPanelHeight, available.getHeight()));

    auto r = panel.reduced (padding);
    r.removeFromTop (titleHeight + titleGap);
    auto footer = r.removeFromBottom (footerHeight);
    r.removeFromBottom (footerGap);

    viewport.setBounds (r);
    content.setSize (listSize.getWidth(), listSize.getHeight());
    cancelButton.setBounds (footer.removeFromRight (100));

    // Opaque: draw the dimmed app, the shadow and the panel into one
    // picture now, so a paint (e.g. under the scroll bar while the list
    // scrolls) is a single copy. The picture is at the screen's scale.
    if (isOpaque())
    {
        background = backdrop.createCopy();
        juce::Graphics g (background);
        g.addTransform (juce::AffineTransform::scale ((float) background.getWidth() / (float) juce::jmax (1, getWidth())));
        paintPanel (g);
    }
}

bool AddPedalDialog::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey)    { close(); return true; }
    if (key == juce::KeyPress::leftKey)      { moveFocus (-1, 0); return true; }
    if (key == juce::KeyPress::rightKey)     { moveFocus (1, 0); return true; }
    if (key == juce::KeyPress::upKey)        { moveFocus (0, -1); return true; }
    if (key == juce::KeyPress::downKey)      { moveFocus (0, 1); return true; }
    return false;
}

void AddPedalDialog::moveFocus (int dx, int dy)
{
    int current = 0;

    for (int i = 0; i < tiles.size(); ++i)
        if (tiles[i]->hasKeyboardFocus (false))
            current = i;

    int next = current;

    if (dx != 0)
    {
        // Left and Right step through the pedals in order, across groups.
        next = juce::jlimit (0, tiles.size() - 1, current + dx);
    }
    else
    {
        // Up and Down: the nearest pedal (left to right) on the next row up
        // or down, which may be in the next group.
        const auto from = tiles[current]->getBounds();
        int bestRowY = dy > 0 ? std::numeric_limits<int>::max() : std::numeric_limits<int>::min();

        for (auto* t : tiles)
        {
            const int y = t->getY();

            if ((dy > 0 && y > from.getY() && y < bestRowY) || (dy < 0 && y < from.getY() && y > bestRowY))
                bestRowY = y;
        }

        int bestDistance = std::numeric_limits<int>::max();

        for (int i = 0; i < tiles.size(); ++i)
        {
            if (tiles[i]->getY() != bestRowY)
                continue;

            const int distance = std::abs (tiles[i]->getX() - from.getX());

            if (distance < bestDistance)
            {
                bestDistance = distance;
                next = i;
            }
        }
    }

    tiles[next]->grabKeyboardFocus();
}

void AddPedalDialog::mouseDown (const juce::MouseEvent& e)
{
    if (! panel.contains (e.getPosition()))
        close();
}

void AddPedalDialog::paint (juce::Graphics& g)
{
    // Opaque: the picture of everything below (see resized).
    if (isOpaque())
    {
        g.drawImage (background, getLocalBounds().toFloat());
        return;
    }

    g.fillAll (theme().scrim);   // dim the app behind the dialog
    paintPanel (g);
}

void AddPedalDialog::paintPanel (juce::Graphics& g)
{
    theme().dialogShadow.drawForRectangle (g, panel);
    g.setColour (theme().dialogBg);
    g.fillRoundedRectangle (panel.toFloat(), size::radiusPill);
    g.setColour (theme().border);
    g.drawRoundedRectangle (panel.toFloat().reduced (0.5f), size::radiusPill, 1.0f);

    auto title = panel.reduced (padding).removeFromTop (titleHeight);
    g.setColour (theme().text);
    g.setFont (fonts::uiBold (textSize::title));
    g.drawText ("Add a pedal", title, juce::Justification::centredLeft);

    g.setColour (theme().textSecondary);
    g.setFont (fonts::ui (textSize::md));
    g.drawText (juce::String (tiles.size()) + " pedals", title, juce::Justification::centredRight);
}

void AddPedalDialog::Content::paint (juce::Graphics& g)
{
    // Opaque (see the constructor), in the panel's colour: scrolling then
    // only redraws the list, not the dialog and the picture behind it.
    g.fillAll (theme().dialogBg);

    // Each group's heading: its name in small capitals, and how many pedals.
    for (auto& heading : headings)
    {
        auto area = heading.area.withTrimmedLeft (tileMargin).withTrimmedTop (6);
        const auto name = heading.name.toUpperCase();
        const auto nameFont = fonts::uiBold (textSize::sm).withExtraKerningFactor (0.08f);

        g.setColour (theme().textSecondary);
        g.setFont (nameFont);
        g.drawText (name, area, juce::Justification::centredLeft);

        const int nameWidth = juce::roundToInt (juce::GlyphArrangement::getStringWidth (nameFont, name));
        g.setColour (theme().textSoft);
        g.setFont (fonts::ui (textSize::sm));
        g.drawText (juce::String (heading.count) + (heading.count == 1 ? " pedal" : " pedals"),
                    area.withTrimmedLeft (nameWidth + 8), juce::Justification::centredLeft);
    }
}

} // namespace ikaros
