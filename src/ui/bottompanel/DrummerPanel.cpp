#include "ui/bottompanel/DrummerPanel.h"
#include "engine/TrackController.h"
#include "ui/theme/Fonts.h"
#include "ui/theme/IkarosLookAndFeel.h"
#include "ui/theme/Theme.h"
#include "ui/theme/Tokens.h"

namespace ikaros
{

using namespace tokens;

namespace
{
    // (The sizes the minimum size depends on are in the header.)
    constexpr int labelHeight = 18;
    constexpr int pieceButtonHeight = 32;
    constexpr int keeperHeight = 30;
    constexpr int segmentInset = 3;

    juce::String percent (float v)     { return juce::String (juce::roundToInt (v * 100.0f)) + "%"; }
}

//==============================================================================
// A drummer in the list: its name and one-line description. A real button
// (Tab reaches it, Space picks it); the chosen one is highlighted.
class DrummerPanel::DrummerCard  : public juce::Button
{
public:
    explicit DrummerCard (const DrummerInfo& d)
        : juce::Button (d.name), info (d)
    {
        setTitle (d.name);
        setDescription (d.description);
        setTooltip (d.description);
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    const DrummerInfo info;
    Family family = Family::drums;

    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        auto r = getLocalBounds().toFloat().reduced (1.0f);
        const bool on = getToggleState();

        g.setColour (on ? theme().selectedRow : down ? theme().rowPressed : over ? theme().rowHover : theme().cardBg);
        g.fillRoundedRectangle (r, size::radius);
        g.setColour (on ? theme().selectedBorder : theme().borderSoft);
        g.drawRoundedRectangle (r, size::radius, on ? 2.0f : 1.0f);

        auto content = getLocalBounds().reduced (10, 4);
        auto iconArea = content.removeFromLeft (26).toFloat();
        icons::draw (g, Icon::drummer, iconArea, theme().family (family), 20.0f, 1.6f);
        content.removeFromLeft (8);

        // The name on top, the description under it (cut short if the card's small).
        const bool roomy = content.getHeight() >= 34;
        auto nameArea = roomy ? content.removeFromTop (content.getHeight() / 2) : content;

        g.setColour (theme().text);
        g.setFont (fonts::uiBold (textSize::md));
        g.drawText (info.name, nameArea, roomy ? juce::Justification::bottomLeft : juce::Justification::centredLeft, true);

        if (roomy)
        {
            g.setColour (theme().textSecondary);
            g.setFont (fonts::ui (textSize::xs));
            g.drawText (info.description, content, juce::Justification::topLeft, true);
        }

        if (hasKeyboardFocus (false))
            IkarosLookAndFeel::drawFocusRing (g, r, size::radius);
    }
};

//==============================================================================
// The XY pad: across is Simple to Complex, up is Soft to Loud. A faint grid
// and a puck to drag. Arrow keys move the puck (Shift for small steps).
class DrummerPanel::XYPad  : public juce::Component
{
public:
    XYPad()
    {
        setWantsKeyboardFocus (true);
        setTitle ("Complexity and loudness");
        setDescription ("Left and right: simple to complex. Up and down: soft to loud.");
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    Family family = Family::drums;

    // Dragging: started, moved (each position), ended. A key press: nudged.
    std::function<void()> onDragStarted, onDragEnded;
    std::function<void (float complexity, float loudness)> onDragged, onNudged;

    /** Where the puck is, unless it's being dragged (the mouse has it then). */
    void setPosition (float complexity, float loudness)
    {
        if (dragging || (complexity == x && loudness == y))
            return;

        x = complexity;
        y = loudness;
        repaint();
    }

    bool isDragging() const noexcept    { return dragging; }

    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat().reduced (1.0f);
        g.setColour (theme().wellBg);
        g.fillRoundedRectangle (r, size::radiusCard);
        g.setColour (theme().borderSoft);
        g.drawRoundedRectangle (r, size::radiusCard, 1.0f);

        // The faint grid: quarters both ways.
        const auto inner = area();
        g.setColour (theme().borderSoft.withMultipliedAlpha (0.6f));

        for (int i = 1; i < 4; ++i)
        {
            const auto gx = inner.getX() + inner.getWidth() * (float) i / 4.0f;
            const auto gy = inner.getY() + inner.getHeight() * (float) i / 4.0f;
            g.drawVerticalLine (juce::roundToInt (gx), inner.getY(), inner.getBottom());
            g.drawHorizontalLine (juce::roundToInt (gy), inner.getX(), inner.getRight());
        }

        // The four ends' names, in the middle of each edge.
        g.setColour (theme().textSecondary);
        g.setFont (fonts::uiSemiBold (textSize::xs));
        const auto labels = r.reduced (10.0f, 6.0f);
        g.drawText ("Loud", labels, juce::Justification::centredTop, false);
        g.drawText ("Soft", labels, juce::Justification::centredBottom, false);
        g.drawText ("Simple", labels, juce::Justification::centredLeft, false);
        g.drawText ("Complex", labels, juce::Justification::centredRight, false);

        // The puck.
        const auto centre = puckCentre();
        const auto puck = juce::Rectangle<float> (puckSize, puckSize).withCentre (centre);
        g.setColour (theme().family (family).withAlpha (0.25f));
        g.fillEllipse (puck.expanded (5.0f));
        g.setColour (theme().family (family));
        g.fillEllipse (puck);
        g.setColour (theme().textOnColour);
        g.drawEllipse (puck.reduced (1.0f), 2.0f);

        if (hasKeyboardFocus (false))
            IkarosLookAndFeel::drawFocusRing (g, r, size::radiusCard);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        dragging = true;

        if (onDragStarted)
            onDragStarted();

        moveTo (e.position);
    }

