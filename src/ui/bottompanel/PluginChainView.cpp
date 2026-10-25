#include "ui/bottompanel/PluginChainView.h"
#include "ui/widgets/PaintProbe.h"
#include "ui/theme/Fonts.h"
#include "ui/theme/IkarosLookAndFeel.h"
#include "ui/theme/Tokens.h"
#include "ui/theme/TrackColours.h"

namespace ikaros
{

using namespace tokens;

namespace
{
    constexpr int sidePadding = 12;
}

//==============================================================================
// One effect in the chain: [power] Name [⋯]. Drag it to reorder.
class PluginChainView::EffectRow  : public juce::Component,
                                    public juce::SettableTooltipClient   // adds setTooltip()
{
public:
    explicit EffectRow (PluginChainView& o) : owner (o)
    {
        setWantsKeyboardFocus (true);
        setMouseCursor (juce::MouseCursor::PointingHandCursor);

        power.setColours (IconButton::powerColours);
        power.setCornerRadius (6.0f);
        power.setIconSize (14.0f);
        power.onClick = [this]
        {
            owner.engine.getTracks().effects().setEffectEnabled (owner.trackId, slot.id, ! slot.enabled);
        };
        addAndMakeVisible (power);

        more.setColours (IconButton::ghostColours);
        more.setCornerRadius (6.0f);
        more.setIconSize (18.0f);
        more.onClick = [this] { owner.showRowMenu (index); };
        addAndMakeVisible (more);
    }

    void update (const PluginSlot& s, int i)
    {
        slot = s;
        index = i;
        power.setToggleState (s.enabled, juce::dontSendNotification);
        power.setTitle ((s.enabled ? "Turn off " : "Turn on ") + s.name);
        power.setTooltip (s.enabled ? "On (click to bypass)" : "Off (click to turn on)");
        more.setTitle (s.name + " options");
        more.setTooltip ("Replace or remove");
        setTitle (s.name + (s.disabledByUser ? " (disabled)" : s.missing ? " (not installed)" : (s.enabled ? "" : " (off)"))
                    + ", effect " + juce::String (i + 1));
        setTooltip (s.missing ? describeMissing (s)
                  : s.isThirdParty ? juce::String ("Click to open its window. Drag to reorder.")
                                   : juce::String ("Click to show its settings. Drag to reorder."));
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        const bool dragging = owner.dragFromIndex == index;
        const bool open = owner.openEffectId == slot.id;

        g.setColour (dragging || open ? theme().selectedRow : (isMouseOver (true) ? theme().raised : theme().cardBg));
        g.fillRoundedRectangle (r, size::radius);

        if (open)
        {
            // The effect whose settings are showing to the right.
            g.setColour (theme().family (owner.family));
            g.drawRoundedRectangle (r.reduced (0.5f), size::radius, 1.5f);
        }

        auto area = nameArea;

        if (slot.missing)
        {
            icons::draw (g, Icon::warning, area.removeFromRight (16).toFloat(), theme().warning, 14.0f);
            area.removeFromRight (4);
        }
        else if (slot.isThirdParty)
        {
            icons::draw (g, Icon::openWindow, area.removeFromRight (16).toFloat(), theme().textSecondary, 13.0f);
            area.removeFromRight (4);
        }

        g.setColour (slot.enabled && ! slot.missing ? theme().text : theme().textSecondary);
        g.setFont (fonts::ui (textSize::md));
        g.drawText (slot.name, area, juce::Justification::centredLeft, true);

        if (showFocusRing && hasKeyboardFocus (false))
            IkarosLookAndFeel::drawFocusRing (g, r, size::radius);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (5);
        power.setBounds (r.removeFromLeft (24));
        more.setBounds (r.removeFromRight (24));
        r.removeFromLeft (8);
        nameArea = r;
    }

    void mouseEnter (const juce::MouseEvent&) override   { repaint(); }
    void mouseExit (const juce::MouseEvent&) override    { repaint(); }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (e.getDistanceFromDragStart() > 4)
        {
            setMouseCursor (juce::MouseCursor::DraggingHandCursor);
            owner.dragRow (index, e.getEventRelativeTo (&owner).y);
        }
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        setMouseCursor (juce::MouseCursor::PointingHandCursor);

        if (e.mouseWasDraggedSinceMouseDown())
            owner.endDrag();
        else
            owner.openEditor (index);
    }

