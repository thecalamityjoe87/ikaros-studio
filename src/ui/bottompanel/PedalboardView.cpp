#include "ui/bottompanel/PedalboardView.h"
#include "engine/Tuner.h"
#include "ui/theme/Fonts.h"
#include "ui/theme/IkarosLookAndFeel.h"
#include "ui/theme/Theme.h"
#include "ui/theme/Tokens.h"
#include "ui/theme/TrackColours.h"

namespace ikaros
{

using namespace tokens;

namespace
{
    // The floor's layout, in pixels.
    constexpr int sidePadding = 24;        // left of the IN marker, right of the last item
    constexpr int inWidth = 36, inHeight = 22;
    constexpr int firstGap = 30;           // from the IN marker to the first pedal
    constexpr int pedalGap = 30;           // between pedals (the cable shows in it)
    constexpr int addSlotWidth = 116;
    constexpr int verticalPadding = 14;    // the least room above and below the pedals
    constexpr int cableThickness = 4;

    bool isPedal (const PluginSlot& slot)    { return findPedalStyle (slot.type) != nullptr; }

    // Every pedal, grouped as the Add Pedal menu shows them. `onChosen` is
    // called with the chosen pedal's plugin type; `ticked` gets a tick.
    juce::PopupMenu buildPedalMenu (std::function<void (const juce::String& type)> onChosen, const juce::String& ticked = {})
    {
        juce::PopupMenu menu;
        juce::String group;

        for (auto& style : getPedalStyles())
        {
            if (style.group != group)
            {
                group = style.group;
                menu.addSectionHeader (group);
            }

            menu.addItem (style.name, true, style.pluginType == ticked,
                          [onChosen, type = style.pluginType] { onChosen (type); });
        }

        return menu;
    }
}

//==============================================================================
// The dashed "+ Add pedal" slot after the last pedal. A real button, so the
// keyboard reaches it too.
class PedalboardView::AddSlot  : public juce::Button
{
public:
    AddSlot() : juce::Button ("Add pedal")
    {
        setTooltip ("Add a pedal at the end of the board");
        setMouseClickGrabsKeyboardFocus (false);
    }

    void paintButton (juce::Graphics& g, bool highlighted, bool down) override
    {
        auto r = getLocalBounds().toFloat().reduced (1.0f);

        if (highlighted || down)
        {
            g.setColour (theme().tintDark.withAlpha (down ? 0.08f : 0.04f));
            g.fillRoundedRectangle (r, size::radiusCard);
        }

        // A dashed outline: createDashedStroke turns the rounded rectangle's
        // outline into dash-shaped pieces, which are then filled.
        juce::Path outline, dashed;
        outline.addRoundedRectangle (r, size::radiusCard);
        const float dashes[] = { 6.0f, 4.0f };
        juce::PathStrokeType (1.5f).createDashedStroke (dashed, outline, dashes, 2);
        g.setColour (theme().outline);
        g.fillPath (dashed);

        auto centre = r.getCentre();
        icons::draw (g, Icon::plus, juce::Rectangle<float> (24.0f, 24.0f).withCentre (centre.translated (0.0f, -14.0f)),
                     theme().textSoft, 20.0f);
        g.setColour (theme().textSoft);
        g.setFont (fonts::uiSemiBold (textSize::md));
        g.drawText ("Add pedal", juce::Rectangle<float> (r.getWidth(), 20.0f).withCentre (centre.translated (0.0f, 14.0f)),
                    juce::Justification::centred);

        if (hasKeyboardFocus (false))
            IkarosLookAndFeel::drawFocusRing (g, getLocalBounds().toFloat(), size::radiusCard);
    }
};

//==============================================================================
// What's on the floor, inside the viewport: the IN marker, the pedals and
// the cables between them, and the Add slot. It's as wide as the pedals
// need (the viewport scrolls it) and at least as big as the viewport.
class PedalboardView::Board  : public juce::Component
{
public:
    explicit Board (PedalboardView& o) : owner (o)
    {
        addSlot.onClick = [this] { owner.showAddMenu (addSlot); };
        addChildComponent (addSlot);
    }

