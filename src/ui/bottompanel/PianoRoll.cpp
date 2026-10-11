#include "ui/bottompanel/PianoRoll.h"
#include "engine/AudioEngine.h"
#include "engine/TrackController.h"
#include "ui/theme/Fonts.h"
#include "ui/theme/Tokens.h"

namespace ikaros
{

using namespace tokens;

namespace
{
    constexpr int keysWidth = 44;
    constexpr int toolbarHeight = 36;
    constexpr int velocityHeight = 52;
    constexpr int scrollBarSize = 10;
    constexpr int numRows = 128;
    constexpr int dragThreshold = 3;
    constexpr int resizeHandle = 6;          // pixels at a note's right end that resize it

    constexpr double minPixelsPerBeat = 8.0, maxPixelsPerBeat = 400.0;
    constexpr float minRowHeight = 6.0f, maxRowHeight = 28.0f;

    bool isBlackKey (int note)
    {
        const int n = note % 12;
        return n == 1 || n == 3 || n == 6 || n == 8 || n == 10;
    }

    bool sameNotes (const std::vector<NoteInfo>& a, const std::vector<NoteInfo>& b)
    {
        // std::equal with a lambda: compares the two lists element by element.
        return std::equal (a.begin(), a.end(), b.begin(), b.end(), [] (const NoteInfo& x, const NoteInfo& y)
        {
            return x.pitch == y.pitch && x.startBeat == y.startBeat
                && x.lengthBeats == y.lengthBeats && x.velocity == y.velocity;
        });
    }
}

//==============================================================================
PianoRoll::PianoRoll (AudioEngine& e) : engine (e)
{
    setTitle ("Piano roll");
    setWantsKeyboardFocus (true);

    auto buttonColours = IconButton::raisedColours();
    buttonColours.border = colour::border;

    for (auto* b : { &gridButton, &quantizeButton })
    {
        b->setColours (buttonColours);
        b->setFont (fonts::ui (12.0f));
        b->setCornerRadius (size::radiusSmall);
        b->setMouseClickGrabsKeyboardFocus (false);   // keep focus here, so Delete etc. still work
        addAndMakeVisible (b);
    }

    gridButton.setTooltip ("Where notes snap to when you draw, move or resize them");
    gridButton.onClick = [this] { showGridMenu(); };
    quantizeButton.setTooltip ("Line the selected notes (or all of them) up with the grid (Q)");
    quantizeButton.onClick = [this] { quantize(); };

    timeZoom.setTitle ("Zoom in time");
    timeZoom.setTooltip ("Zoom in time (Ctrl + mouse wheel)");
    timeZoom.setRange (0.0, 1.0);
    timeZoom.onValueChange = [this]
    {
        // Logarithmic: each step along the slider zooms by the same factor.
        setTimeZoom (minPixelsPerBeat * std::pow (maxPixelsPerBeat / minPixelsPerBeat, timeZoom.getValue()),
                     (float) gridArea.getCentreX());
    };

    noteZoom.setTitle ("Note height");
    noteZoom.setTooltip ("Note height (Alt + mouse wheel)");
    noteZoom.setRange (minRowHeight, maxRowHeight);
    noteZoom.onValueChange = [this] { setNoteZoom ((float) noteZoom.getValue(), (float) gridArea.getCentreY()); };

    for (auto* s : { &timeZoom, &noteZoom })
    {
        s->setMouseClickGrabsKeyboardFocus (false);
        addAndMakeVisible (s);
    }

    for (auto* bar : { &timeScroll, &noteScroll })
    {
        bar->setAutoHide (false);
        bar->addListener (this);
        addAndMakeVisible (bar);
    }

    setRegion (std::nullopt, colour::accent, 4.0);
}

PianoRoll::~PianoRoll()
{
    stopPreview();
    timeScroll.removeListener (this);
    noteScroll.removeListener (this);
}

//==============================================================================
void PianoRoll::setRegion (std::optional<RegionInfo> r, juce::Colour c, double bpb)
{
    const bool sameRegion = r.has_value() && region.has_value() && r->id == region->id;

    if (! sameRegion)
    {
        selected.clear();
        gesture = {};
        needsFit = true;
    }
    else
    {
        // The selection is by note ID, so it survives edits; just drop notes
        // that are gone (deleted, or undone).
        std::erase_if (selected, [&r] (NoteId id)
        {
            return std::none_of (r->notes.begin(), r->notes.end(), [id] (const NoteInfo& n) { return n.id == id; });
        });

        // A drag works on a copy of the notes as they were at mouse-down. If
        // they changed underneath it (an undo, recording), it can't go on.
        if (gesture.mode != Gesture::Mode::none && ! sameNotes (r->notes, region->notes))
        {
            gesture = {};
            stopPreview();
        }
    }

    region = std::move (r);
    colour = c;
    beatsPerBar = bpb;

    const bool editable = region.has_value() && region->isMidi;

    for (auto* comp : std::initializer_list<juce::Component*> { &gridButton, &quantizeButton, &timeZoom, &noteZoom,
                                                                &timeScroll, &noteScroll })
        comp->setVisible (editable);

    if (needsFit && ! gridArea.isEmpty())
        fitToRegion();

    clampView();
    repaint();
}

void PianoRoll::setPlayheadBeat (double editBeat)
{
    double rel = -1.0;

    if (region.has_value() && editBeat >= region->startBeat && editBeat < region->getEndBeat())
    {
        rel = editBeat - contentStartBeat();

        // A looped region plays its loop over and over: show where in the loop.
        if (region->isLooped())
            rel -= std::floor (rel / region->loopLengthBeats) * region->loopLengthBeats;
    }

    if (rel != playheadBeat)
    {
        playheadBeat = rel;
        repaint (gridArea);
    }
}

void PianoRoll::rereadRegion()
{
    // Called right after our own edits, so the notes on screen are always the
    // engine's.
    if (region.has_value())
        if (auto fresh = engine.getTracks().regions().getRegion (region->id))
            region = std::move (fresh);

    clampView();
    repaint();
}

//==============================================================================
// Coordinates
float PianoRoll::beatToX (double beat) const     { return (float) gridArea.getX() + (float) ((beat - scrollBeats) * pixelsPerBeat); }
double PianoRoll::xToBeat (float x) const        { return (x - (float) gridArea.getX()) / pixelsPerBeat + scrollBeats; }
float PianoRoll::pitchToY (int pitch) const      { return (float) gridArea.getY() + (float) ((127 - pitch) - topRow) * rowHeight; }
double PianoRoll::visibleBeats() const           { return gridArea.getWidth() / pixelsPerBeat; }
double PianoRoll::visibleRows() const            { return gridArea.getHeight() / (double) rowHeight; }
double PianoRoll::regionLength() const           { return region.has_value() ? juce::jmax (1.0, region->getContentLengthBeats()) : 1.0; }

// Where beat 0 of the notes is on the song's timeline. For a looped region
// that's the start of the first loop, which can be before the region starts.
double PianoRoll::contentStartBeat() const
{
    return region.has_value() ? region->startBeat - region->loopOffsetBeats : 0.0;
}

int PianoRoll::yToPitch (int y) const
{
    const auto row = (int) std::floor ((y - gridArea.getY()) / rowHeight + topRow);
    return juce::jlimit (0, 127, 127 - row);
}

juce::Rectangle<float> PianoRoll::noteBounds (const NoteInfo& n) const
{
    return { beatToX (n.startBeat), pitchToY (n.pitch) + 1.0f,
             juce::jmax (4.0f, (float) (n.lengthBeats * pixelsPerBeat) - 1.0f), rowHeight - 2.0f };
}

//==============================================================================
// Grid
double PianoRoll::gridBeats() const
{
    switch (grid)
    {
        case Grid::bar:          return beatsPerBar;
        case Grid::quarter:      return 1.0;
        case Grid::eighth:       return 0.5;
        case Grid::sixteenth:    return 0.25;
        case Grid::thirtySecond: return 0.125;
        case Grid::off:          break;
    }

    return 0.0;
}

double PianoRoll::snapToGrid (double beat, bool roundDown) const
{
    const double g = gridBeats();

    if (g <= 0.0 || ! region.has_value())
        return beat;

    // Snap on the song's timeline, so notes line up with the arrangement's
    // bars even when the region doesn't start on a bar.
    const double absolute = contentStartBeat() + beat;
    const double snapped = (roundDown ? std::floor (absolute / g + 1.0e-9) : std::round (absolute / g)) * g;
    return snapped - contentStartBeat();
}

juce::String PianoRoll::gridName() const
{
    switch (grid)
    {
        case Grid::bar:          return "Bar";
        case Grid::quarter:      return "1/4";
        case Grid::eighth:       return "1/8";
        case Grid::sixteenth:    return "1/16";
        case Grid::thirtySecond: return "1/32";
        case Grid::off:          break;
    }

    return "Off";
}

void PianoRoll::showGridMenu()
{
    juce::PopupMenu menu;
    menu.addSectionHeader ("Grid (note length)");

    auto add = [&] (const juce::String& name, Grid value)
    {
        menu.addItem (juce::PopupMenu::Item (name).setTicked (grid == value).setAction ([this, value]
        {
            grid = value;
            gridButton.setText ("Grid: " + gridName());

            if (gridBeats() > 0.0)
                lastNoteLength = gridBeats();

            repaint();
        }));
    };

    add ("Bar", Grid::bar);
    add ("1/4 (beat)", Grid::quarter);
    add ("1/8", Grid::eighth);
    add ("1/16", Grid::sixteenth);
    add ("1/32", Grid::thirtySecond);
    add ("Off", Grid::off);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&gridButton));
}