    bool keyPressed (const juce::KeyPress& key) override
    {
        auto& tracks = owner.engine.getTracks();

        if (key == juce::KeyPress::returnKey || key == juce::KeyPress::spaceKey)
            owner.openEditor (index);
        else if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)
            tracks.effects().removeEffect (owner.trackId, slot.id);
        else if (key == juce::KeyPress (juce::KeyPress::upKey, juce::ModifierKeys::altModifier, 0))
            tracks.effects().moveEffect (owner.trackId, slot.id, juce::jmax (0, index - 1));
        else if (key == juce::KeyPress (juce::KeyPress::downKey, juce::ModifierKeys::altModifier, 0))
            tracks.effects().moveEffect (owner.trackId, slot.id, index + 1);
        else
            return false;

        return true;
    }

    void focusGained (FocusChangeType cause) override
    {
        showFocusRing = cause != focusChangedByMouseClick;
        repaint();
    }

    void focusLost (FocusChangeType) override   { showFocusRing = false; repaint(); }

    PluginSlot slot;
    int index = 0;

private:
    PluginChainView& owner;
    IconButton power { "Turn effect on or off", Icon::power };
    IconButton more { "Effect options", Icon::more };
    juce::Rectangle<int> nameArea;
    bool showFocusRing = false;
};