    void refresh (TrackId newTrack)
    {
        auto& effects = owner.engine.getTracks().effects();
        const bool sameTrack = newTrack == trackId;
        trackId = newTrack;
        chain = trackId != 0 ? effects.getEffectChain (trackId) : std::vector<PluginSlot>();
        chainFull = (int) chain.size() >= EffectsController::maxEffectsPerTrack;

        // Keep the PedalView of every pedal that's still there (so a knob
        // being turned, or keyboard focus, isn't lost), and make new ones.
        // OwnedArray::removeAndReturn hands a pedal over without deleting
        // it; whatever's left in `old` is deleted at the end of this
        // function (and a deleted component leaves its parent by itself).
        juce::OwnedArray<PedalView> old;
        old.swapWith (pedals);
        PedalView* added = nullptr;

        for (auto& slot : chain)
        {
            auto* style = findPedalStyle (slot.type);

            if (style == nullptr)
                continue;

            PedalView* view = nullptr;

            for (int i = 0; i < old.size(); ++i)
                if (old[i]->getPluginId() == slot.id)
                    view = old.removeAndReturn (i);

            if (view == nullptr)
            {
                view = makePedal (*style);
                added = view;
            }

            pedals.add (view);
            view->update (trackId, slot, effects.getEffectParameters (trackId, slot.id));
        }

        if (dragged != nullptr && ! pedals.contains (dragged))
            endDrag();

        addSlot.setVisible (trackId != 0 && ! chainFull);
        updateSize();
        layoutPedals();
        repaint();

        // A pedal was just added on this track: scroll it into view.
        if (sameTrack && added != nullptr)
            owner.viewport.setViewPosition (juce::jmax (0, added->getRight() + pedalGap + addSlotWidth - owner.viewport.getMaximumVisibleWidth()),
                                            owner.viewport.getViewPositionY());
    }

    bool isFull() const noexcept    { return chainFull; }

    void updateSize()
    {
        const int items = pedals.size() + (addSlot.isVisible() ? 1 : 0);
        const int idealWidth = sidePadding + inWidth + firstGap + items * (PedalView::width + pedalGap) + sidePadding;
        auto& viewport = owner.viewport;

        // The scrollbars take some of the viewport's room once the board
        // overflows it, so work the size out twice.
        for (int pass = 0; pass < 2; ++pass)
            setSize (juce::jmax (viewport.getMaximumVisibleWidth(), idealWidth),
                     juce::jmax (viewport.getMaximumVisibleHeight(), PedalView::height + verticalPadding * 2));
    }

    void paint (juce::Graphics& g) override
    {
        const int cableY = pedalTop() + PedalView::height / 2;

        // One cable from the IN marker to the last item: the pedals are
        // drawn over it, so it only shows in the gaps between them.
        const int cableEnd = addSlot.isVisible() ? addSlot.getX()
                           : pedals.isEmpty()   ? inArea().getRight()
                                                : pedals.getLast()->getX();
        g.setColour (theme().pedalboardCable);
        g.fillRoundedRectangle (juce::Rectangle<int> (inArea().getRight(), cableY - cableThickness / 2,
                                                      juce::jmax (0, cableEnd - inArea().getRight()), cableThickness).toFloat(),
                                cableThickness * 0.5f);

        // IN: where the track's input comes in.
        const auto in = inArea().toFloat();
        g.setColour (theme().pedalboardFloor);
        g.fillRoundedRectangle (in, size::radiusXs + 2.0f);
        g.setColour (theme().borderStrong);
        g.drawRoundedRectangle (in.reduced (0.5f), size::radiusXs + 2.0f, 1.0f);
        g.setColour (theme().textSecondary);
        g.setFont (fonts::monoBold (textSize::sm));
        g.drawText ("IN", in, juce::Justification::centred);

        for (auto* pedal : pedals)
            PedalView::drawShadow (g, *pedal);
    }

    void resized() override
    {
        layoutPedals();
    }

private:
    PedalboardView& owner;
    TrackId trackId = 0;
    std::vector<PluginSlot> chain;    // the track's whole effect chain (pedals and the rest)
    bool chainFull = false;
    juce::OwnedArray<PedalView> pedals;   // in chain order
    AddSlot addSlot;

    // Dragging a pedal sideways to reorder it.
    PedalView* dragged = nullptr;
    int dragStartX = 0;
    bool dragMoved = false;           // it has moved far enough to count as a drag
    int dropIndex = -1;               // where it would land, among the pedals

    int pedalTop() const    { return juce::jmax (verticalPadding, (getHeight() - PedalView::height) / 2); }
    int pedalX (int slot) const    { return sidePadding + inWidth + firstGap + slot * (PedalView::width + pedalGap); }

    juce::Rectangle<int> inArea() const
    {
        return { sidePadding, pedalTop() + (PedalView::height - inHeight) / 2, inWidth, inHeight };
    }