//==============================================================================
// View
void PianoRoll::fitToRegion()
{
    needsFit = false;

    if (! region.has_value() || gridArea.isEmpty())
        return;

    // The whole region across, and the notes (or middle C) in the middle.
    pixelsPerBeat = juce::jlimit (minPixelsPerBeat, maxPixelsPerBeat, gridArea.getWidth() / regionLength());
    scrollBeats = 0.0;
    rowHeight = 12.0f;

    int lo = 60, hi = 72;

    if (! region->notes.empty())
    {
        lo = 127;
        hi = 0;

        for (auto& n : region->notes)
        {
            lo = juce::jmin (lo, n.pitch);
            hi = juce::jmax (hi, n.pitch);
        }
    }

    // Rows tall enough to click comfortably (bigger if the notes' range fits
    // easily); a wide range is scrolled rather than squashed.
    rowHeight = juce::jlimit (10.0f, 16.0f, (float) gridArea.getHeight() / (float) (hi - lo + 5));
    topRow = (127 - (lo + hi) / 2.0) - visibleRows() / 2.0;
    clampView();
}

void PianoRoll::setTimeZoom (double newPixelsPerBeat, float anchorX)
{
    const double anchorBeat = xToBeat (anchorX);
    pixelsPerBeat = juce::jlimit (minPixelsPerBeat, maxPixelsPerBeat, newPixelsPerBeat);
    scrollBeats = anchorBeat - (anchorX - (float) gridArea.getX()) / pixelsPerBeat;
    clampView();
    repaint();
}

