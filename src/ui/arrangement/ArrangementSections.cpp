#include "ui/arrangement/ArrangementSections.h"
#include "ui/widgets/PaintProbe.h"
#include "ui/arrangement/ArrangementLayout.h"
#include "engine/AudioEngine.h"
#include "engine/TrackController.h"
#include "ui/theme/Fonts.h"
#include "ui/theme/IkarosLookAndFeel.h"

namespace ikaros
{

using namespace tokens;
using namespace arrangement;

namespace
{
    constexpr float blockGap = 3.0f;        // between a block and the strip's top and bottom
    constexpr float edgeGrab = 5.0f;        // how near an edge counts as grabbing it
    constexpr int dragThreshold = 4;        // pixels before a press on a block becomes a move
    constexpr double tolerance = 1.0e-6;    // beats closer than this are the same place

    // Names for SectionEditor::getSectionColours(), in the same order.
    const juce::StringArray colourNames { "Blue", "Orange", "Green", "Purple", "Yellow", "Pink", "Teal" };
}

ArrangementView::SectionStrip::SectionStrip (ArrangementView& o) : owner (o)
{
    setTitle ("Sections");
    setTooltip ("Song sections: double-click to add one at a bar, drag an edge to resize it, drag it to move "
                "its music (hold Alt to copy). Right-click a section to rename, recolor or delete it.");
    setWantsKeyboardFocus (true);   // for Delete on a clicked section

    addButton.setColours ([]
    {
        auto c = IconButton::raisedColours();
        c.background = theme().buttonFill;
        c.border = theme().buttonBorder;
        return c;
    });
    addButton.setCornerRadius (size::radiusXs);
    addButton.setIconSize (12.0f);
    addButton.setTooltip ("Add a section after the last one");
    addButton.onClick = [this] { addSectionAfterLast(); };
    addAndMakeVisible (addButton);

    nameField.setFont (fonts::ui (textSize::sm));
    nameField.setTitle ("Section name");
    IkarosLookAndFeel::styleInlineEditor (nameField);
    nameField.onReturnKey = [this] { commitNameField(); };
    nameField.onEscapeKey = [this] { hideNameField(); };
    nameField.onFocusLost = [this] { commitNameField(); };
    addChildComponent (nameField);
}

void ArrangementView::SectionStrip::sync()
{
    sections = owner.engine.getTracks().sections().getSections();

    if (selectedId != 0 && find (selectedId) == nullptr)
        selectedId = 0;   // deleted, or undone away

    repaint();
}

void ArrangementView::SectionStrip::resized()
{
    // Centred in the column the track headers' arm buttons and pan knobs
    // use, so all three line up.
    constexpr int column = TrackHeader::controlColumnWidth;
    addButton.setBounds (juce::Rectangle<int> (columnRight - column, 0, column, getHeight() - 1).withSizeKeepingCentre (22, 18));
}

//==============================================================================
// Geometry
float ArrangementView::SectionStrip::beatToLaneX (double beat) const   { return owner.geometry.beatToX (beat); }
double ArrangementView::SectionStrip::laneXToBeat (float x) const      { return juce::jmax (0.0, owner.geometry.xToBeat (x)); }

juce::Point<float> ArrangementView::SectionStrip::laneXY (const juce::MouseEvent& e) const
{
    return e.position.translated ((float) -headerWidth, 0.0f);
}

juce::Rectangle<float> ArrangementView::SectionStrip::blockBounds (const SectionInfo& s) const
{
    const float x = beatToLaneX (s.startBeat);
    return { x, blockGap, beatToLaneX (s.endBeat) - x, (float) getHeight() - 1.0f - blockGap * 2.0f };
}

const SectionInfo* ArrangementView::SectionStrip::find (SectionId id) const
{
    for (auto& s : sections)
        if (s.id == id)
            return &s;

    return nullptr;
}

ArrangementView::SectionStrip::Hit ArrangementView::SectionStrip::findHit (juce::Point<float> p) const
{
    if (p.x < 0.0f)
        return {};

    // Edges first (the nearer one wins where two sections meet), then bodies.
    // A short block's edges leave room in its middle to grab the body.
    Hit best;
    float bestDistance = edgeGrab;

    for (auto& s : sections)
    {
        const auto box = blockBounds (s);
        const float grab = juce::jmin (edgeGrab, box.getWidth() / 3.0f);

        for (auto [x, kind] : { std::pair { box.getX(), Hit::Kind::startEdge }, std::pair { box.getRight(), Hit::Kind::endEdge } })
        {
            const float distance = std::abs (p.x - x);

            if (distance <= grab && distance <= bestDistance)
            {
                best = { kind, s.id };
                bestDistance = distance;
            }
        }
    }

    if (best.kind != Hit::Kind::none)
        return best;

    for (auto& s : sections)
        if (blockBounds (s).getHorizontalRange().contains (p.x))
            return { Hit::Kind::body, s.id };

    return {};
}

//==============================================================================
void ArrangementView::SectionStrip::paint (juce::Graphics& g)
{
    IKAROS_PERF_PAINT ("ArrangementView::SectionStrip");
    auto r = getLocalBounds();
    g.fillAll (theme().sectionsStripBg);
    g.setColour (theme().divider);
    g.fillRect (r.removeFromBottom (1));

    auto label = r.removeFromLeft (headerWidth);
    g.setColour (theme().sectionsRowBg);
    g.fillRect (label);
    g.setColour (theme().divider);
    g.fillRect (label.removeFromRight (1));
    g.setColour (theme().textSecondary);
    g.setFont (columnLabelFont());
    g.drawText ("SECTIONS", label.withTrimmedLeft (columnPadding), juce::Justification::centredLeft);

    // Everything below scrolls with the timeline; keep it out of the header part.
    juce::Graphics::ScopedSaveState save (g);
    g.reduceClipRegion (r);
    g.setOrigin (r.getX(), 0);
    g.setColour (theme().sectionsStripBg);
    g.fillRect (r.withX (0));

    auto& geo = owner.geometry;
    g.setColour (theme().divider);
    forEachVisibleBar (geo, r.getWidth(), barStride (geo, 8.0), [&] (int, int x) { g.fillRect (x, 0, 1, r.getHeight()); });

    auto drawBlock = [&] (const SectionInfo& s, juce::Rectangle<float> box, float alpha, bool lit)
    {
        g.setColour (s.colour.withMultipliedAlpha (alpha * (lit ? 0.55f : 0.35f)));
        g.fillRoundedRectangle (box.reduced (0.5f, 0.0f), size::radiusXs);
        g.setColour (s.colour.withMultipliedAlpha (alpha));
        g.drawRoundedRectangle (box.reduced (1.0f, 0.5f), size::radiusXs, lit ? 1.5f : 1.0f);

        // A stripe down the left edge, like a flag at the bar where it starts.
        g.fillRect (box.getX() + 1.0f, box.getY() + 1.0f, 3.0f, box.getHeight() - 2.0f);

        g.setColour (theme().text.withMultipliedAlpha (alpha));
        g.setFont (lit ? fonts::uiSemiBold (textSize::sm) : fonts::ui (textSize::sm));
        g.drawFittedText (s.name, box.withTrimmedLeft (9.0f).withTrimmedRight (4.0f).toNearestInt(),
                          juce::Justification::centredLeft, 1, 1.0f);
    };

    const bool moving = drag.has_value() && drag->moving;

    for (auto& s : sections)
    {
        const bool lit = s.id == selectedId || (hover.id == s.id && ! moving);
        const bool dragged = moving && s.id == drag->section.id;
        drawBlock (s, blockBounds (s), dragged ? 0.4f : 1.0f, lit);

        // The grabbed (or grabbable) edge.
        const auto edgeKind = drag.has_value() && drag->section.id == s.id ? drag->kind : hover.id == s.id ? hover.kind : Hit::Kind::none;

        if (edgeKind == Hit::Kind::startEdge || edgeKind == Hit::Kind::endEdge)
        {
            const auto box = blockBounds (s);
            const float x = edgeKind == Hit::Kind::startEdge ? box.getX() : box.getRight();
            g.setColour (theme().text);
            g.fillRect (x - 1.0f, 0.0f, 2.0f, (float) getHeight() - 1.0f);
        }
    }

    if (moving)
    {
        // The section where the mouse has it, and a line where it would go.
        auto ghost = drag->section;
        ghost.endBeat = drag->ghostStart + ghost.lengthBeats();
        ghost.startBeat = drag->ghostStart;
        g.setColour (theme().sectionsStripBg);   // so the blocks under it don't show through
        g.fillRoundedRectangle (blockBounds (ghost), size::radiusXs);
        drawBlock (ghost, blockBounds (ghost), 0.85f, true);

        if (drag->target.has_value())
        {
            const float x = beatToLaneX (*drag->target);
            g.setColour (theme().highlight);
            g.fillRect (x - 1.0f, 0.0f, 2.0f, (float) getHeight() - 1.0f);

            juce::Path arrow;
            arrow.addTriangle (x - 5.0f, 0.0f, x + 5.0f, 0.0f, x, 6.0f);
            g.fillPath (arrow);

            if (drag->copy)
            {
                g.setFont (fonts::uiSemiBold (textSize::xs));
                g.drawText ("+ Copy", juce::Rectangle<float> (x + 5.0f, 0.0f, 60.0f, (float) getHeight() - 1.0f),
                            juce::Justification::centredLeft);
            }
        }
    }
}

//==============================================================================
// Mouse
void ArrangementView::SectionStrip::mouseMove (const juce::MouseEvent& e)
{
    const auto hit = e.x < headerWidth ? Hit {} : findHit (laneXY (e));

    if (hit.kind != hover.kind || hit.id != hover.id)
    {
        hover = hit;
        setMouseCursor (hit.kind == Hit::Kind::body ? juce::MouseCursor::DraggingHandCursor
                      : hit.kind != Hit::Kind::none ? juce::MouseCursor::LeftRightResizeCursor
                                                    : juce::MouseCursor::NormalCursor);
        repaint();
    }
}

void ArrangementView::SectionStrip::mouseExit (const juce::MouseEvent&)
{
    hover = {};
    repaint();
}

void ArrangementView::SectionStrip::mouseDown (const juce::MouseEvent& e)
{
    drag.reset();

    if (e.x < headerWidth)
        return;

    const auto p = laneXY (e);
    const auto hit = findHit (p);

    if (e.mods.isPopupMenu())
    {
        if (hit.kind != Hit::Kind::none)
            showSectionMenu (hit.id);
        else
            showEmptyMenu (laneXToBeat (p.x));

        return;
    }

    selectedId = hit.id;
    grabKeyboardFocus();
    repaint();

    if (auto* s = find (hit.id))
    {
        Drag d;
        d.kind = hit.kind;
        d.section = *s;
        d.ghostStart = s->startBeat;
        drag = d;
    }
}

void ArrangementView::SectionStrip::mouseDrag (const juce::MouseEvent& e)
{
    if (! drag.has_value())
        return;

    auto& editor = owner.engine.getTracks().sections();
    const auto& tempo = owner.geometry.tempo;
    auto& section = drag->section;

    if (drag->kind == Hit::Kind::body)
    {
        drag->moving = drag->moving || e.getDistanceFromDragStart() > dragThreshold;

        if (drag->moving)
            updateMoveTarget (e);

        return;
    }

    // Resizing: the edge goes to the nearest bar line, between the section's
    // neighbours, and the section stays at least a bar long.
    double previousEnd = 0.0, nextStart = std::numeric_limits<double>::max();

    for (auto& s : sections)
    {
        if (s.id == section.id)
            continue;

        if (s.endBeat <= section.startBeat + tolerance)
            previousEnd = juce::jmax (previousEnd, s.endBeat);
        else if (s.startBeat >= section.endBeat - tolerance)
            nextStart = juce::jmin (nextStart, s.startBeat);
    }

    const double beat = tempo.nearestBarLine (laneXToBeat (laneXY (e).x));
    double start = section.startBeat, end = section.endBeat;

    if (drag->kind == Hit::Kind::startEdge)
        start = juce::jlimit (previousEnd, juce::jmax (previousEnd, tempo.barStartAt (end - tolerance)), beat);
    else
    {
        const double oneBar = tempo.barToBeat (tempo.barAt (start + tolerance) + 1);
        end = juce::jlimit (oneBar, juce::jmax (oneBar, nextStart), beat);   // jlimit wants lower <= upper
    }

    if (std::abs (start - section.startBeat) < tolerance && std::abs (end - section.endBeat) < tolerance)
        return;

    // The undo step starts with the first real change, so a click that only
    // selects a section leaves the undo history alone.
    if (! drag->changed)
        owner.engine.beginUndoStep ("Resize Section");

    drag->changed = true;

    if (editor.setSectionRange (section.id, start, end))
    {
        section.startBeat = start;
        section.endBeat = end;

        // Shown straight away; the engine's own update arrives a moment later.
        for (auto& s : sections)
            if (s.id == section.id)
                s = section;

        repaint();
    }
}

void ArrangementView::SectionStrip::updateMoveTarget (const juce::MouseEvent& e)
{
    auto& d = *drag;
    const auto& tempo = owner.geometry.tempo;
    const auto& editor = owner.engine.getTracks().sections();
    const double length = d.section.lengthBeats();

    // e.mods.isCommandDown() is Ctrl on Linux and Windows, Cmd on a Mac.
    d.copy = e.mods.isAltDown() || e.mods.isCommandDown();
    d.ghostStart = juce::jmax (0.0, tempo.nearestBarLine (d.section.startBeat + e.getDistanceFromDragStartX() / owner.geometry.pixelsPerBeat));

    // Moved right, a section ends where it goes; otherwise (and copies)
    // it starts there. That place has to be between sections: over one, the
    // nearer of its edges.
    const bool right = ! d.copy && d.ghostStart > d.section.startBeat;
    const double wanted = right ? d.ghostStart + length : d.ghostStart;
    double target = tempo.nearestBarLine (wanted);

    for (auto& s : sections)
        if (target > s.startBeat + tolerance && target < s.endBeat - tolerance)
            target = wanted - s.startBeat < s.endBeat - wanted ? s.startBeat : s.endBeat;

    std::optional<double> newTarget;

    if (editor.canMoveSectionTo (d.section.id, target, d.copy))
        newTarget = target;

    d.target = newTarget;
    repaint();
}

void ArrangementView::SectionStrip::mouseUp (const juce::MouseEvent& e)
{
    if (drag.has_value() && drag->moving)
    {
        updateMoveTarget (e);   // Alt may have been pressed or let go since the last move

        if (drag->target.has_value())
            owner.engine.getTracks().sections().moveSection (drag->section.id, *drag->target, drag->copy);
    }

    drag.reset();
    repaint();
}

void ArrangementView::SectionStrip::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (e.x < headerWidth || e.mods.isPopupMenu())
        return;