//==============================================================================
// The dashed "+ Add effect" slot at the end of the chain.
class PluginChainView::AddRow  : public juce::Button
{
public:
    AddRow() : juce::Button ("Add effect")
    {
        setTitle ("Add effect");
        setTooltip ("Add an effect to this track");
        setMouseClickGrabsKeyboardFocus (false);
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    void paintButton (juce::Graphics& g, bool over, bool) override
    {
        auto r = getLocalBounds().toFloat().reduced (0.5f);

        juce::Path outline, dashed;
        outline.addRoundedRectangle (r, size::radius);
        const float dashes[] = { 4.0f, 3.0f };
        juce::PathStrokeType (1.0f).createDashedStroke (dashed, outline, dashes, 2);

        if (over)
        {
            g.setColour (theme().raised.withAlpha (0.5f));
            g.fillPath (outline);
        }

        g.setColour (over ? theme().textSecondary : theme().borderStrong);
        g.fillPath (dashed);

        auto content = getLocalBounds().reduced (10, 0).toFloat();
        icons::draw (g, Icon::plus, content.removeFromLeft (16.0f), theme().textSecondary, 16.0f, 2.0f);
        content.removeFromLeft (8.0f);
        g.setColour (theme().textSecondary);
        g.setFont (fonts::ui (textSize::md));
        g.drawText ("Add effect", content, juce::Justification::centredLeft);

        if (hasKeyboardFocus (false))
            IkarosLookAndFeel::drawFocusRing (g, r, size::radius);
    }
};

//==============================================================================
PluginChainView::PluginChainView (AudioEngine& e, Selection& s) : engine (e), selection (s)
{
    setTitle ("Plug-ins");
    setFocusContainerType (juce::Component::FocusContainerType::focusContainer);

    addRow = std::make_unique<AddRow>();
    addRow->onClick = [this] { showAddMenu(); };
    addAndMakeVisible (*addRow);

    instrumentWindowButton.setColours (IconButton::ghostColours);
    instrumentWindowButton.setCornerRadius (6.0f);
    instrumentWindowButton.setIconSize (15.0f);
    instrumentWindowButton.setTooltip ("Open the instrument's window");
    instrumentWindowButton.onClick = [this]
    {
        if (instrument.has_value())
            engine.getTracks().effects().showPluginWindow (trackId, instrument->id);
    };
    addChildComponent (instrumentWindowButton);
}

juce::String PluginChainView::describeMissing (const PluginSlot& slot)
{
    if (slot.disabledByUser)
        return slot.name + " is turned off in Preferences, so it isn't loaded. "
               "Its settings are kept; turn it back on under Sounds & Plug-ins.";

    return slot.name + " isn't installed on this computer (or couldn't be loaded). "
           "Its settings are kept, so it will work again once it's installed.";
}

PluginChainView::~PluginChainView() = default;

void PluginChainView::setMasterMode (bool shouldShowMaster)
{
    masterMode = shouldShowMaster;
    refresh();
}

void PluginChainView::refresh()
{
    auto& tracks = engine.getTracks();
    const auto oldHeight = getIdealHeight();

    trackId = masterMode ? masterTrackId : selection.getTrack();
    auto track = tracks.getTrack (trackId);
    family = track.has_value() ? colour::familyOf (*track) : Family::master;
    instrument = tracks.effects().getInstrument (trackId);
    chain = tracks.effects().getEffectChain (trackId);

    // This is called on every change in the session (many a second while
    // automation plays): leave the rows alone, and don't redraw, unless
    // something they show changed.
    const auto describe = [] (const PluginSlot& s)
    {
        return juce::String ((juce::int64) s.id) + "|" + s.type + "|" + s.name + "|" + (s.enabled ? "on" : "off")
               + (s.isThirdParty ? "|3p" : "") + (s.missing ? "|missing" : "") + "|" + s.missingPackId + "|" + s.missingPackName + "\n";
    };

    auto state = juce::String ((juce::int64) trackId) + "|" + juce::String ((int) family) + (masterMode ? "|master\n" : "\n")
                 + (instrument.has_value() ? describe (*instrument) : juce::String ("none\n"));

    for (auto& s : chain)
        state += describe (s);

    if (state == shownState)
        return;

    shownState = state;

    // Keep the same row components where possible (so keyboard focus survives).
    while (rows.size() > (int) chain.size())
        rows.removeLast();

    while (rows.size() < (int) chain.size())
        addAndMakeVisible (rows.add (new EffectRow (*this)));

    for (int i = 0; i < rows.size(); ++i)
        rows[i]->update (chain[(size_t) i], i);

    addRow->setVisible (trackId != 0 && (int) chain.size() < EffectsController::maxEffectsPerTrack);
    instrumentWindowButton.setVisible (instrument.has_value() && instrument->isThirdParty && ! instrument->missing);

    resized();
    repaint();

    if (getIdealHeight() != oldHeight && onHeightChanged)
        onHeightChanged();

    if (openWhenShown != 0)
    {
        for (int i = 0; i < (int) chain.size(); ++i)
        {
            if (chain[(size_t) i].id == openWhenShown)
            {
                openWhenShown = 0;

                // Wait one message-loop turn so the panel has been laid out.
                juce::MessageManager::callAsync ([safeThis = SafePointer<PluginChainView> (this), i]
                {
                    if (safeThis != nullptr)
                        safeThis->openEditor (i);
                });
                break;
            }
        }
    }
}

int PluginChainView::getIdealHeight() const
{
    if (trackId == 0)
        return 60;

    int h = 8 + 34 + 8;                                     // hint text
    h += instrument.has_value() ? labelHeight + rowHeight + 12 : 0;   // instrument (not on audio tracks)
    h += labelHeight + (int) chain.size() * (rowHeight + rowGap);
    h += (int) chain.size() < EffectsController::maxEffectsPerTrack ? rowHeight : 0;
    return h + 14;
}

void PluginChainView::paint (juce::Graphics& g)
{
    IKAROS_PERF_PAINT ("PluginChainView");
    if (trackId == 0)
    {
        g.setColour (theme().textSecondary);
        g.setFont (fonts::ui (textSize::md));
        g.drawFittedText ("Select a track to see its plug-ins.", getLocalBounds().reduced (sidePadding, 8),
                          juce::Justification::topLeft, 2);
        return;
    }

    g.setColour (theme().textSecondary);
    g.setFont (fonts::ui (textSize::sm));
    g.drawFittedText (masterMode ? "Effects on the whole mix. They run from top to bottom."
                                 : "Change how this track sounds. Effects run from top to bottom.",
                      hintArea, juce::Justification::topLeft, 2, 1.0f);   // 1.0 = wrap, never squash

    auto drawLabel = [&] (juce::Rectangle<int> area, const juce::String& text)
    {
        g.setColour (theme().textSecondary);
        g.setFont (fonts::uiBold (textSize::tiny).withExtraKerningFactor (0.1f));
        g.drawText (text, area, juce::Justification::centredLeft);
    };

    drawLabel (effectsLabelArea, "EFFECTS");

    // Instrument row (instrument tracks only; not changeable yet).
    if (instrument.has_value())
    {
        drawLabel (instrumentLabelArea, "INSTRUMENT");

        g.setColour (theme().cardBg);
        g.fillRoundedRectangle (instrumentArea.toFloat(), size::radius);
        auto r = instrumentArea.reduced (10, 0);
        icons::draw (g, Icon::synths, r.removeFromLeft (18).toFloat(), theme().family (family), 16.0f);
        r.removeFromLeft (8);
        if (instrument->missing)
        {
            icons::draw (g, Icon::warning, r.removeFromRight (16).toFloat(), theme().warning, 14.0f);
            r.removeFromRight (4);
        }
        else if (instrumentWindowButton.isVisible())
        {
            r.setRight (instrumentWindowButton.getX() - 4);
        }

        g.setColour (instrument->missing ? theme().textSecondary : theme().text);
        g.setFont (fonts::ui (textSize::md));
        g.drawText (instrument->name + (instrument->disabledByUser ? " (disabled)" : instrument->missing ? " (not installed)" : ""), r, juce::Justification::centredLeft, true);
    }

    // Insertion line while dragging an effect.
    if (dragFromIndex >= 0 && dropIndex >= 0)
    {
        const int top = effectsLabelArea.getBottom() + 2;
        const int y = top + dropIndex * (rowHeight + rowGap) - rowGap / 2 - 1;
        g.setColour (theme().highlight);
        g.fillRoundedRectangle ((float) sidePadding, (float) y, (float) (getWidth() - sidePadding * 2), 2.0f, 1.0f);
    }
}

void PluginChainView::resized()
{
    auto r = getLocalBounds().reduced (sidePadding, 0);
    r.removeFromTop (8);
    hintArea = r.removeFromTop (34);
    r.removeFromTop (8);

    if (instrument.has_value())
    {
        instrumentLabelArea = r.removeFromTop (labelHeight);
        instrumentArea = r.removeFromTop (rowHeight);
        instrumentWindowButton.setBounds (instrumentArea.reduced (5).removeFromRight (24));
        r.removeFromTop (12);
    }

    effectsLabelArea = r.removeFromTop (labelHeight);
    r.removeFromTop (2);

    for (auto* row : rows)
    {
        row->setBounds (r.removeFromTop (rowHeight));
        r.removeFromTop (rowGap);
    }

    addRow->setBounds (r.removeFromTop (rowHeight));
}

//==============================================================================
juce::PopupMenu PluginChainView::buildEffectMenu (std::function<void (const EffectType&)> onChosen) const
{
    juce::PopupMenu menu, plugins, makerMenu;
    juce::String currentCategory;

    // Third-party effects go in a "Plug-ins" submenu, one submenu per maker.
    auto finishMaker = [&]
    {
        if (currentCategory.isNotEmpty() && makerMenu.getNumItems() > 0)
            plugins.addSubMenu (currentCategory, makerMenu);

        makerMenu = {};
    };

    // A track has at most one Amp.
    const bool hasAmp = engine.getTracks().effects().getAmp (trackId).exists;

    for (auto& effect : engine.getTracks().effects().getAvailableEffects())
    {
        // Some only go on some tracks (Tune: audio tracks).
        if (! engine.getTracks().effects().canAddEffect (trackId, effect.type))
            continue;

        if (effect.category != currentCategory)
        {
            if (effect.isThirdParty)
                finishMaker();
            else
                menu.addSectionHeader (effect.category);

            currentCategory = effect.category;
        }

        // PopupMenu items can run a lambda directly when chosen.
        (effect.isThirdParty ? makerMenu : menu).addItem (effect.name, ! (hasAmp && effect.type == ampPluginType), false,
                                                          [onChosen, effect] { onChosen (effect); });
    }

    finishMaker();

    if (plugins.getNumItems() > 0)
    {
        menu.addSeparator();
        menu.addSubMenu ("Plug-ins", plugins);
    }

    return menu;
}

void PluginChainView::showAddMenu()
{
    auto menu = buildEffectMenu ([this, safeThis = SafePointer<PluginChainView> (this)] (const EffectType& effect)
    {
        if (safeThis == nullptr)
            return;

        // The new row appears on the next refresh (model changes arrive
        // asynchronously), so open its settings from there.
        openWhenShown = engine.getTracks().effects().addEffect (trackId, effect.type);
    });

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (addRow.get()));
}