void PianoRoll::setNoteZoom (float newRowHeight, float anchorY)
{
    const double anchorRow = (anchorY - (float) gridArea.getY()) / rowHeight + topRow;
    rowHeight = juce::jlimit (minRowHeight, maxRowHeight, newRowHeight);
    topRow = anchorRow - (anchorY - (float) gridArea.getY()) / rowHeight;
    clampView();
    repaint();
}

void PianoRoll::clampView()
{
    scrollBeats = juce::jlimit (0.0, juce::jmax (0.0, regionLength() - visibleBeats()), scrollBeats);
    topRow = juce::jlimit (0.0, juce::jmax (0.0, numRows - visibleRows()), topRow);
    updateScrollBars();

    timeZoom.setValue (std::log (pixelsPerBeat / minPixelsPerBeat) / std::log (maxPixelsPerBeat / minPixelsPerBeat),
                       juce::dontSendNotification);
    noteZoom.setValue (rowHeight, juce::dontSendNotification);
}

void PianoRoll::updateScrollBars()
{
    timeScroll.setRangeLimits (0.0, juce::jmax (regionLength(), visibleBeats()), juce::dontSendNotification);
    timeScroll.setCurrentRange (scrollBeats, visibleBeats(), juce::dontSendNotification);
    noteScroll.setRangeLimits (0.0, juce::jmax ((double) numRows, visibleRows()), juce::dontSendNotification);
    noteScroll.setCurrentRange (topRow, visibleRows(), juce::dontSendNotification);
    noteScroll.setSingleStepSize (1.0);
}

