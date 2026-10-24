#include "ui/bottompanel/PedalboardView.h"
#include "ui/widgets/PaintProbe.h"
#include "ui/bottompanel/AmpView.h"
#include "ui/dialogs/AddPedalDialog.h"
#include "ui/theme/Fonts.h"
#include "ui/theme/IkarosLookAndFeel.h"
#include "ui/theme/Theme.h"
#include "ui/theme/Tokens.h"
#include "ui/widgets/Icons.h"

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
    constexpr int verticalPadding = 14;    // the least room above and below the pedals
    constexpr int cableThickness = 4;

    bool isPedal (const PluginSlot& slot)    { return findPedalStyle (slot.type) != nullptr; }

    // Every pedal, grouped as in PedalStyles, for a pedal's Replace menu. `onChosen` is
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
// The dashed "+ Add pedal" slot after the last pedal, and "+ Add amp" where
// a guitar or bass track's Amp would be. A real button, so the keyboard
// reaches it too.
class PedalboardView::AddSlot  : public juce::Button
{
public:
    AddSlot (const juce::String& text, const juce::String& tooltip) : juce::Button (text)
    {
        setTooltip (tooltip);
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
        g.drawText (getName(), juce::Rectangle<float> (r.getWidth(), 20.0f).withCentre (centre.translated (0.0f, 14.0f)),
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
        addSlot.onClick = [this] { owner.showAddPedal (addSlot); };
        addChildComponent (addSlot);

        addAmpSlot.onClick = [this] { owner.engine.getTracks().effects().addAmp (trackId); };
        addChildComponent (addAmpSlot);
    }

    // `guitarOrBass`: the track has the guitar or bass icon, so it offers
    // an Amp when it has none.
    void refresh (TrackId newTrack, bool guitarOrBass)
    {
        auto& effects = owner.engine.getTracks().effects();
        const bool sameTrack = newTrack == trackId;
        trackId = newTrack;

        // Which pedals, in which order: the board (its cable and the
        // shadows) only needs redrawing when that changes, not on every knob
        // change (many a second while automation plays).
        const auto pedalIds = [this]
        {
            std::vector<PluginId> ids;

            for (auto& s : chain)
                ids.push_back (s.id);

            return ids;
        };
        const auto pedalsBefore = pedalIds();

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

        // The Amp, after the pedals (or the Add amp slot in its place).
        const bool hadAmp = amp != nullptr, hadAmpSlot = addAmpSlot.isVisible();
        const auto ampSettings = trackId != 0 ? effects.getAmp (trackId) : EffectsController::AmpSettings();

        if (ampSettings.exists)
        {
            if (amp == nullptr)
            {
                amp = std::make_unique<AmpView> (owner.engine);
                addAndMakeVisible (*amp);
            }

            amp->update (trackId, ampSettings);
        }
        else
        {
            amp.reset();   // (a deleted component leaves its parent by itself)
        }

        addSlot.setVisible (trackId != 0 && ! chainFull);
        addAmpSlot.setVisible (trackId != 0 && ! ampSettings.exists && guitarOrBass && ! chainFull);
        updateSize();
        layoutPedals();

        if (! sameTrack || pedalIds() != pedalsBefore || hadAmp != (amp != nullptr) || hadAmpSlot != addAmpSlot.isVisible())
            repaint();

        // A pedal was just added on this track: scroll it into view.
        if (sameTrack && added != nullptr)
            owner.viewport.setViewPosition (juce::jmax (0, added->getRight() + pedalGap + PedalView::width - owner.viewport.getMaximumVisibleWidth()),
                                            owner.viewport.getViewPositionY());
    }

    bool isFull() const noexcept    { return chainFull; }

    void updateSize()
    {
        const int items = pedals.size() + (addSlot.isVisible() ? 1 : 0);
        const int ampWidth = amp != nullptr || addAmpSlot.isVisible() ? AmpView::width + pedalGap : 0;
        const int idealWidth = sidePadding + inWidth + firstGap + items * (PedalView::width + pedalGap) + ampWidth + sidePadding;
        auto& viewport = owner.viewport;

        // The scrollbars take some of the viewport's room once the board
        // overflows it, so work the size out twice.
        for (int pass = 0; pass < 2; ++pass)
            setSize (juce::jmax (viewport.getMaximumVisibleWidth(), idealWidth),
                     juce::jmax (viewport.getMaximumVisibleHeight(), itemHeight + verticalPadding * 2));
    }

    void paint (juce::Graphics& g) override
    {
        const int cableY = pedalTop() + PedalView::height / 2;

        // One cable from the IN marker to the last item: the pedals are
        // drawn over it, so it only shows in the gaps between them.
        const int cableEnd = amp != nullptr            ? amp->getX()
                           : addAmpSlot.isVisible()    ? addAmpSlot.getX()
                           : addSlot.isVisible()       ? addSlot.getX()
                           : pedals.isEmpty()          ? inArea().getRight()
                                                       : pedals.getLast()->getX();
        // The dashed Add slots are see-through, so the cable mustn't run
        // across them: it's drawn in pieces, stopping at a slot's left edge
        // and carrying on from its right edge (the slots are in x order).
        g.setColour (theme().pedalboardCable);
        int cableStart = inArea().getRight();

        auto drawCable = [&] (int from, int to)
        {
            if (to > from)
                g.fillRoundedRectangle (juce::Rectangle<int> (from, cableY - cableThickness / 2, to - from, cableThickness).toFloat(),
                                        cableThickness * 0.5f);
        };

        for (auto* slot : { &addSlot, &addAmpSlot })
        {
            if (slot->isVisible() && slot->getX() < cableEnd)
            {
                drawCable (cableStart, slot->getX());
                cableStart = slot->getRight();
            }
        }

        drawCable (cableStart, cableEnd);

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
            PedalView::drawShadow (g, *pedal, pedalShadow);

        if (amp != nullptr)
            AmpView::drawShadow (g, *amp, ampShadow);
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
    CachedShadow pedalShadow;             // under every pedal (see PedalView::drawShadow)
    AddSlot addSlot { "Add pedal", "Add a pedal at the end of the board" };
    std::unique_ptr<AmpView> amp;         // the track's Amp, after the pedals (null: it has none)
    CachedShadow ampShadow;
    AddSlot addAmpSlot { "Add amp", "Add an amp after the pedals" };

    // Pedals and the Amp are the same height (they line up on the cable).
    static constexpr int itemHeight = juce::jmax (PedalView::height, AmpView::height);

    // Dragging a pedal sideways to reorder it.
    PedalView* dragged = nullptr;
    int dragStartX = 0;
    bool dragMoved = false;           // it has moved far enough to count as a drag
    int dropIndex = -1;               // where it would land, among the pedals

    int pedalTop() const    { return juce::jmax (verticalPadding, (getHeight() - itemHeight) / 2); }
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

        // The same size as a pedal: the space the next one will take.
        addSlot.setBounds (pedalX (pedals.size()), pedalTop(), PedalView::width, PedalView::height);

        // The Amp (or its slot) after the Add pedal slot.
        const auto ampArea = juce::Rectangle<int> (pedalX (pedals.size() + (addSlot.isVisible() ? 1 : 0)), pedalTop(),
                                                   AmpView::width, AmpView::height);
        addAmpSlot.setBounds (ampArea);

        if (amp != nullptr)
            amp->setBounds (ampArea);
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
        // Right-clicked: the menu opens where the mouse is, over the pedal.
        // From the keyboard (Shift+F10): beside the pedal.
        auto options = juce::PopupMenu::Options();
        menu.showMenuAsync (pedal.isMouseOver (true) ? options.withMousePosition() : options.withTargetComponent (&pedal));
    }
};

//==============================================================================
PedalboardView::PedalboardView (AudioEngine& e, Selection& s) : engine (e), selection (s)
{
    setTitle ("Pedalboard");

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
    const auto track = engine.getTracks().getTrack (selection.getTrack());
    trackId = track.has_value() && track->kind == TrackKind::audio ? track->id : 0;
    board->refresh (trackId, trackId != 0 && (track->icon == "guitar" || track->icon == "bass"));
}

bool PedalboardView::canAddPedal() const
{
    return trackId != 0 && ! board->isFull();
}

void PedalboardView::showAddPedal (juce::Component& from)
{
    if (! canAddPedal())
        return;

    // The dialog covers the window's content (its whole area, below the
    // title bar), as the New Track dialog does.
    juce::Component* parent = from.getTopLevelComponent();

    if (auto* window = dynamic_cast<juce::ResizableWindow*> (parent))
        if (auto* windowContent = window->getContentComponent())
            parent = windowContent;

    const auto id = trackId;
    auto& effects = engine.getTracks().effects();
    AddPedalDialog::show (*parent, engine, [&effects, id] (const juce::String& type) { effects.addEffect (id, type); });
}

void PedalboardView::paint (juce::Graphics& g)
{
    IKAROS_PERF_PAINT ("PedalboardView");
    g.fillAll (theme().pedalboardFloor);
}

void PedalboardView::resized()
{
    viewport.setBounds (getLocalBounds());
    board->updateSize();
}

} // namespace ikaros
