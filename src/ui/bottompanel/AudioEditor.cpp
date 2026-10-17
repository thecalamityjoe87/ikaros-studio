#include "ui/bottompanel/AudioEditor.h"
#include "engine/AudioEngine.h"
#include "engine/TrackController.h"
#include "ui/widgets/Icons.h"
#include "ui/theme/Fonts.h"
#include "ui/theme/Tokens.h"

namespace ikaros
{

using namespace tokens;

namespace
{
    constexpr int toolbarHeight = 36;
    constexpr int rulerHeight = 22;
    constexpr int scrollBarSize = 10;
    constexpr int edgeReach = 6;              // how close to a region edge the mouse has to be to trim it
    constexpr int fadeReach = 7;              // ...and to a fade handle to grab it
    constexpr int flexReach = 5;              // ...and to a hit or flex marker
    constexpr float fadeHandleInset = 7.0f;   // a fade handle stays this far inside the edge, so it can be grabbed
    constexpr double minPixelsPerBeat = 2.0, maxPixelsPerBeat = 600.0;
}

//==============================================================================
AudioEditor::AudioEditor (AudioEngine& e) : engine (e)
{
    setTitle ("Audio editor");
    setWantsKeyboardFocus (true);

    timeZoom.setTitle ("Zoom in time");
    timeZoom.setTooltip ("Zoom in time (Ctrl + mouse wheel)");
    timeZoom.setRange (0.0, 1.0);
    timeZoom.setMouseClickGrabsKeyboardFocus (false);
    timeZoom.onValueChange = [this]
    {
        // Logarithmic: each step along the slider zooms by the same factor.
        setTimeZoom (minPixelsPerBeat * std::pow (maxPixelsPerBeat / minPixelsPerBeat, timeZoom.getValue()),
                     (float) waveArea.getCentreX());
    };
    addAndMakeVisible (timeZoom);

    timeScroll.setAutoHide (false);
    timeScroll.addListener (this);
    addAndMakeVisible (timeScroll);

    setRegion (std::nullopt, colour::accent, TempoMap());
}

AudioEditor::~AudioEditor()
{
    timeScroll.removeListener (this);
}

//==============================================================================
void AudioEditor::setRegion (std::optional<RegionInfo> r, juce::Colour c, const TempoMap& t)
{
    const bool sameRegion = r.has_value() && region.has_value() && r->id == region->id;

    if (! sameRegion)
    {
        drag = {};
        selection.reset();
        needsFit = true;
    }
    else if (drag.mode != Drag::Mode::none
              && (r->startBeat != region->startBeat || r->lengthBeats != region->lengthBeats))
    {
        drag = {};   // it changed underneath the drag (an undo, recording): the drag can't go on
    }

    // Comparing two std::optionals compares what's in them (every member of
    // RegionInfo, see its operator==).
    if (r != region)
        waveformDirty = true;

    region = std::move (r);
    colour = c;
    tempo = t;
    refreshFlex();

    const bool editable = hasRegion();

    for (auto* comp : std::initializer_list<juce::Component*> { &timeZoom, &timeScroll })
        comp->setVisible (editable);

    if (needsFit && ! waveArea.isEmpty())
        fitToRegion();

    clampView();
    repaint();
}

void AudioEditor::setGrid (EditorGrid newGrid)
{
    grid = newGrid;
}

void AudioEditor::setQuantizeFeel (double strength, double swing)
{
    quantizeStrength = strength;
    quantizeSwing = swing;
}

void AudioEditor::quantize()
{
    if (! hasRegion())
        return;

    // The selection is kept from the region's start; the engine wants it on the timeline.
    std::optional<juce::Range<double>> range;

    if (selection.has_value())
        range = *selection + region->startBeat;

    switch (engine.getTracks().flex().quantize (region->id, quantizeSettingsFor (grid, quantizeStrength, quantizeSwing), range))
    {
        case FlexQuantizeResult::unavailable:    showNotice (flex.unavailableReason); break;
        case FlexQuantizeResult::alreadyOnGrid:  showNotice (selection.has_value() ? "The selected hits are already on the grid"
                                                                                   : "The hits are already on the grid"); break;
        case FlexQuantizeResult::findingHits:    // the toolbar shows how it's getting on (see flexStatus)
        case FlexQuantizeResult::quantized:      break;
    }

    refreshFlex();
    waveformDirty = true;
    repaint();
}

void AudioEditor::showNotice (const juce::String& text)
{
    notice = text;
    noticeUntil = juce::Time::getMillisecondCounter() + 3000;
    repaint (toolbarArea);
}

void AudioEditor::setPlayheadBeat (double editBeat)
{
    const double beat = hasRegion() ? editBeat : -1.0;

    if (beat == playheadBeat)
        return;

    // Only the thin strips where it was and where it is now need redrawing.
    auto strip = [this] (double b)
    {
        const auto x = (int) beatToX (b - region->startBeat);
        return juce::Rectangle<int> (x - 2, rulerArea.getY(), 5, waveArea.getBottom() - rulerArea.getY());
    };

    if (playheadBeat >= 0.0 && hasRegion())
        repaint (strip (playheadBeat));

    playheadBeat = beat;

    if (playheadBeat >= 0.0)
        repaint (strip (playheadBeat));
}

void AudioEditor::update()
{
    // Still reading the recording: try again.
    if (waveformPending && isShowing())
    {
        waveformDirty = true;
        repaint (waveArea);
    }

    // Flex is finding the hits or stretching the audio, in the background
    // (or Quantize is waiting for the hits): nothing in the session changes
    // then, so ask how it's getting on.
    if (hasRegion() && (flex.on || flex.quantizePending) && (flex.analysing || flex.stretchProgress >= 0.0f) && isShowing())
    {
        refreshFlex();
        repaint();
    }

    if (notice.isNotEmpty() && juce::Time::getMillisecondCounter() > noticeUntil)
    {
        notice.clear();
        repaint (toolbarArea);
    }
}

//==============================================================================
// Flex
void AudioEditor::refreshFlex()
{
    flex = hasRegion() ? engine.getTracks().flex().getFlex (region->id) : FlexInfo();
}

juce::String AudioEditor::flexStatus() const
{
    auto percent = [] (float p) { return p >= 0.0f ? " " + juce::String (juce::roundToInt (p * 100.0f)) + "%" : juce::String(); };

    if (flex.analysing)
        return juce::String::fromUTF8 (flex.quantizePending ? "Finding the hits to quantize\xe2\x80\xa6" : "Finding the hits\xe2\x80\xa6")
                 + percent (flex.analysisProgress);

    if (flex.stretchProgress >= 0.0f)
        return juce::String::fromUTF8 ("Stretching the audio\xe2\x80\xa6") + percent (flex.stretchProgress);

    return {};
}

// Calls fn (beat, repeat) for every place a moment of the region's content
// shows on screen: once, or in every repeat of a looped region. `firstBeat`
// is where it plays the first time round (FlexInfo's beats).
template <typename Fn>
void AudioEditor::forEachRepeat (double firstBeat, Fn&& fn) const
{
    if (! region->isLooped())
    {
        fn (firstBeat, 0.0);
        return;
    }

    // Only what's inside the loop repeats; the rest of the content never plays.
    const double loop = region->loopLengthBeats;
    const double passStart = region->startBeat - region->loopOffsetBeats;

    if (firstBeat < passStart - 1.0e-9 || firstBeat >= passStart + loop)
        return;

    const double from = region->startBeat + scrollBeats, to = from + visibleBeats();

    for (double k = std::floor ((from - firstBeat) / loop); firstBeat + k * loop <= to; k += 1.0)
        fn (firstBeat + k * loop, k * loop);
}

std::optional<AudioEditor::FlexTarget> AudioEditor::flexTargetAt (juce::Point<int> p, bool markersOnly) const
{
    if (! hasRegion() || ! flex.on || ! waveArea.contains (p))
        return std::nullopt;

    std::optional<FlexTarget> best;
    float bestDistance = (float) flexReach + 0.5f;

    auto consider = [&] (double source, double beat, bool isMarker)
    {
        forEachRepeat (beat, [&] (double shown, double repeat)
        {
            const float distance = std::abs ((float) p.x - beatToX (shown - region->startBeat));

            // A marker wins over a hit the same distance away.
            if (distance < bestDistance || (isMarker && distance <= bestDistance && best && ! best->isMarker))
            {
                bestDistance = distance;
                best = FlexTarget { source, beat, repeat, isMarker };
            }
        });
    };

    // The markers in between the ends (the ends of the recording stay put).
    for (size_t i = 1; i + 1 < flex.markers.size(); ++i)
        consider (flex.markers[i].sourceSeconds, flex.markers[i].beat, true);

    if (! markersOnly)
        for (auto& t : flex.transients)
            consider (t.sourceSeconds, t.beat, false);

    return best;
}

//==============================================================================
// Coordinates
float AudioEditor::beatToX (double rel) const   { return (float) waveArea.getX() + (float) ((rel - scrollBeats) * pixelsPerBeat); }
double AudioEditor::xToBeat (float x) const     { return (x - (float) waveArea.getX()) / pixelsPerBeat + scrollBeats; }
double AudioEditor::visibleBeats() const        { return waveArea.getWidth() / pixelsPerBeat; }

juce::Range<double> AudioEditor::viewLimits() const
{
    if (! hasRegion())
        return { 0.0, 1.0 };

    // A bar either side, so the edges can be grabbed. A looped region
    // repeats forever: show one more loop either side. Otherwise show the
    // whole recording, so the edges can be dragged out to anywhere in it.
    const double length = region->lengthBeats;
    const double beatsPerBar = tempo.beatsPerBarAt (region->startBeat);
    double before = beatsPerBar, after = length + beatsPerBar;

    if (region->isLooped())
    {
        before = juce::jmax (beatsPerBar, region->loopLengthBeats);
        after = length + before;
    }
    else
    {
        before += juce::jmax (0.0, region->startBeat - region->minStartBeat);

        if (region->maxEndBeat < 1.0e9)
            after = juce::jmax (length, region->maxEndBeat - region->startBeat) + beatsPerBar;
    }

    // Not before the start of the song.
    return { juce::jmax (-region->startBeat, -before), after };
}

//==============================================================================
// Grid
double AudioEditor::snapToGrid (double editBeat) const
{
    return snapToEditorGrid (grid, tempo, editBeat);
}

//==============================================================================
// View
void AudioEditor::fitToRegion()
{
    needsFit = false;

    if (! hasRegion() || waveArea.isEmpty())
        return;

    // The whole region across, with a little room either side for its edges.
    const double length = juce::jmax (0.25, region->lengthBeats);
    pixelsPerBeat = juce::jlimit (minPixelsPerBeat, maxPixelsPerBeat, waveArea.getWidth() / (length * 1.1));
    scrollBeats = -(visibleBeats() - length) * 0.5;
    clampView();
}

void AudioEditor::setTimeZoom (double newPixelsPerBeat, float anchorX)
{
    // Keep the beat under the anchor (the mouse, or the middle) where it is.
    const double anchorBeat = xToBeat (anchorX);
    pixelsPerBeat = juce::jlimit (minPixelsPerBeat, maxPixelsPerBeat, newPixelsPerBeat);
    scrollBeats = anchorBeat - (anchorX - (float) waveArea.getX()) / pixelsPerBeat;
    clampView();
    repaint();
}

void AudioEditor::clampView()
{
    const auto limits = viewLimits();
    scrollBeats = juce::jlimit (limits.getStart(), juce::jmax (limits.getStart(), limits.getEnd() - visibleBeats()), scrollBeats);

    timeScroll.setRangeLimits (limits.getStart(), juce::jmax (limits.getEnd(), limits.getStart() + visibleBeats()),
                               juce::dontSendNotification);
    timeScroll.setCurrentRange (scrollBeats, visibleBeats(), juce::dontSendNotification);
    timeZoom.setValue (std::log (pixelsPerBeat / minPixelsPerBeat) / std::log (maxPixelsPerBeat / minPixelsPerBeat),
                       juce::dontSendNotification);
    waveformDirty = true;
}

void AudioEditor::scrollBarMoved (juce::ScrollBar*, double newRangeStart)
{
    scrollBeats = newRangeStart;
    clampView();
    repaint();
}

void AudioEditor::refreshWaveform()
{
    waveformDirty = false;
    const int width = waveArea.getWidth();

    if (! hasRegion() || width <= 0)
    {
        columns.clear();
        waveformPending = false;
        return;
    }

    // One column per pixel, across what's on screen.
    const double from = region->startBeat + scrollBeats;
    const bool previewing = drag.mode == Drag::Mode::flex && ! drag.preview.empty();
    waveformPending = ! engine.getTracks().regions().getWaveform (region->id, from, from + visibleBeats(), width, columns,
                                                                  previewing ? &drag.preview : nullptr);
}

//==============================================================================
// The region as drawn
double AudioEditor::shownStart() const
{
    return drag.mode == Drag::Mode::trimStart ? drag.start : region->startBeat;
}

double AudioEditor::shownEnd() const
{
    return drag.mode == Drag::Mode::trimEnd ? drag.end : region->getEndBeat();
}

double AudioEditor::shownFade (bool fadeIn) const
{
    if (fadeIn)
        return drag.mode == Drag::Mode::fadeIn ? drag.fadeIn : region->fadeInBeats;

    return drag.mode == Drag::Mode::fadeOut ? drag.fadeOut : region->fadeOutBeats;
}

juce::Point<float> AudioEditor::fadeHandle (bool fadeIn) const
{
    // At the top of the waveform, where the fade ends.
    const float left = beatToX (shownStart() - region->startBeat);
    const float right = beatToX (shownEnd() - region->startBeat);
    const float length = (float) (shownFade (fadeIn) * pixelsPerBeat);
    const float x = fadeIn ? left + length : right - length;

    return { juce::jlimit (left + fadeHandleInset, juce::jmax (left + fadeHandleInset, right - fadeHandleInset), x),
             (float) waveArea.getY() + 7.0f };
}

AudioEditor::Drag::Mode AudioEditor::modeAt (juce::Point<int> p) const
{
    if (! hasRegion() || ! waveArea.contains (p))
        return Drag::Mode::none;

    if (p.y < waveArea.getY() + fadeReach * 2)
        for (bool fadeIn : { true, false })
            if (std::abs ((float) p.x - fadeHandle (fadeIn).x) <= (float) fadeReach)
                return fadeIn ? Drag::Mode::fadeIn : Drag::Mode::fadeOut;

    // The nearer edge, if the mouse is close enough to one.
    const float toStart = std::abs ((float) p.x - beatToX (0.0));
    const float toEnd = std::abs ((float) p.x - beatToX (region->lengthBeats));

    if (juce::jmin (toStart, toEnd) <= (float) edgeReach)
        return toStart < toEnd ? Drag::Mode::trimStart : Drag::Mode::trimEnd;

    if (flexTargetAt (p, false).has_value())
        return Drag::Mode::flex;

    return Drag::Mode::playhead;
}

//==============================================================================
// Mouse and keys
void AudioEditor::mouseMove (const juce::MouseEvent& e)
{
    switch (modeAt (e.getPosition()))
    {
        case Drag::Mode::trimStart:
        case Drag::Mode::trimEnd:
        case Drag::Mode::fadeIn:
        case Drag::Mode::fadeOut:
        case Drag::Mode::flex:      setMouseCursor (juce::MouseCursor::LeftRightResizeCursor); break;

        // Empty waveform: drag to select.
        case Drag::Mode::playhead:  setMouseCursor (juce::MouseCursor::IBeamCursor); break;
        case Drag::Mode::select:
        case Drag::Mode::none:      setMouseCursor (juce::MouseCursor::NormalCursor); break;
    }
}

void AudioEditor::mouseDown (const juce::MouseEvent& e)
{
    if (! hasRegion())
        return;

    grabKeyboardFocus();
    drag = {};

    if (e.mods.isPopupMenu())
    {
        showMenu();
        return;
    }

    const auto p = e.getPosition();
    auto mode = rulerArea.contains (p) ? Drag::Mode::playhead : modeAt (p);

    if (mode == Drag::Mode::none)
        return;

    // In the waveform, a click moves the playhead (and drops the selection),
    // and a drag selects. The ruler only moves the playhead.
    if (mode == Drag::Mode::playhead && waveArea.contains (p))
    {
        mode = Drag::Mode::select;
        drag.selectFrom = juce::jlimit (0.0, region->lengthBeats, xToBeat ((float) p.x));
        selection.reset();
        engine.setPositionBeats (juce::jmax (0.0, region->startBeat + xToBeat ((float) p.x)));
        repaint();
    }

    drag.mode = mode;
    drag.startX = p.x;
    drag.start = region->startBeat;
    drag.end = region->getEndBeat();
    drag.fadeIn = region->fadeInBeats;
    drag.fadeOut = region->fadeOutBeats;

    if (mode == Drag::Mode::flex)
    {
        const auto target = flexTargetAt (p, false);
        drag.flexSource = target->source;
        drag.flexBeat = target->beat;
        drag.repeat = target->repeat;
    }

    // A click moves the playhead straight away; the edges and fades wait
    // for the mouse to move, so a click doesn't snap them to the grid.
    if (mode == Drag::Mode::playhead)
        updateDrag (e);
}

void AudioEditor::mouseDrag (const juce::MouseEvent& e)
{
    if (drag.mode != Drag::Mode::none && hasRegion())
        updateDrag (e);
}

void AudioEditor::mouseUp (const juce::MouseEvent& e)
{
    if (drag.mode == Drag::Mode::none || ! hasRegion())
        return;

    // The playhead and the selection follow the mouse as it goes: there's
    // nothing left to do.
    if (drag.mode == Drag::Mode::playhead || drag.mode == Drag::Mode::select || ! e.mouseWasDraggedSinceMouseDown())
    {
        drag = {};
        repaint();
        return;
    }

    finishDrag();
}

void AudioEditor::updateDrag (const juce::MouseEvent& e)
{
    const double delta = (e.x - drag.startX) / pixelsPerBeat;
    const double length = region->lengthBeats;

    switch (drag.mode)
    {
        case Drag::Mode::playhead:
            engine.setPositionBeats (juce::jmax (0.0, region->startBeat + xToBeat ((float) e.x)));
            return;

        // Inside the region only (that's what Quantize can move).
        case Drag::Mode::select:
        {
            const double to = juce::jlimit (0.0, length, xToBeat ((float) e.x));
            selection = juce::Range<double>::between (drag.selectFrom, to);

            if (selection->getLength() * pixelsPerBeat < 3.0)
                selection.reset();   // a wobble of the mouse, not a selection

            break;
        }

        case Drag::Mode::trimStart:
            drag.start = juce::jlimit (juce::jmax (0.0, region->minStartBeat), region->getEndBeat() - RegionEditor::minRegionBeats,
                                       snapToGrid (region->startBeat + delta));
            break;

        case Drag::Mode::trimEnd:
            drag.end = juce::jlimit (region->startBeat + RegionEditor::minRegionBeats, region->maxEndBeat,
                                     snapToGrid (region->getEndBeat() + delta));
            break;

        // Into the region makes a fade longer, up to where the other one
        // starts. Fades don't snap to the grid (as in the arrangement).
        case Drag::Mode::fadeIn:
            drag.fadeIn = juce::jlimit (0.0, juce::jmax (0.0, length - region->fadeOutBeats), region->fadeInBeats + delta);
            break;

        case Drag::Mode::fadeOut:
            drag.fadeOut = juce::jlimit (0.0, juce::jmax (0.0, length - region->fadeInBeats), region->fadeOutBeats - delta);
            break;

        // A hit goes where the mouse takes it, without snapping (Quantize
        // lines hits up with the grid). The engine works out the markers it
        // needs and keeps it between its neighbours; previewMove shows that.
        case Drag::Mode::flex:
            drag.flexBeat = xToBeat ((float) e.x) + region->startBeat - drag.repeat;
            drag.preview = FlexEditor::previewMove (flex, drag.flexSource, drag.flexBeat);
            waveformDirty = true;
            break;

        case Drag::Mode::none:
            return;
    }

    repaint();
}

void AudioEditor::finishDrag()
{
    // std::exchange: takes the drag and leaves `drag` back at none.
    const auto finished = std::exchange (drag, {});
    auto& regions = engine.getTracks().regions();
    const auto id = region->id;
    const double oldStart = region->startBeat;

    // Each is one undo step.
    switch (finished.mode)
    {
        case Drag::Mode::trimStart:
        case Drag::Mode::trimEnd:   regions.trimRegion (id, finished.start, finished.end); break;
        case Drag::Mode::fadeIn:    regions.setFade (id, true, finished.fadeIn); break;
        case Drag::Mode::fadeOut:   regions.setFade (id, false, finished.fadeOut); break;
        case Drag::Mode::flex:      engine.getTracks().flex().moveMarker (id, finished.flexSource, finished.flexBeat); break;
        case Drag::Mode::playhead:
        case Drag::Mode::select:
        case Drag::Mode::none:      break;
    }

    // Read the region back now rather than when the session's change
    // message arrives, so it never shows as it was.
    if (auto fresh = regions.getRegion (id))
    {
        // The view is measured from the region's start: when that moves,
        // move the view with it, so the audio stays put on screen.
        scrollBeats -= fresh->startBeat - oldStart;
        region = std::move (fresh);
    }

    refreshFlex();
    clampView();
    repaint();
}

void AudioEditor::mouseDoubleClick (const juce::MouseEvent& e)
{
    // Double-clicking a flex marker takes it away ("Remove Flex Marker").
    if (auto target = flexTargetAt (e.getPosition(), true))
    {
        drag = {};
        engine.getTracks().flex().removeMarker (region->id, target->source);
        refreshFlex();
        waveformDirty = true;
        repaint();
    }
}

void AudioEditor::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (! hasRegion())
        return;