void PianoRoll::scrollBarMoved (juce::ScrollBar* bar, double newRangeStart)
{
    if (bar == &timeScroll)
        scrollBeats = newRangeStart;
    else
        topRow = newRangeStart;

    clampView();
    repaint();
}

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
// Notes
const std::vector<NoteInfo>& PianoRoll::notesToDraw() const
{
    // During a move, resize or velocity drag, show the notes as they'll end up.
    if (gesture.mode == Gesture::Mode::move || gesture.mode == Gesture::Mode::resize
         || gesture.mode == Gesture::Mode::velocity)
        return gesture.notes;

    static const std::vector<NoteInfo> none;
    return region.has_value() ? region->notes : none;
}

int PianoRoll::noteAt (juce::Point<int> p) const
{
    if (! region.has_value() || ! gridArea.contains (p))
        return -1;

    // Last drawn is on top, so search backwards.
    for (int i = (int) region->notes.size(); --i >= 0;)
        if (noteBounds (region->notes[(size_t) i]).expanded (0.0f, 1.0f).contains (p.toFloat()))
            return i;

    return -1;
}

int PianoRoll::stemAt (juce::Point<int> p) const
{
    // The velocity stem nearest the mouse (within a few pixels). Selected
    // notes win, so stacked chord notes can be picked by selecting them first.
    if (! region.has_value() || ! velocityArea.contains (p))
        return -1;

    int best = -1;
    float bestDistance = 6.0f;

    for (int i = 0; i < (int) region->notes.size(); ++i)
    {
        const float distance = std::abs (beatToX (region->notes[(size_t) i].startBeat) - (float) p.x)
                                 - (isSelected (i) ? 0.5f : 0.0f);

        if (distance <= bestDistance)
        {
            bestDistance = distance;
            best = i;
        }
    }

    return best;
}

bool PianoRoll::isNearNoteEnd (int index, juce::Point<int> p) const
{
    auto bounds = noteBounds (region->notes[(size_t) index]);
    return bounds.getWidth() > resizeHandle * 2 && (float) p.x >= bounds.getRight() - (float) resizeHandle;
}

bool PianoRoll::isSelected (int index) const
{
    return std::binary_search (selected.begin(), selected.end(), region->notes[(size_t) index].id);
}

void PianoRoll::selectOnly (int index)
{
    selected = { region->notes[(size_t) index].id };
    repaint();
}

void PianoRoll::toggleSelected (int index)
{
    const auto id = region->notes[(size_t) index].id;

    if (isSelected (index))
        selected.erase (std::find (selected.begin(), selected.end(), id));
    else
        selected.insert (std::upper_bound (selected.begin(), selected.end(), id), id);   // keeps it sorted

    repaint();
}

void PianoRoll::selectAll()
{
    selected.clear();

    for (auto& n : region->notes)
        selected.push_back (n.id);

    std::sort (selected.begin(), selected.end());
    repaint();
}

std::vector<int> PianoRoll::selectedIndices() const
{
    std::vector<int> indices;

    if (region.has_value())
        for (int i = 0; i < (int) region->notes.size(); ++i)
            if (isSelected (i))
                indices.push_back (i);

    return indices;
}

void PianoRoll::applyChanges (const std::vector<NoteInfo>& changes, const juce::String& undoName)
{
    if (changes.empty() || ! region.has_value())
        return;

    // The selection is by ID, so it follows the notes wherever they move.
    engine.getTracks().notes().changeNotes (region->id, changes, undoName);
    rereadRegion();
}