    const auto p = laneXY (e);
    const auto hit = findHit (p);

    if (hit.kind == Hit::Kind::body)
        showNameField (hit.id);
    else if (hit.kind == Hit::Kind::none)
        addSectionAt (owner.geometry.tempo.barAt (laneXToBeat (p.x)));
}

void ArrangementView::SectionStrip::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    owner.handleWheel (e, wheel, true);   // as over the ruler: the wheel scrolls in time
}

bool ArrangementView::SectionStrip::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey && drag.has_value())
    {
        drag.reset();   // cancels a move (a resize has already happened: Undo takes it back)
        repaint();
        return true;
    }

    if ((key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey) && selectedId != 0)
    {
        owner.engine.getTracks().sections().deleteSection (selectedId);
        selectedId = 0;
        repaint();
        return true;
    }

    if (key == juce::KeyPress::escapeKey && selectedId != 0)
    {
        selectedId = 0;
        repaint();
        return true;
    }

    return false;   // everything else goes on to the app's shortcuts
}

void ArrangementView::SectionStrip::focusLost (FocusChangeType)
{
    // A section stays selected only while the strip has the keyboard (so the
    // Delete key can't surprise anyone later).
    if (selectedId != 0 && ! hasKeyboardFocus (true))
    {
        selectedId = 0;
        repaint();
    }
}

