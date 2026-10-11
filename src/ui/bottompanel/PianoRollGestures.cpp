// The piano roll's mouse, wheel and keyboard handling: clicking, drawing and
// dragging notes, the velocity lane, the selection box, zooming and
// scrolling, and the keys. A drag works on a copy of the notes
// (Gesture::notes) and only reaches the session on mouse-up.

#include "ui/bottompanel/PianoRoll.h"
#include "ui/bottompanel/PianoRollLayout.h"
#include "engine/AudioEngine.h"
#include "engine/TrackController.h"

namespace ikaros
{

using namespace pianoroll;

//==============================================================================
// Wheel: zoom and scroll
void PianoRoll::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (! region.has_value() || ! region->isMidi)
        return;

    const auto factor = std::pow (2.0, (double) wheel.deltaY * 2.5);   // one notch ≈ 1.4×

    if (e.mods.isAltDown() || (e.mods.isCommandDown() && e.mods.isShiftDown()))
        setNoteZoom (rowHeight * (float) factor, (float) e.y);         // Alt (or Ctrl+Shift): note height
    else if (e.mods.isCommandDown())
        setTimeZoom (pixelsPerBeat * factor, (float) e.x);              // Ctrl/Cmd: zoom in time
    else if (e.mods.isShiftDown() || std::abs (wheel.deltaX) > std::abs (wheel.deltaY))
    {
        const float amount = wheel.deltaX != 0.0f ? wheel.deltaX : wheel.deltaY;
        scrollBeats -= (wheel.isReversed ? -amount : amount) * 300.0 / pixelsPerBeat;
        clampView();
        repaint();
    }
    else
    {
        topRow -= (wheel.isReversed ? -wheel.deltaY : wheel.deltaY) * 20.0;   // about 4 rows per notch
        clampView();
        repaint();
    }
}

//==============================================================================
// Mouse
void PianoRoll::mouseMove (const juce::MouseEvent& e)
{
    auto cursor = juce::MouseCursor::NormalCursor;

    if (auto i = noteAt (e.getPosition()); i >= 0)
        cursor = isNearNoteEnd (i, e.getPosition()) ? juce::MouseCursor::LeftRightResizeCursor
                                                    : juce::MouseCursor::DraggingHandCursor;
    else if (gridArea.contains (e.getPosition()))
        cursor = juce::MouseCursor::CrosshairCursor;
    else if (stemAt (e.getPosition()) >= 0)
        cursor = juce::MouseCursor::UpDownResizeCursor;
    else if (keysArea.contains (e.getPosition()))
        cursor = juce::MouseCursor::PointingHandCursor;

    setMouseCursor (cursor);
}

void PianoRoll::mouseDown (const juce::MouseEvent& e)
{
    if (! region.has_value() || ! region->isMidi)
        return;

    grabKeyboardFocus();
    gesture = {};
    gesture.start = e.getPosition();
    const auto pos = e.getPosition();

    if (keysArea.contains (pos))
    {
        gesture.mode = Gesture::Mode::previewKey;
        startPreview (yToPitch (pos.y));
        return;
    }

    if (velocityArea.contains (pos))
    {
        const int stem = stemAt (pos);

        if (stem < 0)
            return;

        if (! isSelected (stem))
            selectOnly (stem);

        gesture.mode = Gesture::Mode::velocity;
        gesture.anchor = stem;
        gesture.notes = region->notes;
        updateVelocity (pos);
        return;
    }

    if (! gridArea.contains (pos))
        return;

    const int hit = noteAt (pos);

    if (e.mods.isPopupMenu())
    {
        if (hit >= 0 && ! isSelected (hit))
            selectOnly (hit);

        showNoteMenu();
        return;
    }

    if (hit >= 0)
    {
        if (e.mods.isShiftDown() || e.mods.isCommandDown())
        {
            toggleSelected (hit);

            if (! isSelected (hit))
                return;   // just deselected: nothing to drag
        }
        else if (! isSelected (hit))
        {
            selectOnly (hit);
        }
        else
        {
            // Part of a selection: keep it so the group can be dragged; a
            // plain click (no drag) selects just this note on mouse-up.
            gesture.selectOnlyAnchorOnClick = selected.size() > 1;
        }

        gesture.anchor = hit;
        gesture.notes = region->notes;
        gesture.mode = isNearNoteEnd (hit, pos) ? Gesture::Mode::resize : Gesture::Mode::pendingMove;

        auto& note = region->notes[(size_t) hit];
        startPreview (note.pitch, note.velocity);
        return;
    }

    if (e.mods.isShiftDown())
    {
        // Shift-drag on empty space: draw a box to select notes.
        gesture.mode = Gesture::Mode::marquee;
        gesture.marquee = { pos, pos };
        return;
    }

    // Click on empty space: draw a new note, starting at the grid line to
    // the left of the mouse. Dragging right makes it longer.
    const double start = juce::jmax (0.0, snapToGrid (xToBeat ((float) pos.x), true));

    if (start >= region->getContentLengthBeats())
        return;

    selected.clear();
    gesture.mode = Gesture::Mode::draw;
    gesture.newNote = { yToPitch (pos.y), start, juce::jmin (lastNoteLength, region->getContentLengthBeats() - start), 100 };
    startPreview (gesture.newNote.pitch);
    repaint();
}