    void mouseDrag (const juce::MouseEvent& e) override    { moveTo (e.position); }

    void mouseUp (const juce::MouseEvent&) override
    {
        dragging = false;

        if (onDragEnded)
            onDragEnded();
    }

    bool keyPressed (const juce::KeyPress& key) override
    {
        const float step = key.getModifiers().isShiftDown() ? 0.01f : 0.05f;
        float dx = 0.0f, dy = 0.0f;

        if (key == juce::KeyPress::leftKey)        dx = -step;
        else if (key == juce::KeyPress::rightKey)  dx = step;
        else if (key == juce::KeyPress::upKey)     dy = step;
        else if (key == juce::KeyPress::downKey)   dy = -step;
        else return false;

        x = juce::jlimit (0.0f, 1.0f, x + dx);
        y = juce::jlimit (0.0f, 1.0f, y + dy);
        repaint();

        if (onNudged)
            onNudged (x, y);

        return true;
    }

    void focusGained (FocusChangeType) override     { repaint(); }
    void focusLost (FocusChangeType) override       { repaint(); }

private:
    static constexpr float puckSize = 18.0f;
    float x = 0.5f, y = 0.5f;
    bool dragging = false;

    // Where the puck's centre can go: inset so it never pokes out.
    juce::Rectangle<float> area() const     { return getLocalBounds().toFloat().reduced (puckSize * 0.5f + 6.0f); }

    juce::Point<float> puckCentre() const
    {
        const auto a = area();
        return { a.getX() + a.getWidth() * x, a.getBottom() - a.getHeight() * y };
    }