    if (e.mods.isCommandDown())
    {
        setTimeZoom (pixelsPerBeat * std::pow (2.0, (double) wheel.deltaY * 2.5), (float) e.x);   // one notch ≈ 1.4×
    }
    else
    {
        const float amount = wheel.deltaX != 0.0f ? wheel.deltaX : wheel.deltaY;
        scrollBeats -= (wheel.isReversed ? -amount : amount) * 300.0 / pixelsPerBeat;
        clampView();
        repaint();
    }
}

bool AudioEditor::keyPressed (const juce::KeyPress& key)
{
    // Escape in the middle of a drag cancels it: nothing has changed yet.
    if (key == juce::KeyPress::escapeKey && drag.mode != Drag::Mode::none)
    {
        drag = {};
        waveformDirty = true;   // a flex drag's preview
        repaint();
        return true;
    }

    // Otherwise it drops the selection.
    if (key == juce::KeyPress::escapeKey && selection.has_value())
    {
        selection.reset();
        repaint();
        return true;
    }

    if (key == juce::KeyPress ('q') && hasRegion())   // KeyPress compares letters case-insensitively
    {
        quantize();
        return true;
    }

    return false;   // everything else is the app's (Space to play, undo...)
}

void AudioEditor::showMenu()
{
    if (! hasRegion())
        return;

    juce::PopupMenu menu;
    menu.addItem (juce::PopupMenu::Item (selection.has_value() ? "Quantize Selection" : "Quantize All")
                      .setEnabled (flex.available)
                      .setAction ([this] { quantize(); }));
    menu.addItem (juce::PopupMenu::Item ("Reset Flex")
                      .setEnabled (flex.on && flex.hasEdits)
                      .setAction ([this]
                      {
                          engine.getTracks().flex().resetFlex (region->id);
                          refreshFlex();
                          waveformDirty = true;
                          repaint();
                      }));

    if (selection.has_value())
    {
        menu.addSeparator();
        menu.addItem (juce::PopupMenu::Item ("Deselect").setAction ([this] { selection.reset(); repaint(); }));
    }

    // The menu is asynchronous: its actions run after this function returns.
    // With this editor as its target, JUCE closes the menu if the editor is
    // deleted while it's open, so they never run on a deleted editor.
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
}