//==============================================================================
void ArrangementView::SectionStrip::addSectionAt (int bar)
{
    owner.engine.getTracks().sections().addSection (bar);
}

void ArrangementView::SectionStrip::addSectionAfterLast()
{
    const auto& tempo = owner.geometry.tempo;

    // The bar after the one the last section ends in (the bar it ends at,
    // since sections end on bar lines).
    addSectionAt (sections.empty() ? 0 : tempo.barAt (sections.back().endBeat - tolerance) + 1);
}

void ArrangementView::SectionStrip::showSectionMenu (SectionId id)
{
    auto* s = find (id);

    if (s == nullptr)
        return;

    selectedId = id;
    repaint();

    // The lambdas run after the menu closes. The engine outlives every menu;
    // SafePointer covers this strip going away meanwhile.
    auto& editor = owner.engine.getTracks().sections();
    juce::Component::SafePointer<SectionStrip> safeThis (this);

    juce::PopupMenu menu;
    menu.addSectionHeader (s->name);
    menu.addItem ("Rename Section...", [safeThis, id]
    {
        if (safeThis != nullptr)
            safeThis->showNameField (id);
    });
    menu.addSubMenu ("Section Color", createColourMenu (id));
    menu.addSeparator();
    menu.addItem ("Delete Section and Its Music", [&editor, id] { editor.deleteSection (id); });
    menu.addItem ("Remove Marker Only", [&editor, id] { editor.removeSectionMarker (id); });
    menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition());
}