    void moveTo (juce::Point<float> p)
    {
        const auto a = area();
        x = juce::jlimit (0.0f, 1.0f, (p.x - a.getX()) / juce::jmax (1.0f, a.getWidth()));
        y = juce::jlimit (0.0f, 1.0f, (a.getBottom() - p.y) / juce::jmax (1.0f, a.getHeight()));
        repaint();

        if (onDragged)
            onDragged (x, y);
    }
};

//==============================================================================
DrummerPanel::DrummerPanel (AudioEngine& e) : engine (e)
{
    setTitle ("Drummer");

    for (auto& d : engine.getTracks().drummer().getDrummers())
    {
        auto* card = cards.add (new DrummerCard (d));
        card->onClick = [this, id = d.id]
        {
            auto s = settings;
            s.drummer = id;
            apply (s, "Choose Drummer");
        };
        addAndMakeVisible (card);
    }

    pad = std::make_unique<XYPad>();
    addAndMakeVisible (*pad);

    pad->onDragStarted = [this] { previewing = true; };
    pad->onDragged = [this] (float complexity, float loudness)
    {
        auto s = settings;
        s.complexity = complexity;
        s.loudness = loudness;
        preview (s);
    };
    pad->onDragEnded = [this] { finishPreview ("Complexity and Loudness"); };
    pad->onNudged = [this] (float complexity, float loudness)
    {
        auto s = settings;
        s.complexity = complexity;
        s.loudness = loudness;
        apply (s, "Complexity and Loudness");
    };

    // The pieces: toggles (raised, lit in the accent when on).
    const std::array<std::pair<IconButton*, DrumGroup>, 4> pieces {{
        { &kickSnareButton, DrumGroup::kickSnare }, { &hihatButton, DrumGroup::hihat },
        { &cymbalsButton, DrumGroup::cymbals }, { &tomsButton, DrumGroup::toms } }};

    for (auto [button, group] : pieces)
    {
        button->setIconSize (16.0f);
        button->setTooltip ("Play the " + button->getTitle().toLowerCase() + ", or leave them out");
        button->onClick = [this, g = group] { togglePieces (g); };
        addAndMakeVisible (*button);
    }

    // What keeps time: one of three segments.
    const std::array<std::pair<IconButton*, Timekeeping>, 3> keepers {{
        { &hihatKeeper, Timekeeping::hihat }, { &rideKeeper, Timekeeping::ride }, { &autoKeeper, Timekeeping::automatic } }};

    for (auto [button, keeper] : keepers)
    {
        button->setColours (IconButton::segmentColours);
        button->onClick = [this, k = keeper]
        {
            auto s = settings;
            s.timekeeping = k;
            apply (s, "Hi-hat or Ride");
        };
        addAndMakeVisible (*button);
    }

    autoKeeper.setTooltip ("The hi-hat, and the ride when it's loud");

    // Fills and Swing. A drag plays as it goes and is one undo step when
    // it ends; the mouse wheel and arrow keys make a step each.
    auto setUpKnob = [this] (Knob& knob, float DrummerSettings::* member, const juce::String& undoName)
    {
        knob.setArcFamily (family);
        addAndMakeVisible (knob);

        // `member` is a pointer to one of DrummerSettings' floats, so one
        // function serves both knobs: s.*member is that float in s.
        knob.onValueChange = [this, &knob, member, undoName] (float v)
        {
            auto s = settings;
            s.*member = v;

            if (knob.getSlider().isMouseButtonDown())
                preview (s);
            else
                apply (s, undoName);
        };
        knob.getSlider().onDragEnd = [this, undoName] { finishPreview (undoName); };
    };

    setUpKnob (fillsKnob, &DrummerSettings::fills, "Drummer Fills");
    setUpKnob (swingKnob, &DrummerSettings::swing, "Drummer Swing");
    fillsKnob.getSlider().setTooltip ("How often the drummer plays a fill, and how long");
    swingKnob.getSlider().setTooltip ("Swings the off-beats, up to a triplet feel");

    variationButton.setColours (IconButton::borderedColours);
    variationButton.setIconSize (15.0f);
    variationButton.setTooltip ("Play the same settings a different way");
    variationButton.onClick = [this]
    {
        if (region == 0)
            return;

        engine.getTracks().drummer().newVariation (region);

        if (auto s = engine.getTracks().drummer().getSettings (region))
            settings = *s;

        updateControls();
    };
    addAndMakeVisible (variationButton);

    updateControls();
}

DrummerPanel::~DrummerPanel()
{
    // A drag still going (the panel closing under the mouse): keep what it did.
    if (previewing)
        engine.getTracks().drummer().finishPreview ("Complexity and Loudness");
}

void DrummerPanel::setRegion (std::optional<RegionInfo> r, Family f)
{
    const RegionId id = r.has_value() && r->isDrummer ? r->id : 0;

    if (previewing && id != region)
        finishPreview ("Drummer Settings");

    region = id;

    if (family != f)
    {
        family = f;
        pad->family = f;
        fillsKnob.setArcFamily (f);
        swingKnob.setArcFamily (f);

        for (auto* card : cards)
            card->family = f;
    }

    if (region == 0)
        return;

    auto& drummer = engine.getTracks().drummer();

    // During a drag the drag is in charge; afterwards, back to the region's own.
    if (! previewing)
        if (auto s = drummer.getSettings (region))
            settings = *s;

    silentBars = drummer.getSilentBars (region);
    updateControls();
    repaint();
}

void DrummerPanel::apply (const DrummerSettings& s, const juce::String& undoName)
{
    if (region == 0)
        return;

    settings = s;
    engine.getTracks().drummer().setSettings (region, s, undoName);
    updateControls();
}

void DrummerPanel::preview (const DrummerSettings& s)
{
    if (region == 0)
        return;

    previewing = true;
    settings = s;
    engine.getTracks().drummer().previewSettings (region, s);
    updateControls();
}

void DrummerPanel::finishPreview (const juce::String& undoName)
{
    if (! previewing)
        return;

    previewing = false;
    auto& drummer = engine.getTracks().drummer();
    drummer.finishPreview (undoName);

    if (auto s = drummer.getSettings (region))
        settings = *s;

    updateControls();
}

void DrummerPanel::togglePieces (DrumGroup group)
{
    auto s = settings;

    switch (group)
    {
        case DrumGroup::kickSnare:  s.kickSnare = ! s.kickSnare; break;
        case DrumGroup::hihat:      s.hihat = ! s.hihat; break;
        case DrumGroup::cymbals:    s.cymbals = ! s.cymbals; break;
        case DrumGroup::toms:       s.toms = ! s.toms; break;
    }

    apply (s, "Drummer Pieces");
}

void DrummerPanel::updateControls()
{
    for (auto* card : cards)
        card->setToggleState (card->info.id == settings.drummer, juce::dontSendNotification);

    pad->setPosition (settings.complexity, settings.loudness);

    kickSnareButton.setToggleState (settings.kickSnare, juce::dontSendNotification);
    hihatButton.setToggleState (settings.hihat, juce::dontSendNotification);
    cymbalsButton.setToggleState (settings.cymbals, juce::dontSendNotification);
    tomsButton.setToggleState (settings.toms, juce::dontSendNotification);

    hihatKeeper.setToggleState (settings.timekeeping == Timekeeping::hihat, juce::dontSendNotification);
    rideKeeper.setToggleState (settings.timekeeping == Timekeeping::ride, juce::dontSendNotification);
    autoKeeper.setToggleState (settings.timekeeping == Timekeeping::automatic, juce::dontSendNotification);

    fillsKnob.setValue (settings.fills, percent (settings.fills));
    swingKnob.setValue (settings.swing, percent (settings.swing));
    variationButton.setEnabled (region != 0);
}

//==============================================================================
void DrummerPanel::paint (juce::Graphics& g)
{
    Knob::drawShadow (g, fillsKnob);
    Knob::drawShadow (g, swingKnob);

    g.setColour (theme().textSecondary);
    g.setFont (fonts::uiSemiBold (textSize::xs));
    g.drawText ("PIECES", piecesLabel, juce::Justification::centredLeft, false);
    g.drawText ("TIME-KEEPING", keeperLabel, juce::Justification::centredLeft, false);

    // The time-keeping switch's well, behind its segments.
    g.setColour (theme().segmentWell);
    g.fillRoundedRectangle (keeperArea.toFloat(), size::radius);
    g.setColour (theme().segmentWellBorder);
    g.drawRoundedRectangle (keeperArea.toFloat().reduced (0.5f), size::radius, 1.0f);

    for (auto* segment : { &hihatKeeper, &rideKeeper, &autoKeeper })
        IconButton::drawSegmentShadow (g, *segment);

    // Bars in another time signature are left out, for now.
    if (! silentBars.empty())
    {
        auto notice = noticeArea;
        icons::draw (g, Icon::warning, notice.removeFromLeft (18).toFloat(), theme().warning, 14.0f, 1.8f);
        notice.removeFromLeft (4);
        g.setColour (theme().textSecondary);
        g.setFont (fonts::ui (textSize::sm));
        g.drawText ("Drummer plays 4/4 for now: bars in other time signatures are silent.", notice,
                    juce::Justification::centredLeft, true);
    }
}

void DrummerPanel::resized()
{
    auto r = getLocalBounds().reduced (padding);

    // Left: the drummers (they share the height).
    cardsArea = r.removeFromLeft (cardsWidth);
    r.removeFromLeft (columnGap);
    const int count = juce::jmax (1, cards.size());
    const int cardHeight = juce::jlimit (minCardHeight, 52, (cardsArea.getHeight() - cardGap * (count - 1)) / count);

    for (int i = 0; i < cards.size(); ++i)
        cards[i]->setBounds (cardsArea.getX(), cardsArea.getY() + i * (cardHeight + cardGap), cardsArea.getWidth(), cardHeight);

    // Right: the pieces and time-keeping, then the knobs and New Variation,
    // always at their full width (the pad in the middle takes what's left).
    auto right = r.removeFromRight (rightWidth);
    r.removeFromRight (columnGap);

    auto controls = right.removeFromLeft (controlsWidth);
    right.removeFromLeft (columnGap);

    piecesLabel = controls.removeFromTop (labelHeight);
    const int buttonWidth = (controls.getWidth() - gap) / 2;

    for (int row = 0; row < 2; ++row)
    {
        auto line = controls.removeFromTop (pieceButtonHeight);
        (row == 0 ? kickSnareButton : cymbalsButton).setBounds (line.removeFromLeft (buttonWidth));
        line.removeFromLeft (gap);
        (row == 0 ? hihatButton : tomsButton).setBounds (line);
        controls.removeFromTop (gap);
    }

    controls.removeFromTop (6);
    keeperLabel = controls.removeFromTop (labelHeight);
    keeperArea = controls.removeFromTop (keeperHeight);
    auto segments = keeperArea.reduced (segmentInset);
    const int segmentWidth = segments.getWidth() / 3;
    hihatKeeper.setBounds (segments.removeFromLeft (segmentWidth));
    rideKeeper.setBounds (segments.removeFromLeft (segmentWidth));
    autoKeeper.setBounds (segments);

    const int knobHeight = juce::jmin (Knob::preferredHeight, right.getHeight() - variationHeight - gap);
    auto knobs = right.removeFromTop (knobHeight);
    fillsKnob.setBounds (knobs.removeFromLeft (Knob::preferredWidth));
    knobs.removeFromLeft (gap);
    swingKnob.setBounds (knobs.removeFromLeft (Knob::preferredWidth));
    right.removeFromTop (gap);
    variationButton.setBounds (right.removeFromTop (variationHeight).withWidth (Knob::preferredWidth * 2 + gap));

    // Middle: the XY pad, a little wider than tall, with the 4/4 notice under it.
    noticeArea = r.removeFromBottom (20);
    r.removeFromBottom (4);
    const int padWidth = juce::jmin (r.getWidth(), juce::roundToInt (r.getHeight() * 1.5f));
    pad->setBounds (r.withSizeKeepingCentre (padWidth, r.getHeight()));
    noticeArea = noticeArea.withX (pad->getX()).withWidth (juce::jmax (padWidth, noticeArea.getWidth()));
}

} // namespace ikaros