//==============================================================================
// Painting
void AudioEditor::paint (juce::Graphics& g)
{
    g.fillAll (colour::windowBg);

    if (! hasRegion())
    {
        g.setColour (colour::textSecondary);
        g.setFont (fonts::ui (14.0f));
        g.drawText ("Select an audio region to edit it here. Record onto an audio track, or drag in a loop or an audio file.",
                    getLocalBounds().reduced (12, 0), juce::Justification::centred);
        return;
    }

    paintToolbar (g);
    paintRuler (g);
    paintWaveform (g);
}

void AudioEditor::paintToolbar (juce::Graphics& g)
{
    g.setColour (colour::toolbarBg);
    g.fillRect (toolbarArea);
    g.setColour (colour::divider);
    g.fillRect (toolbarArea.withTop (toolbarArea.getBottom() - 1));

    g.setColour (colour::textSecondary);
    g.setFont (fonts::ui (12.0f));
    // With Flex on: how it's getting on, or how to use it.
    auto hint = juce::String::fromUTF8 ("Drag the edges to trim \xc2\xb7 Drag the dots to fade \xc2\xb7 Click to move the playhead");

    if (flex.on || flex.quantizePending)
    {
        const auto status = flexStatus();
        hint = status.isNotEmpty() ? status
                                   : juce::String::fromUTF8 ("Drag a hit to move it \xc2\xb7 Double-click a marker to remove it \xc2\xb7 "
                                                             "Drag across hits to select \xc2\xb7 Q to quantize");
    }

    if (notice.isNotEmpty())
        hint = notice;

    g.drawFittedText (hint, hintArea, juce::Justification::centredLeft, 1, 1.0f);

    icons::draw (g, Icon::search, timeZoomIcon.toFloat(), colour::textSecondary, 13.0f, 1.6f);
}