    PedalView* makePedal (const PedalStyle& style)
    {
        auto* view = new PedalView (owner.engine, style);   // `pedals` owns it from here
        view->onBodyMouseDown = [this] (PedalView& p, const juce::MouseEvent&) { startDrag (p); };
        view->onBodyMouseDrag = [this] (PedalView& p, const juce::MouseEvent& e) { dragTo (p, e.getDistanceFromDragStartX()); };
        view->onBodyMouseUp   = [this] (PedalView&, const juce::MouseEvent&) { finishDrag(); };
        view->onMenuRequested = [this] (PedalView& p) { showMenu (p); };
        view->onMoveRequested = [this] (PedalView& p, int direction) { move (p.getPluginId(), pedals.indexOf (&p) + direction); };
        view->onRemoveRequested = [this] (PedalView& p) { owner.engine.getTracks().effects().removeEffect (trackId, p.getPluginId()); };
        view->setMouseClickGrabsKeyboardFocus (false);   // a click shouldn't leave a focus ring; Tab still reaches it
        addAndMakeVisible (view);
        return view;
    }

    void layoutPedals()
    {
        // While a pedal is dragged, the others make room where it would land.
        int slot = 0;

        for (auto* pedal : pedals)
        {
            if (pedal == dragged && dragMoved)
                continue;

            if (dragged != nullptr && dragMoved && slot == dropIndex)
                ++slot;

            pedal->setBounds (pedalX (slot++), pedalTop(), PedalView::width, PedalView::height);
        }

        if (dragged != nullptr && dragMoved)
            dragged->setTopLeftPosition (dragged->getX(), pedalTop());

        addSlot.setBounds (pedalX (pedals.size()), pedalTop(), addSlotWidth, PedalView::height);
    }

    //==============================================================================
    void startDrag (PedalView& pedal)
    {
        dragged = &pedal;
        dragStartX = pedal.getX();
        dragMoved = false;
        dropIndex = pedals.indexOf (&pedal);
    }

    void dragTo (PedalView& pedal, int distanceX)
    {
        if (dragged != &pedal || (! dragMoved && std::abs (distanceX) < 4))
            return;

        dragMoved = true;
        pedal.toFront (false);   // drawn over the others (false: without taking keyboard focus)
        pedal.setTopLeftPosition (juce::jlimit (0, getWidth() - PedalView::width, dragStartX + distanceX), pedalTop());

        // The slot nearest the pedal's left edge.
        const auto step = (float) (PedalView::width + pedalGap);
        const auto newDrop = juce::jlimit (0, pedals.size() - 1, juce::roundToInt ((float) (pedal.getX() - pedalX (0)) / step));

        if (newDrop != dropIndex)
        {
            dropIndex = newDrop;
            layoutPedals();
        }

        repaint();   // the shadows follow
    }

    void finishDrag()
    {
        auto* pedal = dragged;
        const bool moved = dragMoved;
        const int to = dropIndex;
        endDrag();

        if (pedal != nullptr && moved && to != pedals.indexOf (pedal))
            move (pedal->getPluginId(), to);   // the board re-lays itself out when the engine reports the move
    }

    void endDrag()
    {
        dragged = nullptr;
        dragMoved = false;
        dropIndex = -1;
        layoutPedals();
        repaint();
    }

    //==============================================================================
    // Moves a pedal to `newIndex` among the pedals. The engine counts
    // positions in the whole chain (pedals and other effects), so find the
    // chain position that puts it there.
    void move (PluginId id, int newIndex)
    {
        if (! juce::isPositiveAndBelow (newIndex, pedals.size()))
            return;

        int seen = 0, position = 0;

        for (auto& slot : chain)
        {
            if (slot.id == id)
                continue;

            if (isPedal (slot))
            {
                if (seen++ == newIndex)
                    break;
            }

            ++position;
        }

        // (Run out of pedals: `position` is just past the last effect, and
        // the engine keeps the pedal before any other effects.)
        owner.engine.getTracks().effects().moveEffect (trackId, id, position);
    }