void PianoRoll::mouseDrag (const juce::MouseEvent& e)
{
    const auto pos = e.getPosition();

    switch (gesture.mode)
    {
        case Gesture::Mode::previewKey:
            if (yToPitch (pos.y) != previewNote)
                startPreview (yToPitch (pos.y));   // glide along the keys
            break;

        case Gesture::Mode::draw:
        {
            // Drag right to make the new note longer (at least one grid step).
            const double step = gridBeats() > 0.0 ? gridBeats() : 1.0 / 32.0;
            const double end = juce::jmax (gesture.newNote.startBeat + step, snapToGrid (xToBeat ((float) pos.x), false));
            gesture.newNote.lengthBeats = juce::jmin (end, region->getContentLengthBeats()) - gesture.newNote.startBeat;
            repaint();
            break;
        }

        case Gesture::Mode::pendingMove:
            if (pos.getDistanceFrom (gesture.start) < dragThreshold)
                break;

            gesture.mode = Gesture::Mode::move;
            [[fallthrough]];   // the move starts with this same mouse position

        case Gesture::Mode::move:      updateMove (pos); break;
        case Gesture::Mode::resize:    updateResize (pos); break;
        case Gesture::Mode::velocity:  updateVelocity (pos); break;
        case Gesture::Mode::marquee:   updateMarquee (pos); break;
        case Gesture::Mode::none:      break;
    }
}

void PianoRoll::updateMove (juce::Point<int> pos)
{
    auto& original = region->notes;
    auto& anchor = original[(size_t) gesture.anchor];

    // Time: snap the clicked note's start; the others keep their spacing.
    double delta = xToBeat ((float) pos.x) - xToBeat ((float) gesture.start.x);
    delta = snapToGrid (anchor.startBeat + delta, false) - anchor.startBeat;

    // Pitch: whole rows. Keep every selected note inside the region and 0..127.
    int semitones = yToPitch (pos.y) - yToPitch (gesture.start.y);
    double earliest = anchor.startBeat;

    for (auto index : selectedIndices())
    {
        auto& n = original[(size_t) index];
        earliest = juce::jmin (earliest, n.startBeat);
        semitones = juce::jlimit (-n.pitch, 127 - n.pitch, semitones);
    }

    if (earliest >= 0.0)
        delta = juce::jmax (delta, -earliest);

    gesture.notes = original;

    for (auto index : selectedIndices())
    {
        gesture.notes[(size_t) index].startBeat += delta;
        gesture.notes[(size_t) index].pitch += semitones;
    }

    // Hear the clicked note at its new pitch.
    const int newPitch = anchor.pitch + semitones;

    if (newPitch != previewNote)
        startPreview (newPitch, anchor.velocity);

    repaint();
}

void PianoRoll::updateResize (juce::Point<int> pos)
{
    auto& original = region->notes;
    auto& anchor = original[(size_t) gesture.anchor];
    const double minLength = gridBeats() > 0.0 ? gridBeats() : 1.0 / 32.0;

    // Snap the clicked note's end; the others change length by the same amount.
    double delta = xToBeat ((float) pos.x) - xToBeat ((float) gesture.start.x);
    delta = snapToGrid (anchor.startBeat + anchor.lengthBeats + delta, false) - (anchor.startBeat + anchor.lengthBeats);

    gesture.notes = original;

    for (auto index : selectedIndices())
    {
        auto& n = gesture.notes[(size_t) index];
        n.lengthBeats = juce::jmax (juce::jmin (minLength, original[(size_t) index].lengthBeats), n.lengthBeats + delta);
    }

    repaint();
}

void PianoRoll::updateVelocity (juce::Point<int> pos)
{
    // The height in the lane is the velocity. With several notes selected,
    // they all change by the same amount as the clicked one.
    const auto lane = velocityArea.reduced (0, 4);
    const int value = juce::jlimit (1, 127, juce::roundToInt ((lane.getBottom() - pos.y) * 127.0f / (float) lane.getHeight()));
    const int delta = value - region->notes[(size_t) gesture.anchor].velocity;

    gesture.notes = region->notes;

    for (auto index : selectedIndices())
        gesture.notes[(size_t) index].velocity = juce::jlimit (1, 127, region->notes[(size_t) index].velocity + delta);

    repaint();
}