void AudioEditor::paintRuler (juce::Graphics& g)
{
    g.setColour (colour::sidebarBg);
    g.fillRect (rulerArea);
    g.setColour (colour::divider);
    g.fillRect (rulerArea.withTop (rulerArea.getBottom() - 1));

    juce::Graphics::ScopedSaveState save (g);
    g.reduceClipRegion (rulerArea);

    const double start = region->startBeat;
    const double first = start + scrollBeats, last = first + visibleBeats();
    const auto area = rulerArea.toFloat();

    // Beats: small ticks, when they're far enough apart to see.
    if (pixelsPerBeat >= 8.0)
    {
        g.setColour (colour::textSecondary.withAlpha (0.4f));

        for (double beat = std::ceil (first); beat <= last; beat += 1.0)
            g.fillRect (beatToX (beat - start), area.getBottom() - 5.0f, 1.0f, 4.0f);
    }

    // Bars: numbered (only every few, when they're close together).
    // Bars can differ in length: the tempo map knows where each one starts.
    const double barPixels = tempo.shortestBar() * pixelsPerBeat;
    const int labelEvery = juce::jmax (1, (int) std::ceil (36.0 / barPixels));
    g.setFont (fonts::mono (11.0f));

    tempo.forEachBar (first, last, 1, [&] (int bar, double barBeat)
    {
        const float x = beatToX (barBeat - start);
        const auto number = bar + 1;

        if (number % labelEvery != 0 && labelEvery > 1)
            return;

        g.setColour (colour::textSecondary.withAlpha (0.6f));
        g.fillRect (x, area.getY() + 4.0f, 1.0f, area.getHeight() - 4.0f);
        g.setColour (colour::textSecondary);
        g.drawText (juce::String (number), juce::Rectangle<float> (x + 4.0f, area.getY(), 40.0f, area.getHeight()),
                    juce::Justification::centredLeft);
    });

    if (playheadBeat >= 0.0)
    {
        g.setColour (colour::accent);
        g.fillRect (beatToX (playheadBeat - start) - 1.0f, area.getY(), 2.0f, area.getHeight());
    }
}

