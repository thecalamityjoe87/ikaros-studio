#include "ui/arrangement/ArrangementAutomation.h"
#include "ui/arrangement/ArrangementLayout.h"
#include "ui/theme/Fonts.h"
#include "ui/theme/IkarosLookAndFeel.h"
#include "ui/widgets/AutomationHint.h"
#include "ui/widgets/Icons.h"
#include <numeric>

namespace ikaros
{

using namespace tokens;
using namespace arrangement;

namespace
{
    constexpr float pointRadius = 4.0f;
    constexpr float hitRadius = 7.0f;
    constexpr float curveMargin = 8.0f;   // space above the top value and below the bottom one
}

//==============================================================================
// The lane
ArrangementView::AutomationLane::AutomationLane (ArrangementView& o, TrackId id) : owner (o), trackId (id)
{
    setTitle ("Automation lane");
    setWantsKeyboardFocus (true);   // for Delete and Ctrl/Cmd+A
}

void ArrangementView::AutomationLane::update (const TrackInfo& t)
{
    setAlpha (t.silencedBySolo || t.muted ? 0.4f : 1.0f);

    // A drag in progress keeps its own points (the engine's are a step behind).
    if (gesture != Gesture::none)
        return;

    auto& automation = owner.engine.getTracks().automation();
    const auto target = automation.getTarget (trackId, t.automationTarget);
    const bool targetChanged = t.automationTarget != targetId;
    targetId = t.automationTarget;
    editable = target.has_value() && target->editable;

    setTooltip (! target ? juce::String()
                : editable ? "Click to add a point, drag points to move them, double-click a point to delete it"
                           : "This plug-in is missing. Its automation is kept, and can be changed again once it's back.");

    auto newPoints = automation.getPoints (trackId, targetId);
    const auto newValue = automation.getCurrentValue (trackId, targetId);

    if (targetChanged || newPoints.size() != points.size())
        selected.assign (newPoints.size(), false);

    if (targetChanged || newPoints != points || (newPoints.empty() && newValue != currentValue))
    {
        points = std::move (newPoints);
        currentValue = newValue;
        repaint();
    }
}

float ArrangementView::AutomationLane::valueToY (float value) const
{
    const float top = curveMargin, bottom = (float) getHeight() - 1.0f - curveMargin;
    return bottom - value * (bottom - top);
}

float ArrangementView::AutomationLane::yToValue (float y) const
{
    const float top = curveMargin, bottom = (float) getHeight() - 1.0f - curveMargin;
    return juce::jlimit (0.0f, 1.0f, (bottom - y) / juce::jmax (1.0f, bottom - top));
}

juce::Point<float> ArrangementView::AutomationLane::pointPosition (const AutomationPoint& p) const
{
    return { owner.geometry.beatToX (p.beat), valueToY (p.value) };
}

int ArrangementView::AutomationLane::pointAt (juce::Point<float> pos) const
{
    int best = -1;
    float bestDistance = hitRadius;

    for (int i = 0; i < (int) points.size(); ++i)
    {
        const float d = pointPosition (points[(size_t) i]).getDistanceFrom (pos);

        if (d <= bestDistance)
        {
            best = i;
            bestDistance = d;
        }
    }

    return best;
}

int ArrangementView::AutomationLane::numSelected() const
{
    return (int) std::count (selected.begin(), selected.end(), true);
}

//==============================================================================
void ArrangementView::AutomationLane::paint (juce::Graphics& g)
{
    // No background: the track's lane (grid and faded regions) shows through.
    // Each kind of setting has its own colour (volume blue, pan green...).
    const float width = (float) getWidth();
    const auto lineColour = getAutomationColour (targetId);

    if (points.empty())
    {
        // Not automated: a dashed line at the setting's value.
        const float y = valueToY (currentValue);
        juce::Path line;
        line.startNewSubPath (0.0f, y);
        line.lineTo (width, y);

        juce::Path dashed;
        const float dashes[] { 6.0f, 4.0f };
        juce::PathStrokeType (1.5f).createDashedStroke (dashed, line, dashes, 2);
        g.setColour (lineColour.withAlpha (0.8f));
        g.fillPath (dashed);

        if (editable)
        {
            g.setColour (colour::textSecondary);
            g.setFont (fonts::ui (textSize::xs));
            g.drawText ("Click to add a point", juce::Rectangle<float> (8.0f, y < 24.0f ? y + 4.0f : y - 18.0f, 200.0f, 14.0f),
                        juce::Justification::centredLeft, false);
        }
    }
    else
    {
        // The curve: flat before the first point and after the last, as it plays.
        juce::Path curve;
        curve.startNewSubPath (0.0f, valueToY (points.front().value));

        for (auto& p : points)
            curve.lineTo (pointPosition (p));

        curve.lineTo (width, valueToY (points.back().value));

        juce::Path fill (curve);
        fill.lineTo (width, (float) getHeight());
        fill.lineTo (0.0f, (float) getHeight());
        fill.closeSubPath();
        g.setColour (lineColour.withAlpha (editable ? 0.12f : 0.06f));
        g.fillPath (fill);

        g.setColour (editable ? lineColour : lineColour.withAlpha (0.5f));
        g.strokePath (curve, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        for (size_t i = 0; i < points.size(); ++i)
        {
            const auto pos = pointPosition (points[i]);

            if (pos.x < -hitRadius || pos.x > width + hitRadius)
                continue;

            const auto dot = juce::Rectangle<float> (pointRadius * 2.0f, pointRadius * 2.0f).withCentre (pos);

            if (selected[i])
            {
                g.setColour (colour::text);
                g.fillEllipse (dot.expanded (1.0f));
                g.setColour (lineColour);
                g.drawEllipse (dot.expanded (1.0f), 2.0f);
            }
            else
            {
                g.setColour (editable ? lineColour : lineColour.withAlpha (0.5f));
                g.fillEllipse (dot);
            }
        }

        // While dragging, the grabbed point's value, as its control would show it.
        if (gesture == Gesture::movePoints && juce::isPositiveAndBelow (labelPoint, (int) points.size()))
        {
            const auto& p = points[(size_t) labelPoint];
            const auto pos = pointPosition (p);
            const auto text = owner.engine.getTracks().automation().describeValue (trackId, targetId, p.value);
            g.setFont (fonts::mono (textSize::xs));
            auto label = juce::Rectangle<float> (90.0f, 18.0f).withCentre (pos).translated (0.0f, pos.y > 28.0f ? -16.0f : 16.0f);
            g.setColour (colour::raised);
            g.fillRoundedRectangle (label, size::radiusSmall);
            g.setColour (colour::text);
            g.drawText (text, label, juce::Justification::centred, false);
        }
    }

    if (! selectBox.isEmpty())
    {
        g.setColour (colour::accent.withAlpha (0.1f));
        g.fillRect (selectBox);
        g.setColour (colour::accent);
        g.drawRect (selectBox, 1.0f);
    }
}

//==============================================================================
void ArrangementView::AutomationLane::apply (std::vector<AutomationPoint> newPoints, std::vector<bool> newSelected,
                                             const juce::String& undoName)
{
    // Sort by beat, carrying each point's selection (and the grabbed point)
    // along. The engine sorts the same way, so its indices match ours.
    std::vector<size_t> order (newPoints.size());
    std::iota (order.begin(), order.end(), size_t (0));   // 0, 1, 2...
    std::stable_sort (order.begin(), order.end(), [&] (size_t a, size_t b) { return newPoints[a].beat < newPoints[b].beat; });

    points.clear();
    selected.clear();
    int newGrabbed = -1;

    for (size_t i = 0; i < order.size(); ++i)
    {
        points.push_back (newPoints[order[i]]);
        selected.push_back (newSelected[order[i]]);

        if ((int) order[i] == grabbedPoint)
            newGrabbed = (int) i;
    }

    labelPoint = newGrabbed;   // grabbedPoint counts in the unsorted order; painting needs the sorted one

    if (undoName.isNotEmpty())
        owner.engine.beginUndoStep (undoName);

    owner.engine.getTracks().automation().setPoints (trackId, targetId, points);
    repaint();
}

void ArrangementView::AutomationLane::deleteSelected()
{
    std::vector<AutomationPoint> kept;

    for (size_t i = 0; i < points.size(); ++i)
        if (! selected[i])
            kept.push_back (points[i]);

    if (kept.size() != points.size())
        apply (std::move (kept), std::vector<bool> (kept.size(), false), "Delete Automation Points");
}

void ArrangementView::AutomationLane::showMenu (juce::Point<float>)
{
    juce::Component::SafePointer<AutomationLane> safe (this);
    juce::PopupMenu menu;

    menu.addItem (juce::PopupMenu::Item ("Delete Selected Points")
                      .setEnabled (editable && numSelected() > 0)
                      .setAction ([safe] { if (safe) safe->deleteSelected(); }));
    menu.addItem (juce::PopupMenu::Item ("Select All Points")
                      .setEnabled (! points.empty())
                      .setAction ([safe] { if (safe) { std::fill (safe->selected.begin(), safe->selected.end(), true); safe->repaint(); } }));
    menu.addSeparator();
    menu.addItem (juce::PopupMenu::Item ("Delete All Points")
                      .setEnabled (editable && ! points.empty())
                      .setAction ([safe] { if (safe) safe->apply ({}, {}, "Delete Automation"); }));

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
}

//==============================================================================
void ArrangementView::AutomationLane::mouseMove (const juce::MouseEvent& e)
{
    if (! editable)
        setMouseCursor (juce::MouseCursor::NormalCursor);
    else
        setMouseCursor (pointAt (e.position) >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::CrosshairCursor);
}

void ArrangementView::AutomationLane::mouseDown (const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    owner.selection.selectTrack (trackId);

    if (e.mods.isPopupMenu())
    {
        if (const int hit = pointAt (e.position); hit >= 0 && ! selected[(size_t) hit])
        {
            std::fill (selected.begin(), selected.end(), false);
            selected[(size_t) hit] = true;
            repaint();
        }

        showMenu (e.position);
        return;
    }

    if (! editable)
        return;

    const bool adding = e.mods.isShiftDown() || e.mods.isCommandDown();
    grabbedPoint = pointAt (e.position);

    if (grabbedPoint >= 0)
    {
        const auto i = (size_t) grabbedPoint;

        if (adding)
        {
            selected[i] = ! selected[i];
        }
        else if (! selected[i])
        {
            // Just this one.
            std::fill (selected.begin(), selected.end(), false);
            selected[i] = true;
        }
    }
    else if (! adding)
    {
        std::fill (selected.begin(), selected.end(), false);
    }

    gesture = Gesture::pending;
    pointsAtStart = points;
    selectedAtStart = selected;
    undoStepStarted = false;
    repaint();
}

void ArrangementView::AutomationLane::mouseDrag (const juce::MouseEvent& e)
{
    if (gesture == Gesture::pending && e.getDistanceFromDragStart() > 3)
    {
        const bool onSelectedPoint = grabbedPoint >= 0 && selectedAtStart[(size_t) grabbedPoint];
        gesture = onSelectedPoint ? Gesture::movePoints : Gesture::selectBox;
    }

    if (gesture == Gesture::movePoints)
    {
        // Move the grabbed point to the mouse (on the grid), and every other
        // selected point by the same amount.
        const auto& anchor = pointsAtStart[(size_t) grabbedPoint];
        const double rawBeat = anchor.beat + e.getDistanceFromDragStartX() / owner.geometry.pixelsPerBeat;
        double deltaBeats = (e.mods.isAltDown() ? rawBeat : owner.snapBeat (rawBeat)) - anchor.beat;
        const float deltaValue = yToValue (valueToY (anchor.value) + (float) e.getDistanceFromDragStartY()) - anchor.value;

        // Nothing goes before the start of the song.
        for (size_t i = 0; i < pointsAtStart.size(); ++i)
            if (selectedAtStart[i])
                deltaBeats = juce::jmax (deltaBeats, -pointsAtStart[i].beat);

        auto moved = pointsAtStart;

        for (size_t i = 0; i < moved.size(); ++i)
        {
            if (selectedAtStart[i])
            {
                moved[i].beat += deltaBeats;
                moved[i].value = juce::jlimit (0.0f, 1.0f, moved[i].value + deltaValue);
            }
        }

        // One undo step for the whole drag, started once it really moves.
        const juce::String undoName = undoStepStarted ? juce::String() : juce::String ("Move Automation Points");
        undoStepStarted = true;

        apply (std::move (moved), selectedAtStart, undoName);
    }
    else if (gesture == Gesture::selectBox)
    {
        selectBox = juce::Rectangle<float> (e.mouseDownPosition, e.position);
        const bool adding = e.mods.isShiftDown() || e.mods.isCommandDown();

        for (size_t i = 0; i < points.size(); ++i)
            selected[i] = (adding && selectedAtStart[i]) || selectBox.contains (pointPosition (points[i]));

        repaint();
    }
}

void ArrangementView::AutomationLane::mouseUp (const juce::MouseEvent& e)
{
    const auto finished = gesture;
    gesture = Gesture::none;
    selectBox = {};

    // A click on an empty spot adds a point there (on the grid).
    if (finished == Gesture::pending && grabbedPoint < 0 && editable
         && ! (e.mods.isShiftDown() || e.mods.isCommandDown() || e.mods.isPopupMenu()))
    {
        auto withNew = points;
        withNew.push_back ({ juce::jmax (0.0, owner.snapBeat (owner.geometry.xToBeat (e.position.x))), yToValue (e.position.y) });
        std::vector<bool> onlyNew (points.size(), false);
        onlyNew.push_back (true);
        apply (std::move (withNew), std::move (onlyNew), "Add Automation Point");
    }

    grabbedPoint = -1;
    undoStepStarted = false;
    repaint();

    // Catch up with the engine (changes during the drag were ignored).
    if (auto t = owner.engine.getTracks().getTrack (trackId))
        update (*t);
}

void ArrangementView::AutomationLane::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (! editable)
        return;

    if (const int hit = pointAt (e.position); hit >= 0)
    {
        auto remaining = points;
        remaining.erase (remaining.begin() + hit);
        apply (std::move (remaining), std::vector<bool> (points.size() - 1, false), "Delete Automation Point");
    }
}

bool ArrangementView::AutomationLane::keyPressed (const juce::KeyPress& key)
{
    if ((key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey) && editable && numSelected() > 0)
    {
        deleteSelected();
        return true;
    }

    if (key == juce::KeyPress ('a', juce::ModifierKeys::commandModifier, 0))
    {
        std::fill (selected.begin(), selected.end(), true);
        repaint();
        return true;
    }

    if (key == juce::KeyPress::escapeKey && numSelected() > 0)
    {
        std::fill (selected.begin(), selected.end(), false);
        repaint();
        return true;
    }

    return false;
}

} // namespace ikaros