void PluginChainView::showRowMenu (int rowIndex)
{
    if (! juce::isPositiveAndBelow (rowIndex, (int) chain.size()))
        return;

    const auto slot = chain[(size_t) rowIndex];
    auto& tracks = engine.getTracks();

    juce::PopupMenu menu;
    if (slot.isThirdParty)
        menu.addItem ("Open Plug-in Window", ! slot.missing, false, [this, rowIndex] { openEditor (rowIndex); });
    else
        menu.addItem ("Settings...", [this, rowIndex] { openEditor (rowIndex); });

    menu.addItem (slot.enabled ? "Turn Off" : "Turn On", [&tracks, this, slot] { tracks.effects().setEffectEnabled (trackId, slot.id, ! slot.enabled); });
    menu.addSubMenu ("Replace With", buildEffectMenu ([&tracks, this, slot] (const EffectType& effect)
    {
        tracks.effects().replaceEffect (trackId, slot.id, effect.type);
    }));

    // An effect's ready-made settings (Tune's Natural and Hard Tune).
    if (const auto presets = tracks.effects().getEffectPresets (trackId, slot.id); ! presets.isEmpty())
    {
        juce::PopupMenu presetMenu;

        for (int i = 0; i < presets.size(); ++i)
            presetMenu.addItem (presets[i], [&tracks, this, slot, i] { tracks.effects().applyEffectPreset (trackId, slot.id, i); });

        menu.addSubMenu ("Presets", presetMenu);
    }

    menu.addSeparator();
    menu.addItem ("Move Up", rowIndex > 0, false, [&tracks, this, slot, rowIndex] { tracks.effects().moveEffect (trackId, slot.id, rowIndex - 1); });
    menu.addItem ("Move Down", rowIndex < (int) chain.size() - 1, false, [&tracks, this, slot, rowIndex] { tracks.effects().moveEffect (trackId, slot.id, rowIndex + 1); });
    menu.addSeparator();
    menu.addItem ("Remove", [&tracks, this, slot] { tracks.effects().removeEffect (trackId, slot.id); });

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (rows[rowIndex]));
}