void ArrangementView::SectionStrip::showEmptyMenu (double beat)
{
    const int bar = owner.geometry.tempo.barAt (beat);
    juce::Component::SafePointer<SectionStrip> safeThis (this);

    juce::PopupMenu menu;
    menu.addItem ("Add Section at Bar " + juce::String (bar + 1), [safeThis, bar]   // bars show from 1
    {
        if (safeThis != nullptr)
            safeThis->addSectionAt (bar);
    });
    menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition());
}

juce::PopupMenu ArrangementView::SectionStrip::createColourMenu (SectionId id)
{
    juce::PopupMenu menu;
    auto& editor = owner.engine.getTracks().sections();
    const auto& colours = SectionEditor::getSectionColours();
    const auto* s = find (id);

    for (size_t i = 0; i < colours.size(); ++i)
    {
        const auto c = colours[i];

        // A small rounded swatch beside each name.
        auto swatch = std::make_unique<juce::DrawablePath>();
        juce::Path shape;
        shape.addRoundedRectangle (0.0f, 0.0f, 14.0f, 14.0f, size::radiusTiny);
        swatch->setPath (shape);
        swatch->setFill (c);

        menu.addItem (juce::PopupMenu::Item (colourNames[(int) i].isNotEmpty() ? colourNames[(int) i] : "Color " + juce::String (i + 1))
                          .setImage (std::move (swatch))
                          .setTicked (s != nullptr && s->colour == c)
                          .setAction ([&editor, id, c] { editor.setSectionColour (id, c); }));
    }

    return menu;
}