void PianoRoll::deleteSelected()
{
    if (region.has_value() && ! selected.empty())
    {
        engine.getTracks().notes().deleteNotes (region->id, selected);
        selected.clear();
        rereadRegion();
    }
}

void PianoRoll::quantize()
{
    if (! region.has_value())
        return;

    // With the grid off, quantize to 1/16 notes.
    engine.getTracks().notes().quantiseNotes (region->id, selected, gridBeats() > 0.0 ? gridBeats() : 0.25);
    rereadRegion();
}

void PianoRoll::nudgeSelected (double beats, int semitones)
{
    std::vector<NoteInfo> changes;

    for (auto index : selectedIndices())
    {
        auto note = region->notes[(size_t) index];
        note.startBeat = juce::jmax (0.0, note.startBeat + beats);
        note.pitch = juce::jlimit (0, 127, note.pitch + semitones);
        changes.push_back (note);
    }

    applyChanges (changes, semitones != 0 ? "Transpose Notes" : "Move Notes");

    if (auto moved = selectedIndices(); semitones != 0 && moved.size() == 1)
    {
        // Let the user hear the new pitch.
        startPreview (region->notes[(size_t) moved.front()].pitch);
        juce::Timer::callAfterDelay (200, [safeThis = SafePointer<PianoRoll> (this)]
        {
            if (safeThis != nullptr && safeThis->gesture.mode == Gesture::Mode::none)
                safeThis->stopPreview();
        });
    }
}

void PianoRoll::showNoteMenu()
{
    juce::PopupMenu menu;
    menu.addItem (juce::PopupMenu::Item ("Delete").setEnabled (! selected.empty()).setAction ([this] { deleteSelected(); }));
    menu.addItem (juce::PopupMenu::Item (selected.empty() ? "Quantize All" : "Quantize")
                      .setAction ([this] { quantize(); }));
    menu.addSeparator();
    menu.addItem (juce::PopupMenu::Item ("Select All").setAction ([this] { selectAll(); }));

    menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition());
}

//==============================================================================
// Hearing notes: plays through the selected track's instrument, which is the
// region's track (selecting a region selects its track).
void PianoRoll::startPreview (int pitch, int velocity)
{
    stopPreview();

    // Don't play into a take that's being recorded.
    if (engine.getTransportState().recording)
        return;

    previewNote = pitch;
    engine.getTracks().inputs().noteOn (pitch, (float) velocity / 127.0f);
}

void PianoRoll::stopPreview()
{
    if (previewNote >= 0)
        engine.getTracks().inputs().noteOff (std::exchange (previewNote, -1));
}