    void showMenu (PedalView& pedal)
    {
        auto& effects = owner.engine.getTracks().effects();
        const auto id = pedal.getPluginId();
        const auto index = pedals.indexOf (&pedal);
        const auto track = trackId;

        // The menu's items run their lambdas after it closes; they capture
        // ids by value, never the PedalView (a refresh may have replaced it).
        juce::PopupMenu menu;
        menu.addItem ("Move Left", index > 0, false, [this, id, index] { move (id, index - 1); });
        menu.addItem ("Move Right", index < pedals.size() - 1, false, [this, id, index] { move (id, index + 1); });
        menu.addSeparator();
        menu.addSubMenu ("Replace", buildPedalMenu ([&effects, track, id] (const juce::String& type)
                                                    {
                                                        effects.replaceEffect (track, id, type);
                                                    }, pedal.getStyle().pluginType));
        menu.addItem ("Remove", [&effects, track, id] { effects.removeEffect (track, id); });
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&pedal));
    }
};

//==============================================================================
PedalboardView::PedalboardView (AudioEngine& e, Selection& s) : engine (e), selection (s)
{
    setTitle ("Pedalboard");

    for (auto* b : { &tunerButton, &addButton })
    {
        b->setColours (IconButton::borderedColours);
        b->setFont (fonts::uiSemiBold (textSize::md));
        b->setIconSize (16.0f);
        addAndMakeVisible (b);
    }

    tunerButton.setTooltip ("Tune up: the transport readout becomes a tuner for this track's input");
    tunerButton.onClick = [this] { engine.getTuner().setActive (! engine.getTuner().isActive()); };
    addButton.onClick = [this] { showAddMenu (addButton); };

    board = std::make_unique<Board> (*this);
    viewport.setViewedComponent (board.get(), false);   // false: we own the board, not the viewport
    viewport.setScrollBarsShown (true, true);   // scrollbars only when the pedals don't fit
    addAndMakeVisible (viewport);
}

PedalboardView::~PedalboardView()
{
    // The viewport mustn't point at the board once it's gone.
    viewport.setViewedComponent (nullptr, false);
}

void PedalboardView::refresh()
{
    track = engine.getTracks().getTrack (selection.getTrack());
    const bool audioTrack = track.has_value() && track->kind == TrackKind::audio;
    board->refresh (audioTrack ? track->id : 0);

    addButton.setEnabled (audioTrack && ! board->isFull());
    addButton.setTooltip (board->isFull() ? "This track has " + juce::String (EffectsController::maxEffectsPerTrack)
                                              + " effects, the most a track can have"
                                          : juce::String ("Add a pedal at the end of the board"));
    repaint (titleArea);
}

void PedalboardView::update()
{
    tunerButton.setToggleState (engine.getTuner().isActive(), juce::dontSendNotification);
}

void PedalboardView::showAddMenu (juce::Component& target)
{
    if (! track.has_value() || board->isFull())
        return;

    const auto id = track->id;
    auto& effects = engine.getTracks().effects();
    buildPedalMenu ([&effects, id] (const juce::String& type) { effects.addEffect (id, type); })
        .showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&target));
}

void PedalboardView::paint (juce::Graphics& g)
{
    auto r = getLocalBounds();
    auto header = r.removeFromTop (headerHeight);

    g.setColour (theme().bottomPanelBg);
    g.fillRect (header);
    g.setColour (theme().borderSoft);
    g.fillRect (header.removeFromBottom (1));
    g.setColour (theme().pedalboardFloor);
    g.fillRect (r);

    // The track: a swatch in its colour, its name, then "Pedalboard".
    if (! track.has_value())
        return;

    auto title = titleArea;
    g.setColour (colour::forTrack (*track));
    g.fillRoundedRectangle (title.removeFromLeft (14).withSizeKeepingCentre (14, 14).toFloat(), 3.5f);
    title.removeFromLeft (10);

    g.setColour (theme().text);
    g.setFont (fonts::uiBold (textSize::lg));
    const auto nameWidth = (int) std::ceil (juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), track->name));
    g.drawText (track->name, title.removeFromLeft (juce::jmin (nameWidth + 2, title.getWidth())), juce::Justification::centredLeft, true);
    title.removeFromLeft (10);
    g.setColour (theme().textSecondary);
    g.setFont (fonts::ui (textSize::md));
    g.drawText ("Pedalboard", title, juce::Justification::centredLeft, true);
}

void PedalboardView::resized()
{
    auto r = getLocalBounds();
    auto header = r.removeFromTop (headerHeight).withTrimmedBottom (1).reduced (size::panelPadding, 0);

    addButton.setBounds (header.removeFromRight (124).withSizeKeepingCentre (124, 32));
    header.removeFromRight (8);
    tunerButton.setBounds (header.removeFromRight (92).withSizeKeepingCentre (92, 32));
    header.removeFromRight (14);
    titleArea = header;

    viewport.setBounds (r);
    board->updateSize();
}

} // namespace ikaros