//==============================================================================
void ArrangementView::SectionStrip::showNameField (SectionId id)
{
    auto* s = find (id);

    if (s == nullptr)
        return;

    editingId = id;
    const auto box = blockBounds (*s).translated ((float) headerWidth, 0.0f).toNearestInt();
    const int left = juce::jlimit (headerWidth, juce::jmax (headerWidth, getWidth() - 140), box.getX());
    nameField.setBounds (left, 1, juce::jmax (140, box.getRight() - left), getHeight() - 3);
    nameField.setText (s->name, false);
    nameField.setVisible (true);
    nameField.grabKeyboardFocus();
    nameField.selectAll();
}

void ArrangementView::SectionStrip::commitNameField()
{
    // Hiding the field takes its focus away, which calls onFocusLost and so
    // this again: editingId is 0 by then, so that call does nothing.
    if (editingId == 0)
        return;

    const auto id = editingId;
    const auto name = nameField.getText();
    hideNameField();
    owner.engine.getTracks().sections().renameSection (id, name);   // ignores empty or unchanged names
}

void ArrangementView::SectionStrip::hideNameField()
{
    editingId = 0;
    const bool hadFocus = nameField.hasKeyboardFocus (true);
    nameField.setVisible (false);

    if (hadFocus)
        grabKeyboardFocus();
}

} // namespace ikaros