void AudioEditor::paintWaveform (juce::Graphics& g)
{
    if (waveformDirty)
        refreshWaveform();

    juce::Graphics::ScopedSaveState save (g);
    g.reduceClipRegion (waveArea);

    const auto area = waveArea.toFloat();
    const double start = region->startBeat;
    const float left = beatToX (shownStart() - start), right = beatToX (shownEnd() - start);
    const auto regionArea = juce::Rectangle<float>::leftTopRightBottom (left, area.getY(), right, area.getBottom());

    // Grid lines on the song's timeline: beats and bars.
    auto drawLines = [&] (double step, float alpha)
    {
        if (step * pixelsPerBeat < 6.0)
            return;

        g.setColour (juce::Colours::white.withAlpha (alpha));

        for (double beat = std::ceil ((start + scrollBeats) / step) * step; beat - start <= scrollBeats + visibleBeats(); beat += step)
            g.fillRect (beatToX (beat - start), area.getY(), 1.0f, area.getHeight());
    };

    drawLines (1.0, 0.04f);

    if (tempo.shortestBar() * pixelsPerBeat >= 6.0)
    {
        g.setColour (juce::Colours::white.withAlpha (0.1f));
        tempo.forEachBar (start + scrollBeats, start + scrollBeats + visibleBeats(), 1, [&] (int, double barBeat)
        {
            g.fillRect (beatToX (barBeat - start), area.getY(), 1.0f, area.getHeight());
        });
    }

    g.setColour (colour.withAlpha (0.1f));
    g.fillRect (regionArea);

    // The waveform: one column per pixel, from its quietest to its loudest sample.
    const float mid = area.getCentreY();
    const float halfHeight = area.getHeight() * 0.45f;
    g.setColour (juce::Colours::white.withAlpha (0.08f));
    g.fillRect (area.getX(), mid, area.getWidth(), 1.0f);
    g.setColour (colour);

    for (size_t i = 0; i < columns.size(); ++i)
    {
        const float top = mid - columns[i].max * halfHeight;
        const float bottom = mid - columns[i].min * halfHeight;
        g.fillRect (area.getX() + (float) i, top, 1.0f, juce::jmax (1.0f, bottom - top));
    }

    if (flex.on)
        paintFlex (g, area);

    // The selection: a light wash with its edges marked.
    if (selection.has_value())
    {
        const float from = beatToX (selection->getStart()), to = beatToX (selection->getEnd());
        g.setColour (colour::accent.withAlpha (0.14f));
        g.fillRect (juce::Rectangle<float>::leftTopRightBottom (from, area.getY(), to, area.getBottom()));
        g.setColour (colour::accent.withAlpha (0.6f));
        g.fillRect (from, area.getY(), 1.0f, area.getHeight());
        g.fillRect (to - 1.0f, area.getY(), 1.0f, area.getHeight());
    }

    // Outside the region (the rest of the recording, or the loop going
    // round again): dimmed.
    g.setColour (colour::windowBg.withAlpha (0.7f));
    g.fillRect (area.withRight (left));
    g.fillRect (area.withLeft (right));

    // A looped region: where each repeat starts.
    if (region->isLooped())
    {
        const double loop = region->loopLengthBeats;
        const double contentStart = -region->loopOffsetBeats;   // where the first loop starts, from the region's start
        const float dashes[] = { 3.0f, 3.0f };
        g.setColour (juce::Colours::white.withAlpha (0.3f));

        for (double b = contentStart + std::ceil ((scrollBeats - contentStart) / loop) * loop; b <= scrollBeats + visibleBeats(); b += loop)
        {
            const float x = beatToX (b);
            g.drawDashedLine ({ x, area.getY(), x, area.getBottom() }, dashes, 2, 1.0f);
        }
    }

    // Fades: a line from silence at the edge up to full volume, with the
    // part they turn down shaded (as in the arrangement), and their handles.
    for (bool fadeIn : { true, false })
    {
        const float length = juce::jmin (right - left, (float) (shownFade (fadeIn) * pixelsPerBeat));

        if (length >= 1.0f)
        {
            const float edge = fadeIn ? left : right;
            const float end = fadeIn ? edge + length : edge - length;

            juce::Path faded;
            faded.startNewSubPath (edge, area.getY());
            faded.lineTo (end, area.getY());
            faded.lineTo (edge, area.getBottom());
            faded.closeSubPath();

            g.setColour (colour::windowBg.withAlpha (0.5f));
            g.fillPath (faded);
            g.setColour (colour);
            g.drawLine (edge, area.getBottom(), end, area.getY(), 1.5f);
        }

        const auto dot = juce::Rectangle<float> (9.0f, 9.0f).withCentre (fadeHandle (fadeIn));
        g.setColour (colour::text);
        g.fillEllipse (dot);
        g.setColour (colour::windowBg);
        g.drawEllipse (dot, 1.0f);
    }

    // The region's edges, with a tab halfway down to grab.
    for (float x : { left, right })
    {
        g.setColour (colour);
        g.fillRect (x - 1.0f, area.getY(), 2.0f, area.getHeight());
        g.fillRoundedRectangle (juce::Rectangle<float> (6.0f, 28.0f).withCentre ({ x, mid }), 3.0f);
    }

    if (playheadBeat >= 0.0)
    {
        g.setColour (colour::accent);
        g.fillRect (beatToX (playheadBeat - start) - 1.0f, area.getY(), 2.0f, area.getHeight());
    }

    // Reading a long recording takes a moment (not while recording: then
    // the waveform comes when it stops).
    if (waveformPending && ! engine.getTransportState().recording)
    {
        g.setColour (colour::textSecondary);
        g.setFont (fonts::ui (13.0f));
        g.drawText (juce::String::fromUTF8 ("Reading the recording\xe2\x80\xa6"), waveArea, juce::Justification::centred);
    }
}

