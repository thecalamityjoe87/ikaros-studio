#include "ui/bottompanel/PianoRoll.h"
#include "ui/bottompanel/PianoRollLayout.h"
#include "engine/AudioEngine.h"
#include "engine/TrackController.h"
#include "ui/theme/Fonts.h"
#include "ui/theme/Tokens.h"

namespace ikaros
{

using namespace tokens;
using namespace pianoroll;

//==============================================================================
PianoRoll::PianoRoll (AudioEngine& e) : engine (e)
{
    setTitle ("Piano roll");
    setWantsKeyboardFocus (true);

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

    setRegion (std::nullopt, colour::master, TempoMap());
}

PianoRoll::~PianoRoll()
{
    stopPreview();
    timeScroll.removeListener (this);
    noteScroll.removeListener (this);
}

//==============================================================================
void PianoRoll::setRegion (std::optional<RegionInfo> r, juce::Colour c, const TempoMap& t)
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
    tempo = t;

    const bool editable = region.has_value() && region->isMidi;

    for (auto* comp : std::initializer_list<juce::Component*> { &timeZoom, &noteZoom,
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
    // ikaros:: picks EditorGrid.h's gridBeats rather than this member function.
    // As a step (a new note's length, say), the grid is measured in the bar
    // the region starts in (bars and beats can differ from bar to bar).
    return ikaros::gridBeats (grid, tempo, region.has_value() ? contentStartBeat() : 0.0);
}

void PianoRoll::setGrid (EditorGrid newGrid)
{
    grid = newGrid;

    // New notes get one grid step long.
    if (gridBeats() > 0.0)
        lastNoteLength = gridBeats();

    repaint();
}

void PianoRoll::setQuantizeFeel (double strength, double swing)
{
    quantizeStrength = strength;
    quantizeSwing = swing;
}

double PianoRoll::snapToGrid (double beat, bool roundDown) const
{
    if (gridBeats() <= 0.0 || ! region.has_value())
        return beat;

    // Snap on the song's timeline, so notes line up with the arrangement's
    // bars even when the region doesn't start on a bar.
    const double absolute = contentStartBeat() + beat;
    return snapToEditorGrid (grid, tempo, absolute, roundDown) - contentStartBeat();
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
    engine.getTracks().notes().quantiseNotes (region->id, selected, quantizeSettingsFor (grid, quantizeStrength, quantizeSwing));
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
// Layout (painting is in PianoRollPainting.cpp, the mouse and keys in
// PianoRollGestures.cpp)
void PianoRoll::resized()
{
    auto r = getLocalBounds();

    toolbarArea = r.removeFromTop (toolbarHeight);
    auto bar = toolbarArea.reduced (12, 6);

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

} // namespace ikaros