void PianoRoll::focusLost (FocusChangeType)
{
    if (gesture.mode == Gesture::Mode::none)
        stopPreview();
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

//==============================================================================
// Layout and painting
void PianoRoll::resized()
{
    auto r = getLocalBounds();

    toolbarArea = r.removeFromTop (toolbarHeight);
    auto bar = toolbarArea.reduced (12, 6);
    gridButton.setBounds (bar.removeFromLeft (92));
    bar.removeFromLeft (6);
    quantizeButton.setBounds (bar.removeFromLeft (84));
    bar.removeFromLeft (14);

    noteZoom.setBounds (bar.removeFromRight (70));
    noteZoomIcon = bar.removeFromRight (20).withSizeKeepingCentre (16, 16);
    bar.removeFromRight (10);
    timeZoom.setBounds (bar.removeFromRight (90));
    timeZoomIcon = bar.removeFromRight (20).withSizeKeepingCentre (14, 14);
    bar.removeFromRight (10);
    hintArea = bar;

    auto bottom = r.removeFromBottom (scrollBarSize);
    timeScroll.setBounds (bottom.withTrimmedLeft (keysWidth).withTrimmedRight (scrollBarSize));

    auto velocity = r.removeFromBottom (velocityHeight);
    velocityLabelArea = velocity.removeFromLeft (keysWidth);
    velocityArea = velocity.withTrimmedRight (scrollBarSize);

    noteScroll.setBounds (r.removeFromRight (scrollBarSize));
    keysArea = r.removeFromLeft (keysWidth);
    gridArea = r;

    if (needsFit)
        fitToRegion();

    clampView();
}

void PianoRoll::paint (juce::Graphics& g)
{
    g.fillAll (colour::windowBg);

    if (! region.has_value() || ! region->isMidi)
    {
        g.setColour (colour::textSecondary);
        g.setFont (fonts::ui (14.0f));
        g.drawText ("Select a MIDI region to edit it here. Double-click an empty lane to create one.",
                    getLocalBounds(), juce::Justification::centred);
        return;
    }

    paintToolbar (g);
    paintGrid (g);
    paintNotes (g);
    paintKeys (g);
    paintVelocities (g);
}

void PianoRoll::paintToolbar (juce::Graphics& g)
{
    g.setColour (colour::toolbarBg);
    g.fillRect (toolbarArea);
    g.setColour (colour::divider);
    g.fillRect (toolbarArea.removeFromBottom (1));

    g.setColour (colour::textSecondary);
    g.setFont (fonts::ui (12.0f));
    g.drawFittedText (juce::String::fromUTF8 ("Click to add a note \xc2\xb7 Drag to move \xc2\xb7 Drag the end to resize \xc2\xb7 Shift-drag to select"),
                      hintArea, juce::Justification::centredLeft, 1, 1.0f);

    icons::draw (g, Icon::search, timeZoomIcon.toFloat(), colour::textSecondary, 13.0f, 1.6f);
    icons::draw (g, Icon::keyboards, noteZoomIcon.toFloat(), colour::textSecondary, 15.0f, 1.6f);
}

void PianoRoll::paintKeys (juce::Graphics& g)
{
    juce::Graphics::ScopedSaveState save (g);
    g.reduceClipRegion (keysArea);

    g.setColour (colour::sidebarBg);
    g.fillRect (keysArea);

    const int firstPitch = yToPitch (gridArea.getBottom());
    const int lastPitch = yToPitch (gridArea.getY());

    for (int note = firstPitch; note <= lastPitch; ++note)
    {
        auto row = juce::Rectangle<float> ((float) keysArea.getX(), pitchToY (note), (float) keysArea.getWidth(), rowHeight);
        const bool down = note == previewNote;

        g.setColour (down ? colour::accent : isBlackKey (note) ? colour::wellBg : juce::Colour (0xffc9d0da));
        g.fillRect (row.reduced (0.0f, 0.5f));

        if (note % 12 == 0 && rowHeight >= 8.0f)
        {
            g.setColour (colour::pianoLabel);
            g.setFont (fonts::ui (juce::jmin (10.0f, rowHeight - 1.0f)));
            g.drawText ("C" + juce::String (note / 12 - 1), row.withTrimmedRight (3.0f), juce::Justification::centredRight);
        }
    }
}

void PianoRoll::paintGrid (juce::Graphics& g)
{
    juce::Graphics::ScopedSaveState save (g);
    g.reduceClipRegion (gridArea);
    auto area = gridArea.toFloat();

    // Rows: darker for black keys, a faint line between rows.
    const int firstPitch = yToPitch (gridArea.getBottom());
    const int lastPitch = yToPitch (gridArea.getY());

    for (int note = firstPitch; note <= lastPitch; ++note)
    {
        const float y = pitchToY (note);

        if (isBlackKey (note))
        {
            g.setColour (juce::Colours::black.withAlpha (0.18f));
            g.fillRect (area.getX(), y, area.getWidth(), rowHeight);
        }

        g.setColour (juce::Colours::white.withAlpha (note % 12 == 0 ? 0.07f : 0.035f));
        g.fillRect (area.getX(), y + rowHeight - 1.0f, area.getWidth(), 1.0f);
    }

    // Vertical lines on the song's timeline: grid steps (when they're far
    // enough apart to see), beats, and bars.
    const double regionStart = contentStartBeat();
    auto drawLines = [&] (double step, float alpha)
    {
        if (step <= 0.0 || step * pixelsPerBeat < 6.0)
            return;

        const double first = std::ceil ((regionStart + scrollBeats) / step) * step;

        for (double beat = first; beat - regionStart <= scrollBeats + visibleBeats(); beat += step)
        {
            g.setColour (juce::Colours::white.withAlpha (alpha));
            g.fillRect (beatToX (beat - regionStart), area.getY(), 1.0f, area.getHeight());
        }
    };

    drawLines (gridBeats(), 0.025f);
    drawLines (1.0, 0.04f);
    drawLines (beatsPerBar, 0.1f);

    // Past the end of the region (or of the loop): shaded.
    const float endX = beatToX (region->getContentLengthBeats());

    if (endX < area.getRight())
    {
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.fillRect (area.withLeft (endX));
    }

    if (playheadBeat >= 0.0)
    {
        g.setColour (colour::accent);
        g.fillRect (beatToX (playheadBeat) - 1.0f, area.getY(), 2.0f, area.getHeight());
    }
}

void PianoRoll::paintNotes (juce::Graphics& g)
{
    juce::Graphics::ScopedSaveState save (g);
    g.reduceClipRegion (gridArea);

    auto& notes = notesToDraw();
    const float corner = juce::jmin (4.0f, rowHeight * 0.3f);

    for (int i = 0; i < (int) notes.size(); ++i)
    {
        auto& n = notes[(size_t) i];
        auto bounds = noteBounds (n);

        if (! bounds.intersects (gridArea.toFloat()))
            continue;

        const bool sel = isSelected (i);
        auto fill = colour.withMultipliedAlpha (0.55f + 0.45f * (float) n.velocity / 127.0f);
        g.setColour (sel ? fill.brighter (0.35f) : fill);
        g.fillRoundedRectangle (bounds, corner);

        if (sel)
        {
            g.setColour (juce::Colours::white);
            g.drawRoundedRectangle (bounds.reduced (0.5f), corner, 1.5f);
        }
        else
        {
            g.setColour (juce::Colours::black.withAlpha (0.35f));
            g.drawRoundedRectangle (bounds, corner, 1.0f);
        }
    }

    // The note being drawn.
    if (gesture.mode == Gesture::Mode::draw)
    {
        auto bounds = noteBounds (gesture.newNote);
        g.setColour (colour.withAlpha (0.6f));
        g.fillRoundedRectangle (bounds, corner);
        g.setColour (juce::Colours::white);
        g.drawRoundedRectangle (bounds.reduced (0.5f), corner, 1.5f);
    }

    if (gesture.mode == Gesture::Mode::marquee)
    {
        g.setColour (colour::accent.withAlpha (0.12f));
        g.fillRect (gesture.marquee);
        g.setColour (colour::accent);
        g.drawRect (gesture.marquee, 1);
    }
}

void PianoRoll::paintVelocities (juce::Graphics& g)
{
    g.setColour (colour::cardBg);
    g.fillRect (velocityLabelArea.getUnion (velocityArea));
    g.setColour (colour::divider);
    g.fillRect (velocityLabelArea.getUnion (velocityArea).removeFromTop (1));

    g.setColour (colour::textSecondary);
    g.setFont (fonts::ui (10.0f));
    g.drawText ("Vel.", velocityLabelArea.reduced (4, 0), juce::Justification::centred);

    juce::Graphics::ScopedSaveState save (g);
    g.reduceClipRegion (velocityArea);

    // A stem per note: taller = louder.
    const auto lane = velocityArea.reduced (0, 4).toFloat();
    auto& notes = notesToDraw();

    for (int i = 0; i < (int) notes.size(); ++i)
    {
        auto& n = notes[(size_t) i];
        const float x = beatToX (n.startBeat);

        if (x < lane.getX() - 4.0f || x > lane.getRight() + 4.0f)
            continue;

        const float top = lane.getBottom() - lane.getHeight() * (float) n.velocity / 127.0f;
        const bool sel = isSelected (i);
        g.setColour (sel ? colour.brighter (0.4f) : colour.withAlpha (0.7f));
        g.fillRect (x, top, 2.0f, lane.getBottom() - top);
        g.fillEllipse (x - 2.5f, top - 2.5f, 7.0f, 7.0f);
    }
}

} // namespace ikaros