void PianoRoll::updateMarquee (juce::Point<int> pos)
{
    gesture.marquee = juce::Rectangle<int> (gesture.start, pos);
    selected.clear();

    for (auto& n : region->notes)
        if (noteBounds (n).intersects (gesture.marquee.toFloat()))
            selected.push_back (n.id);

    std::sort (selected.begin(), selected.end());
    repaint();
}

void PianoRoll::mouseUp (const juce::MouseEvent&)
{
    auto finished = std::exchange (gesture, {});
    stopPreview();

    if (! region.has_value())
        return;

    auto changesFrom = [&] (const std::vector<NoteInfo>& notes)
    {
        std::vector<NoteInfo> changes;

        for (auto index : selectedIndices())
            changes.push_back (notes[(size_t) index]);   // each keeps its ID: that says which note it is

        return changes;
    };

    switch (finished.mode)
    {
        case Gesture::Mode::draw:
        {
            const auto added = engine.getTracks().notes().addNote (region->id, finished.newNote);
            lastNoteLength = finished.newNote.lengthBeats;
            selected.clear();

            if (added != 0)
                selected.push_back (added);

            rereadRegion();
            break;
        }

        case Gesture::Mode::move:
            if (! sameNotes (finished.notes, region->notes))
                applyChanges (changesFrom (finished.notes), selected.size() == 1 ? "Move Note" : "Move Notes");
            break;

        case Gesture::Mode::resize:
            if (! sameNotes (finished.notes, region->notes))
            {
                lastNoteLength = finished.notes[(size_t) finished.anchor].lengthBeats;
                applyChanges (changesFrom (finished.notes), selected.size() == 1 ? "Resize Note" : "Resize Notes");
            }
            break;

        case Gesture::Mode::velocity:
            if (! sameNotes (finished.notes, region->notes))
                applyChanges (changesFrom (finished.notes), "Velocity");
            break;

        case Gesture::Mode::pendingMove:
            if (finished.selectOnlyAnchorOnClick)
                selectOnly (finished.anchor);
            break;

        case Gesture::Mode::previewKey:
        case Gesture::Mode::marquee:
        case Gesture::Mode::none:
            break;
    }

    repaint();
}

//==============================================================================
bool PianoRoll::keyPressed (const juce::KeyPress& key)
{
    // Escape in the middle of a drag cancels it. Drags only change the
    // roll's own copy of the notes until the mouse is released (see
    // mouseUp), so forgetting the gesture puts everything back and nothing
    // reaches the session.
    if (key == juce::KeyPress::escapeKey)
    {
        using Mode = Gesture::Mode;
        const auto mode = gesture.mode;

        if (mode == Mode::draw || mode == Mode::pendingMove || mode == Mode::move
             || mode == Mode::resize || mode == Mode::velocity)
        {
            gesture = {};
            stopPreview();
            repaint();
            return true;
        }
    }

    if (! region.has_value() || ! region->isMidi)
        return false;

    const auto mods = key.getModifiers();
    const double step = gridBeats() > 0.0 ? gridBeats() : 0.25;

    // The Delete key always stays here while the roll has focus: passing it
    // on would delete the selected *region* instead.
    if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)
    {
        deleteSelected();
        return true;
    }

    if (key == juce::KeyPress ('a', juce::ModifierKeys::commandModifier, 0))
    {
        selectAll();
        return true;
    }

    if (key == juce::KeyPress::escapeKey && ! selected.empty())
    {
        selected.clear();
        repaint();
        return true;
    }

    if (key == juce::KeyPress ('q'))   // KeyPress compares letters case-insensitively
    {
        quantize();
        return true;
    }

    if (! selected.empty() && ! mods.isCommandDown() && ! mods.isAltDown())
    {
        const int octave = mods.isShiftDown() ? 12 : 1;

        if (key.isKeyCode (juce::KeyPress::upKey))     { nudgeSelected (0.0, octave);  return true; }
        if (key.isKeyCode (juce::KeyPress::downKey))   { nudgeSelected (0.0, -octave); return true; }
        if (key.isKeyCode (juce::KeyPress::leftKey))   { nudgeSelected (-step, 0);     return true; }
        if (key.isKeyCode (juce::KeyPress::rightKey))  { nudgeSelected (step, 0);      return true; }
    }

    return false;   // e.g. Space, Ctrl+Z: on to the app's shortcuts
}

} // namespace ikaros
