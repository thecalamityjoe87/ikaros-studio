// The audio editor's Pitch mode: the sung notes of an audio region, drawn
// and edited in place of its waveform. Part of AudioEditor (see
// AudioEditor.h); split out like the piano roll's painting and gestures.

#include "ui/bottompanel/AudioEditor.h"
#include "engine/AudioEngine.h"
#include "engine/TrackController.h"
#include "ui/theme/Fonts.h"
#include "ui/theme/Tokens.h"

namespace ikaros
{

using namespace tokens;

namespace
{
    constexpr int noteColumnWidth = 46;        // the note names on the left
    constexpr float maxRowHeight = 15.0f, minRowHeight = 6.0f;
    constexpr float blobHeight = 12.0f;
    constexpr float waveformShare = 0.16f;     // the faint waveform: this much of the height, at the bottom
    constexpr float centsLabelFrom = 0.12f;    // notes further off than this (semitones) show their cents

    const juce::String minusSign = juce::String::fromUTF8 ("\xe2\x88\x92");
    const juce::String centSign = juce::String::fromUTF8 ("\xc2\xa2");
    const juce::String dot = juce::String::fromUTF8 (" \xc2\xb7 ");

    // The app's note names (as in key names: flats for the black keys but F#).
    juce::String noteName (int note)
    {
        static const char* const names[] { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
        return names[((note % 12) + 12) % 12];
    }

    juce::String centsText (float semitones)
    {
        const int cents = juce::roundToInt (semitones * 100.0f);
        return (cents > 0 ? "+" : cents < 0 ? minusSign : juce::String()) + juce::String (std::abs (cents)) + centSign;
    }

    bool sameStart (double a, double b)   { return std::abs (a - b) < 0.003; }
}

//==============================================================================
// State
void AudioEditor::refreshPitch()
{
    const auto before = pitch;
    pitch = hasRegion() ? engine.getTracks().pitch().getPitch (region->id) : PitchInfo();

    // Notes that aren't there any more (the analysis changed) can't stay selected.
    const auto selectedBefore = pitchSelected;
    pitchSelected.erase (std::remove_if (pitchSelected.begin(), pitchSelected.end(), [this] (double s)
    {
        return std::none_of (pitch.notes.begin(), pitch.notes.end(), [s] (const PitchNoteInfo& n) { return sameStart (n.sourceStart, s); });
    }), pitchSelected.end());

    if (pitchDrag.mode == PitchDrag::Mode::none && (pitch.notes.size() != before.notes.size() || pitch.transpose != before.transpose))
        fitPitchRows();

    // The inspector shows the selected notes' values: tell it when they (or the notes) change.
    const bool ready = pitch.analysed;

    if ((pitchSelected != selectedBefore || ready != pitchWasReady || pitch.notes.size() != before.notes.size()) && onPitchSelectionChanged)
        onPitchSelectionChanged (pitchSelected);

    pitchWasReady = ready;
    refreshModeSwitch();
}

void AudioEditor::fitPitchRows()
{
    // The rows: the notes' range plus a little either side, and enough rows
    // to fill the height (about 15 px each), centred on the notes.
    float low = 1000.0f, high = -1000.0f;

    for (auto& n : pitch.notes)
    {
        for (float v : { n.sung, n.corrected })
        {
            low = juce::jmin (low, v);
            high = juce::jmax (high, v);
        }

        for (auto& [beat, p] : n.curve)
        {
            low = juce::jmin (low, p);
            high = juce::jmax (high, p);
        }
    }

    if (low > high)
        low = high = 60.0f;

    int lowNote = (int) std::floor (low) - 2, highNote = (int) std::ceil (high) + 2;
    const float height = (float) waveArea.getHeight() * (1.0f - waveformShare);
    const int fits = juce::jmax (12, (int) (height / maxRowHeight));

    while (highNote - lowNote + 1 < fits)
    {
        --lowNote;

        if (highNote - lowNote + 1 < fits)
            ++highNote;
    }

    pitchLow = juce::jlimit (0, 120, lowNote);
    pitchHigh = juce::jlimit (pitchLow + 11, 127, highNote);
    pitchRowHeight = juce::jlimit (minRowHeight, maxRowHeight, height / (float) (pitchHigh - pitchLow + 1));
}

juce::String AudioEditor::pitchStatus() const
{
    auto percent = [] (float f) { return " " + juce::String (juce::roundToInt (f * 100.0f)) + "%"; };

    if (pitch.analysing)
        return juce::String::fromUTF8 ("Finding the notes\xe2\x80\xa6") + percent (pitch.analysisProgress);

    if (pitch.rendering)
        return juce::String::fromUTF8 ("Rendering\xe2\x80\xa6") + percent (pitch.renderProgress);

    return {};
}

//==============================================================================
// Coordinates
juce::Rectangle<int> AudioEditor::pitchColumn() const
{
    return waveArea.withWidth (noteColumnWidth);
}

float AudioEditor::noteToY (float note) const
{
    // The top row is pitchHigh; each row is centred on its note.
    return (float) waveArea.getY() + ((float) pitchHigh - note + 0.5f) * pitchRowHeight;
}

float AudioEditor::shownCentre (const PitchNoteInfo& n) const
{
    if (pitchDrag.mode == PitchDrag::Mode::move && isPitchSelected (n.sourceStart))
        return n.corrected + pitchDrag.semitones;

    return n.corrected;
}

juce::Rectangle<float> AudioEditor::pitchNoteBounds (const PitchNoteInfo& n, float centre) const
{
    const float left = beatToX (n.startBeat - region->startBeat), right = beatToX (n.endBeat - region->startBeat);
    const float h = juce::jmin (blobHeight, pitchRowHeight * 0.85f);
    return { left, noteToY (centre) - h * 0.5f, juce::jmax (3.0f, right - left), h };
}

int AudioEditor::pitchNoteAt (juce::Point<int> p) const
{
    // Topmost first (the last drawn), with a little slack round each note.
    for (int i = (int) pitch.notes.size(); --i >= 0;)
        if (pitchNoteBounds (pitch.notes[(size_t) i], shownCentre (pitch.notes[(size_t) i])).expanded (2.0f, 3.0f).contains (p.toFloat()))
            return i;

    return -1;
}

bool AudioEditor::isPitchSelected (double sourceStart) const
{
    return std::any_of (pitchSelected.begin(), pitchSelected.end(), [sourceStart] (double s) { return sameStart (s, sourceStart); });
}

void AudioEditor::setPitchSelection (std::vector<double> notes)
{
    if (notes == pitchSelected)
        return;

    pitchSelected = std::move (notes);

    if (onPitchSelectionChanged)
        onPitchSelectionChanged (pitchSelected);

    repaint();
}

//==============================================================================
// Mouse
void AudioEditor::pitchMouseDown (const juce::MouseEvent& e)
{
    pitchDrag = {};
    pitchDrag.start = e.getPosition();
    const int index = pitch.analysed && pitch.singleVoice ? pitchNoteAt (e.getPosition()) : -1;

    if (index < 0)
    {
        // Empty space: a click moves the playhead and drops the selection,
        // a drag draws a selection box (Shift adds to the selection).
        pitchDrag.mode = PitchDrag::Mode::box;
        pitchDrag.selectedBefore = e.mods.isShiftDown() ? pitchSelected : std::vector<double>();

        if (! e.mods.isShiftDown())
            setPitchSelection ({});

        engine.setPositionBeats (juce::jmax (0.0, region->startBeat + xToBeat ((float) e.x)));
        return;
    }

    const auto start = pitch.notes[(size_t) index].sourceStart;
    pitchFocus = start;

    if (e.mods.isShiftDown())
    {
        // Shift-click: in or out of the selection.
        auto chosen = pitchSelected;

        if (isPitchSelected (start))
            chosen.erase (std::remove_if (chosen.begin(), chosen.end(), [start] (double s) { return sameStart (s, start); }), chosen.end());
        else
            chosen.push_back (start);

        setPitchSelection (chosen);
        return;
    }

    // Grabbing a note that isn't selected selects just it.
    if (! isPitchSelected (start))
        setPitchSelection ({ start });

    pitchDrag.mode = PitchDrag::Mode::move;
    repaint();
}

void AudioEditor::pitchMouseDrag (const juce::MouseEvent& e)
{
    if (pitchDrag.mode == PitchDrag::Mode::move)
    {
        // Whole semitones, or cents with Alt held (each drag can switch).
        const float rows = (float) (pitchDrag.start.y - e.y) / pitchRowHeight;
        pitchDrag.fine = e.mods.isAltDown();
        pitchDrag.semitones = pitchDrag.fine ? std::round (rows * 100.0f) / 100.0f : std::round (rows);
        setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
        repaint();
    }
    else if (pitchDrag.mode == PitchDrag::Mode::box)
    {
        pitchDrag.box = juce::Rectangle<int> (pitchDrag.start, e.getPosition()).getIntersection (waveArea);

        // What was selected before (with Shift), plus what's in the box.
        // (setPitchSelection compares, so it only tells the inspector when it changes.)
        std::vector<double> inBox = pitchDrag.selectedBefore;

        for (auto& n : pitch.notes)
            if (pitchNoteBounds (n, shownCentre (n)).intersects (pitchDrag.box.toFloat())
                 && std::none_of (inBox.begin(), inBox.end(), [&] (double s) { return sameStart (s, n.sourceStart); }))
                inBox.push_back (n.sourceStart);

        setPitchSelection (inBox);
        repaint();
    }
}

void AudioEditor::pitchMouseUp (const juce::MouseEvent&)
{
    const auto finished = std::exchange (pitchDrag, {});
    setMouseCursor (juce::MouseCursor::NormalCursor);

    // A finished move is one undo step ("Move Note"); the session's change
    // message brings the notes back where they now are.
    if (finished.mode == PitchDrag::Mode::move && finished.semitones != 0.0f)
    {
        engine.getTracks().pitch().moveNotes (region->id, pitchSelected, finished.semitones);
        refreshPitch();
    }

    repaint();
}

void AudioEditor::pitchDoubleClick (const juce::MouseEvent& e)
{
    // Double-clicking a note centres it on its semitone ("Center Pitch").
    if (const int index = pitchNoteAt (e.getPosition()); index >= 0)
    {
        pitchDrag = {};
        engine.getTracks().pitch().centreNotes (region->id, { pitch.notes[(size_t) index].sourceStart });
        refreshPitch();
        repaint();
    }
}

void AudioEditor::pitchMenu (juce::PopupMenu& menu)
{
    const bool ready = pitch.analysed && pitch.singleVoice && ! pitch.notes.empty();
    const auto selected = pitchSelected;

    menu.addItem (juce::PopupMenu::Item (selected.empty() ? "Center All Notes" : "Center Selected Notes")
                      .setEnabled (ready)
                      .setAction ([this, selected]
                      {
                          engine.getTracks().pitch().centreNotes (region->id, selected);
                          refreshPitch();
                          repaint();
                      }));
    menu.addItem (juce::PopupMenu::Item ("Select All").setEnabled (ready).setAction ([this]
    {
        std::vector<double> all;

        for (auto& n : pitch.notes)
            all.push_back (n.sourceStart);

        setPitchSelection (all);
    }));
    menu.addItem (juce::PopupMenu::Item ("Reset Pitch")
                      .setEnabled (pitch.hasCorrections)
                      .setAction ([this]
                      {
                          engine.getTracks().pitch().resetPitch (region->id);
                          refreshPitch();
                          repaint();
                      }));

    if (! selected.empty())
    {
        menu.addSeparator();
        menu.addItem (juce::PopupMenu::Item ("Deselect").setAction ([this] { setPitchSelection ({}); }));
    }
}

//==============================================================================
// Painting
void AudioEditor::paintPitch (juce::Graphics& g)
{
    if (waveformDirty)
        refreshWaveform();

    juce::Graphics::ScopedSaveState save (g);
    g.reduceClipRegion (waveArea);

    const auto area = waveArea.toFloat();
    const double start = region->startBeat;
    const auto colour = trackColour();

    // Rows: one per semitone, the key's lighter, a stronger line at each C.
    for (int n = pitchLow; n <= pitchHigh; ++n)
    {
        const float top = noteToY ((float) n) - pitchRowHeight * 0.5f;
        const bool inKey = (pitch.keyNotes & (1 << (n % 12))) != 0;   // rows are pitches as heard

        if (inKey)
        {
            g.setColour (theme().arrangement.tintLight.withAlpha (0.045f));
            g.fillRect (area.getX(), top, area.getWidth(), pitchRowHeight);
        }

        g.setColour (theme().arrangement.tintLight.withAlpha (n % 12 == 0 ? 0.2f : 0.05f));
        g.fillRect (area.getX(), top + pitchRowHeight - 1.0f, area.getWidth(), 1.0f);
    }

    // Bars, faintly (as in the waveform).
    if (tempo.shortestBar() * pixelsPerBeat >= 6.0)
    {
        g.setColour (theme().arrangement.tintLight.withAlpha (0.08f));
        tempo.forEachBar (start + scrollBeats, start + scrollBeats + visibleBeats(), 1, [&] (int, double barBeat)
        {
            g.fillRect (beatToX (barBeat - start), area.getY(), 1.0f, area.getHeight());
        });
    }

    // The faint waveform along the bottom.
    {
        const float h = area.getHeight() * waveformShare;
        const float mid = area.getBottom() - h * 0.5f;
        g.setColour (colour.withAlpha (0.28f));

        for (size_t i = 0; i < columns.size(); ++i)
        {
            const float top = mid - columns[i].max * h * 0.5f;
            const float bottom = mid - columns[i].min * h * 0.5f;
            g.fillRect (area.getX() + (float) i, top, 1.0f, juce::jmax (1.0f, bottom - top));
        }
    }

    // Outside the region: dimmed, as in the waveform.
    const float left = beatToX (0.0), right = beatToX (region->lengthBeats);
    g.setColour (theme().arrangement.laneBg.withAlpha (0.6f));
    g.fillRect (area.withRight (left));
    g.fillRect (area.withLeft (right));

    // A message instead of notes: still analysing, or not a single voice.
    auto message = [&] (const juce::String& text)
    {
        g.setColour (theme().arrangement.textSecondary);
        g.setFont (fonts::ui (textSize::md));
        g.drawFittedText (text, waveArea.withTrimmedLeft (noteColumnWidth).reduced (20, 0), juce::Justification::centred, 2);
    };

    if (pitch.analysing)
        message (juce::String::fromUTF8 ("Finding the notes in the recording\xe2\x80\xa6"));
    else if (pitch.analysed && ! pitch.singleVoice)
        message ("This recording doesn't look like a single voice or instrument");
    else if (pitch.analysed && pitch.notes.empty())
        message ("There are no sung or played notes in this recording");

    // The notes: a blob in the track's colour where it's sung (or moved to),
    // with its measured pitch through it.
    const PitchNoteInfo* focus = nullptr;

    for (auto& n : pitch.notes)
    {
        const float centre = shownCentre (n);
        const auto blob = pitchNoteBounds (n, centre);

        if (blob.getRight() < area.getX() || blob.getX() > area.getRight())
            continue;

        const bool selected = isPitchSelected (n.sourceStart);

        // Where it was sung, dashed, when it's been moved (or is being dragged).
        if (selected && std::abs (centre - n.sung) > 0.01f)
        {
            juce::Path outline;
            outline.addRoundedRectangle (pitchNoteBounds (n, n.sung), blob.getHeight() * 0.5f);
            juce::Path dashed;
            const float dashes[] { 3.0f, 3.0f };
            juce::PathStrokeType (1.0f).createDashedStroke (dashed, outline, dashes, 2);
            g.setColour (theme().arrangement.text.withAlpha (0.5f));
            g.fillPath (dashed);
        }

        if (selected)
        {
            g.setColour (colour.withAlpha (0.3f));   // the glow
            g.fillRoundedRectangle (blob.expanded (3.0f), blob.getHeight() * 0.5f + 3.0f);
        }

        // Selected: lighter, with a light outline (as in the design).
        g.setColour (selected ? colour.interpolatedWith (juce::Colours::white, 0.55f) : colour);
        g.fillRoundedRectangle (blob, blob.getHeight() * 0.5f);

        if (selected)
        {
            g.setColour (theme().arrangement.text.withAlpha (0.9f));
            g.drawRoundedRectangle (blob.reduced (0.5f), blob.getHeight() * 0.5f, 1.5f);
        }

        // The pitch curve, moved and flattened the way the corrections will.
        if (n.curve.size() > 1)
        {
            juce::Path curve;
            const float move = centre - n.sung;

            for (size_t i = 0; i < n.curve.size(); ++i)
            {
                const auto [beat, p] = n.curve[i];
                const float shown = p + move - n.correction.flatten * (p - n.sung);
                const juce::Point<float> pt { beatToX (beat - start), noteToY (shown) };

                if (i == 0)
                    curve.startNewSubPath (pt);
                else
                    curve.lineTo (pt);
            }

            g.setColour (juce::Colours::white.withAlpha (0.85f));
            g.strokePath (curve, juce::PathStrokeType (1.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        // How far off its semitone, if it's noticeably off.
        if (const float off = centre - std::round (centre); std::abs (off) > centsLabelFrom)
        {
            g.setColour (theme().arrangement.textSecondary);
            g.setFont (fonts::mono (textSize::xs));
            g.drawText (centsText (off), juce::Rectangle<float> (blob.getRight() + 4.0f, blob.getCentreY() - 8.0f, 48.0f, 16.0f),
                        juce::Justification::centredLeft, false);
        }

        if (selected && sameStart (n.sourceStart, pitchFocus))
            focus = &n;
    }

    // The bubble over the focused note: "A4  was -30¢ · now 0¢", the
    // note's name brighter.
    if (focus != nullptr)
    {
        const float centre = shownCentre (*focus);
        const int nearest = juce::roundToInt (centre);
        const auto name = noteName (nearest) + juce::String (nearest / 12 - 1);
        const auto rest = "was " + centsText (focus->sung - std::round (focus->sung)) + dot + "now " + centsText (centre - (float) nearest);
        const auto nameFont = fonts::monoBold (textSize::sm), restFont = fonts::mono (textSize::sm);
        const int nameWidth = juce::GlyphArrangement::getStringWidthInt (nameFont, name);
        const int restWidth = juce::GlyphArrangement::getStringWidthInt (restFont, rest);
        const auto blob = pitchNoteBounds (*focus, centre);
        auto bubble = juce::Rectangle<float> ((float) (nameWidth + restWidth) + 34.0f, 26.0f)
                          .withX (blob.getX() - 60.0f).withBottom (blob.getY() - 6.0f);

        bubble.setX (juce::jmax (area.getX() + (float) noteColumnWidth + 4.0f, bubble.getX()));

        if (bubble.getY() < area.getY() + 2.0f)
            bubble.setY (blob.getBottom() + 6.0f);

        g.setColour (theme().arrangement.laneBg.brighter (0.12f));
        g.fillRoundedRectangle (bubble, size::radiusSmall);
        g.setColour (theme().arrangement.tintLight.withAlpha (0.25f));
        g.drawRoundedRectangle (bubble.reduced (0.5f), size::radiusSmall, 1.0f);

        auto text = bubble.reduced (12.0f, 0.0f);
        g.setColour (theme().arrangement.text);
        g.setFont (nameFont);
        g.drawText (name, text.removeFromLeft ((float) nameWidth + 1.0f), juce::Justification::centredLeft, false);
        text.removeFromLeft (10.0f);
        g.setColour (theme().arrangement.textSecondary);
        g.setFont (restFont);
        g.drawText (rest, text, juce::Justification::centredLeft, false);
    }

    // The selection box.
    if (pitchDrag.mode == PitchDrag::Mode::box && ! pitchDrag.box.isEmpty())
    {
        g.setColour (theme().arrangement.highlight.withAlpha (0.1f));
        g.fillRect (pitchDrag.box);
        g.setColour (theme().arrangement.highlight.withAlpha (0.6f));
        g.drawRect (pitchDrag.box, 1);
    }

    if (playheadBeat >= 0.0)
    {
        g.setColour (theme().accent);
        g.fillRect (beatToX (playheadBeat - start) - 1.0f, area.getY(), 2.0f, area.getHeight());
    }

    // The note column, over the left edge: in-key notes named, C and A with
    // their octave.
    const auto column = pitchColumn().toFloat();
    g.setColour (theme().bottomPanelBg);
    g.fillRect (column);
    g.setColour (theme().divider);
    g.fillRect (column.getRight() - 1.0f, column.getY(), 1.0f, column.getHeight());
    g.setFont (fonts::mono (juce::jmin (textSize::xs, pitchRowHeight - 2.0f)));

    for (int n = pitchLow; n <= pitchHigh; ++n)
    {
        if ((pitch.keyNotes & (1 << (n % 12))) == 0)
            continue;

        const bool named = n % 12 == 0 || n % 12 == 9;
        g.setColour (named ? theme().text : theme().textSecondary);
        g.drawText (noteName (n) + (named ? juce::String (n / 12 - 1) : juce::String()),
                    juce::Rectangle<float> (column.getX(), noteToY ((float) n) - pitchRowHeight * 0.5f, column.getWidth() - 6.0f, pitchRowHeight),
                    juce::Justification::centredRight, false);
    }
}

} // namespace ikaros