void AudioEditor::paintFlex (juce::Graphics& g, juce::Rectangle<float> area)
{
    const double start = region->startBeat;
    const auto& markers = drag.mode == Drag::Mode::flex && ! drag.preview.empty() ? drag.preview : flex.markers;

    // The markers as a map from the recording to the timeline (in beats),
    // so the hits move with a drag's preview.
    std::vector<WarpPoint> map;

    for (auto& m : markers)
        map.push_back ({ m.sourceSeconds, m.beat });

    // The hits: faint lines (unless a marker's on them).
    g.setColour (juce::Colours::white.withAlpha (0.18f));

    for (auto& t : flex.transients)
    {
        if (flexmap::indexOf (map, t.sourceSeconds) >= 0)
            continue;

        forEachRepeat (flexmap::toWarp (map, t.sourceSeconds), [&] (double beat, double)
        {
            g.fillRect (beatToX (beat - start), area.getY(), 1.0f, area.getHeight());
        });
    }

    // The markers between the ends: brighter, with a handle at the bottom.
    // (In the text colour, not the track's or the accent: the waveform is
    // drawn in the track's colour, and some tracks are amber.)
    for (size_t i = 1; i + 1 < markers.size(); ++i)
    {
        forEachRepeat (markers[i].beat, [&] (double beat, double)
        {
            const float x = beatToX (beat - start);
            g.setColour (colour::text.withAlpha (0.85f));
            g.fillRect (x - 0.75f, area.getY(), 1.5f, area.getHeight());
            g.fillRoundedRectangle (juce::Rectangle<float> (8.0f, 12.0f).withCentre ({ x, area.getBottom() - 8.0f }), 2.0f);
        });
    }
}

//==============================================================================
void AudioEditor::resized()
{
    auto r = getLocalBounds();

    toolbarArea = r.removeFromTop (toolbarHeight);
    auto bar = toolbarArea.reduced (12, 6);
    timeZoom.setBounds (bar.removeFromRight (90));
    timeZoomIcon = bar.removeFromRight (20).withSizeKeepingCentre (14, 14);
    bar.removeFromRight (10);
    hintArea = bar;

    timeScroll.setBounds (r.removeFromBottom (scrollBarSize));
    rulerArea = r.removeFromTop (rulerHeight);
    waveArea = r;

    if (needsFit)
        fitToRegion();

    clampView();
}

} // namespace ikaros