void PluginChainView::openEditor (int rowIndex)
{
    if (! juce::isPositiveAndBelow (rowIndex, (int) chain.size()))
        return;

    auto& slot = chain[(size_t) rowIndex];

    // Third-party plug-ins draw their own settings, in a window of their own.
    if (slot.isThirdParty)
        engine.getTracks().effects().showPluginWindow (trackId, slot.id);
    else if (onOpenEffect)
        onOpenEffect (slot.id);
}

void PluginChainView::setOpenEffect (PluginId id)
{
    if (id != openEffectId)
    {
        openEffectId = id;

        for (auto* row : rows)
            row->repaint();
    }
}

//==============================================================================
void PluginChainView::dragRow (int rowIndex, int y)
{
    dragFromIndex = rowIndex;

    // Which gap between rows (0 = above the first, n = below the last) is
    // nearest the mouse?
    const int top = effectsLabelArea.getBottom() + 2;
    const auto newDrop = juce::jlimit (0, (int) chain.size(),
                                       juce::roundToInt ((float) (y - top) / (float) (rowHeight + rowGap)));

    if (newDrop != dropIndex)
    {
        dropIndex = newDrop;
        repaint();

        for (auto* row : rows)
            row->repaint();
    }
}

void PluginChainView::endDrag()
{
    const int from = dragFromIndex, drop = dropIndex;
    dragFromIndex = dropIndex = -1;
    repaint();

    if (! juce::isPositiveAndBelow (from, (int) chain.size()) || drop < 0)
        return;

    // Dropping below its own position shifts everything up by one once the
    // effect has been taken out of the chain.
    const int newPosition = drop > from ? drop - 1 : drop;

    if (newPosition != from)
        engine.getTracks().effects().moveEffect (trackId, chain[(size_t) from].id, newPosition);
    else
        for (auto* row : rows)
            row->repaint();
}

} // namespace ikaros
