#include "ui/arrangement/ArrangementView.h"
#include "ui/arrangement/ArrangementContent.h"
#include "ui/arrangement/ArrangementLayout.h"
#include "ui/arrangement/ArrangementOverlays.h"
#include "ui/arrangement/ArrangementRegionDrag.h"
#include "ui/arrangement/ArrangementRuler.h"
#include "ui/arrangement/ArrangementTempoLane.h"
#include "ui/arrangement/ArrangementSections.h"
#include "app/CommandIDs.h"
#include "engine/AudioPreview.h"
#include "engine/PluginLibrary.h"
#include "engine/SoundLibrary.h"
#include "ui/state/DragTypes.h"
#include "ui/theme/Fonts.h"
#include "ui/theme/IkarosLookAndFeel.h"
#include "ui/theme/Tokens.h"

namespace ikaros
{

using namespace tokens;
using namespace arrangement;

namespace
{
    constexpr int scrollBarHeight = 12;
    constexpr int bottomBarHeight = 26;   // the strip along the bottom: zoom under the track headers, the scroll bar beside it

    // Zoom range, in pixels per beat: from about 200 bars across the window
    // down to a single beat filling a large part of it.
    constexpr double minPixelsPerBeat = 1.5;
    constexpr double maxPixelsPerBeat = 240.0;

    // The zoom slider is logarithmic: each step along it zooms by the same factor.
    double zoomToSlider (double pixelsPerBeat)
    {
        return std::log (pixelsPerBeat / minPixelsPerBeat) / std::log (maxPixelsPerBeat / minPixelsPerBeat);
    }

    double sliderToZoom (double value)
    {
        return minPixelsPerBeat * std::pow (maxPixelsPerBeat / minPixelsPerBeat, value);
    }
}

//==============================================================================
ArrangementView::ArrangementView (AudioEngine& e, Selection& s, juce::ApplicationCommandManager& cm)
    : engine (e), selection (s), commands (cm)
{
    ruler = std::make_unique<Ruler> (*this);
    tempoLane = std::make_unique<TempoLane> (*this);
    sectionStrip = std::make_unique<SectionStrip> (*this);
    content = std::make_unique<Content> (*this);
    overlay = std::make_unique<Overlay>();
    playhead = std::make_unique<Playhead> (*this);
    regionDrag = std::make_unique<RegionDrag> (*this);

    addAndMakeVisible (*ruler);
    addChildComponent (*tempoLane);   // hidden until setTempoLaneShown
    addAndMakeVisible (*sectionStrip);

    viewport.setViewedComponent (content.get(), false);
    viewport.setScrollBarsShown (true, false);
    addAndMakeVisible (viewport);

    timeScrollBar.setAutoHide (false);
    timeScrollBar.addListener (this);

    // Both scrollbars sit on the dark arrangement, so they use its colour in
    // either theme (read by IkarosLookAndFeel::drawScrollbar).
    timeScrollBar.getProperties().set ("arrangement", true);
    viewport.getVerticalScrollBar().getProperties().set ("arrangement", true);
    addAndMakeVisible (timeScrollBar);

    // Snap lives above the track headers, next to "TRACKS", styled like the
    // headers' small buttons. (Zoom is in the bottom strip: see resized.)
    // Colours are given as functions the buttons call when they paint (see
    // IconButton::ColourScheme), so they follow the theme.
    auto snapColours = []
    {
        auto c = IconButton::raisedColours();
        c.background = theme().buttonFill;
        c.border = theme().buttonBorder;
        c.borderOn = theme().accent;
        return c;
    };
    snapButton.setColours (snapColours);
    snapButton.setFont (fonts::ui (textSize::xs));
    snapButton.setCornerRadius (size::radiusSmall);
    snapButton.setTooltip ("Snap to grid: where moved regions and edges land");
    snapButton.onClick = [this] { showSnapMenu(); };
    addAndMakeVisible (snapButton);

    // Show Automation: every track's automation at once (amber while it's on).
    auto automationColours = [snapColours]   // like the snap button, amber when on
    {
        auto c = snapColours();
        c.foreground = theme().buttonIcon;
        c.backgroundOn = theme().accent;
        c.foregroundOn = theme().textOnAccent;
        return c;
    };
    automationButton.setColours (automationColours);
    automationButton.setCornerRadius (size::radiusSmall);
    automationButton.setIconSize (16.0f);
    automationButton.setTooltip ("Show automation on every track");
    automationButton.onClick = [this]
    {
        auto& automation = engine.getTracks().automation();
        automation.setAllLanesShown (! automation.areAllLanesShown());
    };
    addAndMakeVisible (automationButton);

    // The tempo lane's button: amber while the lane shows, like automation's.
    tempoLaneButton.setColours (automationColours);
    tempoLaneButton.setCornerRadius (size::radiusSmall);
    tempoLaneButton.setIconSize (15.0f);
    tempoLaneButton.setTooltip ("Show the tempo lane: tempo and time-signature changes");
    tempoLaneButton.onClick = [this] { setTempoLaneShown (! tempoLaneShown); };
    addAndMakeVisible (tempoLaneButton);

    // "+" at the right of the tracks bar: adds a track like the "Add track"
    // row under the last track, but it stays in view however many tracks
    // there are. (The Sections row's button below it is a flag, so this is
    // the only "+" there.)
    newTrackButton.setColours ([snapColours]
    {
        auto c = snapColours();
        c.foreground = theme().buttonIcon;
        return c;
    });
    newTrackButton.setCornerRadius (size::radiusSmall);
    newTrackButton.setIconSize (14.0f);
    newTrackButton.setTooltip ("Add track");
    newTrackButton.onClick = [this] { if (onAddTrack) onAddTrack(); };
    addAndMakeVisible (newTrackButton);

    // Zoom out | reset | zoom in, beside the zoom slider: one step out, back
    // to the starting zoom, one step in (like View > Zoom Out, Reset Zoom,
    // Zoom In). The buttons are transparent; paint() draws their shared
    // outline and the lines between them, like the app's segmented controls.
    for (auto* b : { &zoomOutButton, &zoomResetButton, &zoomInButton })
    {
        b->setColours ([] { return IconButton::Colours { juce::Colours::transparentBlack, theme().buttonIcon,
                                                         juce::Colours::transparentBlack, theme().buttonIcon }; });
        b->setCornerRadius (size::radiusSmall);
        b->setIconSize (13.0f);
        addAndMakeVisible (b);
    }

    zoomOutButton.setIconSize (15.0f);   // magnifiers: a little larger, so the − and + inside them read
    zoomInButton.setIconSize (15.0f);
    zoomOutButton.setTooltip ("Zoom out");
    zoomOutButton.onClick = [this] { zoomOut(); };
    zoomResetButton.setTooltip ("Reset zoom");
    zoomResetButton.setIconSize (12.0f);
    zoomResetButton.onClick = [this] { resetZoom(); };
    zoomInButton.setTooltip ("Zoom in");
    zoomInButton.onClick = [this] { zoomIn(); };

    zoomSlider.setTitle ("Zoom");
    zoomSlider.setTooltip ("Zoom (Ctrl + mouse wheel). Double-click to reset.");
    zoomSlider.setRange (0.0, 1.0);
    zoomSlider.onValueChange = [this] { setZoom (sliderToZoom (zoomSlider.getValue()), getZoomAnchorX ((float) getLaneWidth() * 0.5f)); };
    addAndMakeVisible (zoomSlider);

    addAndMakeVisible (*overlay);
    addAndMakeVisible (*playhead);   // added last so it's drawn on top

    applySnapPreference();
    tracksTall = engine.getPreferences().get (settings::trackHeight) == settings::TrackHeight::tall;
    geometry.rowHeight = getBaseRowHeight();
    setTempoLaneShown (engine.getPreferences().get (settings::tempoLaneShown));
    refresh();
}

// Defined here (not in the header) because the unique_ptr members point to
// classes that are only complete in this file.
ArrangementView::~ArrangementView()
{
    timeScrollBar.removeListener (this);
}

void ArrangementView::refresh()
{
    auto& tc = engine.getTracks();
    tracks = tc.getTracks();

    std::map<TrackId, std::vector<RegionInfo>> regions;

    for (auto& t : tracks)
        regions[t.id] = tc.regions().getRegions (t.id);

    geometry.tempo = engine.getTempoMap();
    sectionStrip->sync();
    updateAutomationButton();
    content->sync (tracks, regions);
    contentEndBeat = content->getEndBeat();
    layoutContent();
    timelineChanged();
}

void ArrangementView::updateAutomationButton()
{
    const bool allShown = ! tracks.empty()
                           && std::all_of (tracks.begin(), tracks.end(), [] (const TrackInfo& t) { return t.automationShown; });
    automationButton.setToggleState (allShown, juce::dontSendNotification);
    automationButton.setTooltip (allShown ? "Hide automation" : "Show automation on every track");
}

void ArrangementView::refreshTracks()
{
    tracks = engine.getTracks().getTracks();
    updateAutomationButton();

    if (! content->syncTracks (tracks))
        refresh();
}

void ArrangementView::refreshRegions (const std::set<RegionId>& ids)
{
    for (auto id : ids)
    {
        auto info = engine.getTracks().regions().getRegion (id);

        if (! info.has_value() || ! content->syncRegion (*info))
        {
            refresh();
            return;
        }
    }

    // A region may now end later (or earlier), which changes how far the
    // timeline scrolls.
    const auto end = content->getEndBeat();

    if (end != contentEndBeat)
    {
        contentEndBeat = end;
        timelineChanged();
    }
}

void ArrangementView::layoutContent()
{
    content->setSize (viewport.getMaximumVisibleWidth(),
                      juce::jmax (viewport.getMaximumVisibleHeight(), content->getIdealHeight()));

    // setSize only triggers resized() when the size actually changes, but rows
    // may have been added or removed, so always re-run the layout.
    content->resized();
}

//==============================================================================
// Zoom and horizontal scroll
int ArrangementView::getLaneWidth() const
{
    return juce::jmax (1, viewport.getMaximumVisibleWidth() - headerWidth);
}

double ArrangementView::getVisibleBeats() const
{
    return getLaneWidth() / geometry.pixelsPerBeat;
}

double ArrangementView::getTotalBeats() const
{
    // You can scroll to 16 bars past the last region (and at least 32 bars in).
    const auto& tempo = geometry.tempo;
    return juce::jmax (tempo.barToBeat (32), tempo.barToBeat (tempo.barAt (contentEndBeat - 1.0e-9) + 1 + 16));
}

void ArrangementView::setZoom (double pixelsPerBeat, float anchorX)
{
    // Keep the beat under anchorX (the mouse, say) in the same place.
    const double anchorBeat = geometry.xToBeat (anchorX);
    geometry.pixelsPerBeat = juce::jlimit (minPixelsPerBeat, maxPixelsPerBeat, pixelsPerBeat);
    geometry.scrollBeats = anchorBeat - anchorX / geometry.pixelsPerBeat;

    // Like GarageBand, zooming in makes the tracks taller too, so regions
    // and waveforms grow both ways. Square root: twice as wide for every
    // four times the zoom. Zoomed out, tracks keep their normal height (the
    // header's controls need it).
    const int oldRowHeight = geometry.rowHeight;
    const double growth = std::sqrt (geometry.pixelsPerBeat / startPixelsPerBeat);
    geometry.rowHeight = juce::jlimit (getBaseRowHeight(), maxRowHeight,
                                       juce::roundToInt (getBaseRowHeight() * growth));

    if (geometry.rowHeight != oldRowHeight)
    {
        // Keep the same tracks in view as they grow or shrink.
        const auto oldY = viewport.getViewPositionY();
        layoutContent();
        viewport.setViewPosition (viewport.getViewPositionX(), oldY * geometry.rowHeight / oldRowHeight);
    }

    timelineChanged();
}

int ArrangementView::getBaseRowHeight() const noexcept
{
    return tracksTall ? size::trackRowTall : size::trackRowCompact;
}

void ArrangementView::setTracksTall (bool shouldBeTall)
{
    tracksTall = shouldBeTall;
    engine.getPreferences().set (settings::trackHeight, shouldBeTall ? settings::TrackHeight::tall
                                                                     : settings::TrackHeight::compact);

    // setZoom works out the row height (from this and the zoom) and lays
    // the rows out again. Anchored at the left edge: nothing scrolls.
    setZoom (geometry.pixelsPerBeat, 0.0f);
}

float ArrangementView::getZoomAnchorX (float fallbackX) const
{
    // Like GarageBand, every zoom (slider, keys, Ctrl + wheel) keeps the
    // playhead where it is, so you stay looking at the part of the song
    // you're working on. Only if it's off screen does the fallback (the
    // middle, or the mouse) stay put instead.
    const auto x = geometry.beatToX (engine.getTransportState().positionBeats);
    return juce::isPositiveAndBelow (x, (float) getLaneWidth()) ? x : fallbackX;
}

void ArrangementView::zoomIn()
{
    setZoom (geometry.pixelsPerBeat * 1.5, getZoomAnchorX ((float) getLaneWidth() * 0.5f));
}

void ArrangementView::zoomOut()
{
    setZoom (geometry.pixelsPerBeat / 1.5, getZoomAnchorX ((float) getLaneWidth() * 0.5f));
}

void ArrangementView::resetZoom()
{
    setZoom (startPixelsPerBeat, getZoomAnchorX ((float) getLaneWidth() * 0.5f));
}

ArrangementView::ViewPosition ArrangementView::getViewPosition() const
{
    if (pendingViewPosition.has_value())
        return *pendingViewPosition;

    return { geometry.pixelsPerBeat, geometry.scrollBeats, viewport.getViewPositionY() };
}

void ArrangementView::setViewPosition (const ViewPosition& position)
{
    if (! zoomInitialised)
    {
        pendingViewPosition = position;
        return;
    }

    pendingViewPosition.reset();

    // Anchored at the left edge, then scrolled: setZoom also sets the
    // tracks' height, which the vertical scroll below depends on.
    setZoom (position.pixelsPerBeat > 0.0 ? position.pixelsPerBeat : startPixelsPerBeat, 0.0f);
    scrollToBeat (position.scrollBeats);
    viewport.setViewPosition (0, position.scrollY);
}

void ArrangementView::scrollToBeat (double beat)
{
    geometry.scrollBeats = beat;
    timelineChanged();
}

void ArrangementView::timelineChanged()
{
    geometry.scrollBeats = juce::jlimit (0.0, juce::jmax (0.0, getTotalBeats() - getVisibleBeats()), geometry.scrollBeats);

    content->timelineChanged();
    ruler->repaint();
    tempoLane->repaint();
    sectionStrip->repaint();
    updateScrollBar();
    zoomSlider.setValue (zoomToSlider (geometry.pixelsPerBeat), juce::dontSendNotification);
    playhead->setX (geometry.beatToX (engine.getTransportState().positionBeats));
}

void ArrangementView::updateScrollBar()
{
    // setRangeLimits/setCurrentRange call scrollBarMoved asynchronously only
    // when the user moves it, so this doesn't loop back.
    timeScrollBar.setRangeLimits (0.0, juce::jmax (getTotalBeats(), geometry.scrollBeats + getVisibleBeats()),
                                  juce::dontSendNotification);
    timeScrollBar.setCurrentRange (geometry.scrollBeats, getVisibleBeats(), juce::dontSendNotification);
    timeScrollBar.setSingleStepSize (geometry.tempo.beatsPerBarAt (geometry.scrollBeats));
}

void ArrangementView::scrollBarMoved (juce::ScrollBar*, double newRangeStart)
{
    scrollToBeat (newRangeStart);
}

bool ArrangementView::handleWheel (const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel, bool wheelScrollsTime)
{
    auto e = event.getEventRelativeTo (this);

    if (e.mods.isCommandDown() && engine.getPreferences().get (settings::wheelMode) == settings::Wheel::zoomWithModifier)
    {
        // Ctrl/Cmd + wheel: zoom around the playhead (or the mouse, if the
        // playhead is off screen). One wheel notch zooms ~1.4x.
        const float anchorX = getZoomAnchorX ((float) (e.x - headerWidth));
        setZoom (geometry.pixelsPerBeat * std::pow (2.0, (double) wheel.deltaY * 2.5), anchorX);
        return true;
    }

    // Shift + wheel (or a sideways swipe on a touchpad) scrolls in time.
    const bool sideways = wheelScrollsTime || e.mods.isShiftDown();
    float amount = wheel.deltaX;

    if (sideways && amount == 0.0f)
        amount = wheel.deltaY;

    if (amount == 0.0f || (! sideways && std::abs (wheel.deltaX) < std::abs (wheel.deltaY)))
        return false;

    if (wheel.isReversed)
        amount = -amount;

    scrollToBeat (geometry.scrollBeats - amount * 300.0 / geometry.pixelsPerBeat);
    return true;
}

//==============================================================================
// Snap
void ArrangementView::setSnap (Snap newSnap)
{
    snap = newSnap;
    snapButton.setText (snap == Snap::bar ? "Snap: Bar" : snap == Snap::beat ? "Snap: Beat" : "Snap: Off");

    if (onSnapChanged)
        onSnapChanged();
}

void ArrangementView::applySnapPreference()
{
    auto& prefs = engine.getPreferences();

    if (! prefs.get (settings::snapEnabled))
        setSnap (Snap::off);
    else
        setSnap (prefs.get (settings::snapGrid) == settings::Grid::bar ? Snap::bar : Snap::beat);
}

double ArrangementView::getSnapBeats (double atBeat) const
{
    return snap == Snap::bar ? geometry.tempo.beatsPerBarAt (atBeat) : snap == Snap::beat ? 1.0 : 0.0;
}

double ArrangementView::snapBeat (double beat) const
{
    // Bars can differ in length, so the bar grid comes from the tempo map.
    if (snap == Snap::bar)
        return geometry.tempo.nearestBarLine (beat);

    return snap == Snap::beat ? std::round (beat) : beat;
}

void ArrangementView::setTempoLaneShown (bool shouldShow)
{
    tempoLaneShown = shouldShow;
    tempoLane->setVisible (shouldShow);
    tempoLaneButton.setToggleState (shouldShow, juce::dontSendNotification);
    engine.getPreferences().set (settings::tempoLaneShown, shouldShow);
    resized();

    if (onTempoLaneToggled)
        onTempoLaneToggled();
}

void ArrangementView::addTempoMenuItems (juce::PopupMenu& menu, double beat)
{
    const auto& tempo = geometry.tempo;
    const int bar = tempo.nearestBar (beat);
    const double barBeat = tempo.barToBeat (bar);
    const auto barName = "Bar " + juce::String (bar + 1);   // bars show from 1

    // The lambdas run after the menu closes; AudioEngine outlives every menu,
    // so capturing it by reference is safe.
    auto& e = engine;

    const int tempoIndex = tempo.indexOfTempoAt (barBeat);
    const bool tempoHere = std::abs (tempo.getTempoChanges()[(size_t) tempoIndex].beat - barBeat) < 1.0e-6;

    if (tempoHere && tempoIndex > 0)
        menu.addItem ("Delete Tempo Change at " + barName, [&e, tempoIndex] { e.removeTempoChange (tempoIndex); });
    else if (! tempoHere)
        menu.addItem ("Add Tempo Change at " + barName, [&e, bar, bpm = tempo.tempoAt (barBeat).bpm] { e.addTempoChange (bar, bpm); });

    const int sigIndex = tempo.indexOfSignatureAt (barBeat);
    const auto& sig = tempo.getTimeSignatureChanges()[(size_t) sigIndex];
    const bool sigHere = sig.bar == bar;
    juce::PopupMenu signatures;

    for (auto& choice : TempoMap::commonSignatures())
    {
        const int n = choice.first, d = choice.second;
        signatures.addItem (juce::String (n) + "/" + juce::String (d), true, sigHere && sig.numerator == n && sig.denominator == d,
                            [&e, bar, n, d] { e.addTimeSignatureChange (bar, n, d); });   // or changes the one there
    }

    menu.addSubMenu (sigHere ? "Time Signature at " + barName : "Add Time Signature Change at " + barName, signatures);

    if (sigHere && sigIndex > 0)
        menu.addItem ("Delete Time Signature Change at " + barName, [&e, sigIndex] { e.removeTimeSignatureChange (sigIndex); });

    menu.addSeparator();
    menu.addItem (tempoLaneShown ? "Hide Tempo Lane" : "Show Tempo Lane", [this] { setTempoLaneShown (! tempoLaneShown); });
}

void ArrangementView::showSnapMenu()
{
    juce::PopupMenu menu;
    menu.addSectionHeader ("Snap to grid");

    // PopupMenu::Item lets each entry run its own lambda.
    auto add = [&] (const juce::String& name, Snap value)
    {
        menu.addItem (juce::PopupMenu::Item (name).setTicked (snap == value).setAction ([this, value] { setSnap (value); }));
    };

    add ("Bar", Snap::bar);
    add ("Beat", Snap::beat);
    add ("Off", Snap::off);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&snapButton));
}

//==============================================================================
void ArrangementView::updateTransport()
{
    content->updateMeters();

    auto state = engine.getTransportState();
    ruler->setLoop (state.looping, state.loopStartBeats, state.loopEndBeats);

    auto x = geometry.beatToX (state.positionBeats);
    const auto width = (float) getLaneWidth();

    // While playing, turn the page when the playhead leaves the screen (it
    // ran off the right edge, or jumped back to the start of a loop), but
    // not while the user is dragging something, or if they turned it off.
    const bool wasVisible = lastPlayheadX >= 0.0f && lastPlayheadX <= width - 20.0f;
    const bool isVisible = x >= 0.0f && x <= width - 20.0f;

    if (state.playing && wasVisible && ! isVisible && ! regionDrag->isDragging()
         && engine.getPreferences().get (settings::followPlayhead)
         && ! juce::Component::isMouseButtonDownAnywhere())
    {
        scrollToBeat (state.positionBeats - getVisibleBeats() * 0.05);
        x = geometry.beatToX (state.positionBeats);
    }

    lastPlayheadX = x;
    playhead->setX (x);

    // The live waveform first, so the recording region repaints with it.
    engine.updateLiveRecording();

    if (state.recording)
        content->setRecording (engine.getTracks().inputs().getArmedTrack(), state.recordStartBeats, state.positionBeats);
    else
        content->setRecording (0, 0.0, 0.0);
}

void ArrangementView::paint (juce::Graphics& g)
{
    g.fillAll (theme().arrangement.laneBg);   // under the lanes: the scrollbar strip

    // The bottom strip: a line along its top, and under the track headers
    // the column's background (the zoom slider sits there), with its divider.
    auto strip = getLocalBounds().removeFromBottom (bottomBarHeight);
    auto corner = strip.withWidth (headerWidth);
    g.setColour (theme().trackColumnBg);
    g.fillRect (corner.withTrimmedRight (1));
    g.setColour (theme().divider);
    g.fillRect (corner.removeFromRight (1));
    g.setColour (theme().trackRowLine);
    g.fillRect (strip.removeFromTop (1));

    // The zoom buttons: one outline around all three, a line between each.
    const auto group = zoomButtonsArea.toFloat();
    g.setColour (theme().buttonFill);
    g.fillRoundedRectangle (group, size::radiusSmall);
    g.setColour (theme().buttonBorder);
    g.drawRoundedRectangle (group.reduced (0.5f), size::radiusSmall, 1.0f);

    for (auto* b : { &zoomResetButton, &zoomInButton })
        g.fillRect (b->getX() - 1, zoomButtonsArea.getY() + 4, 1, zoomButtonsArea.getHeight() - 8);
}

void ArrangementView::paintOverChildren (juce::Graphics& g)
{
    icons::draw (g, Icon::search, zoomIconArea.toFloat(), theme().textSecondary, 13.0f, 1.6f);
}

void ArrangementView::resized()
{
    auto r = getLocalBounds();
    auto top = r.removeFromTop (size::rulerHeight);
    ruler->setBounds (top);

    if (tempoLaneShown)
        tempoLane->setBounds (r.removeFromTop (tempoLaneHeight));

    sectionStrip->setBounds (r.removeFromTop (sectionStripHeight));

    // The tracks bar, in the ruler's header part: "TRACKS" | snap |
    // automation | tempo lane ... add track. It has the column's padding,
    // and the "+" is centred in the column the track headers' arm buttons
    // and pan knobs use (as is the Sections row's button below it). Zoom
    // is in the bottom strip, where it has room for fine control.
    {
        auto bar = juce::Rectangle<int> (columnPadding, top.getY(), columnRight - columnPadding, top.getHeight() - 1)
                       .withSizeKeepingCentre (columnRight - columnPadding, 22);
        bar.removeFromLeft (columnLabelWidth ("TRACKS") + 12);
        newTrackButton.setBounds (bar.removeFromRight (TrackHeader::controlColumnWidth).withSizeKeepingCentre (24, 22));
        bar.removeFromRight (8);
        snapButton.setBounds (bar.removeFromLeft (72));
        bar.removeFromLeft (8);
        automationButton.setBounds (bar.removeFromLeft (24));
        bar.removeFromLeft (8);
        tempoLaneButton.setBounds (bar.removeFromLeft (24));
    }

    // The bottom strip: zoom under the track headers, with the column's
    // padding so it lines up with the controls above, and the time scroll
    // bar beside it. Zooming and scrolling the timeline live together.
    {
        auto strip = r.removeFromBottom (bottomBarHeight);
        auto zoomArea = strip.removeFromLeft (headerWidth).withTrimmedTop (1);   // under the strip's top line
        zoomArea = { columnPadding, zoomArea.getY(), columnRight - columnPadding, zoomArea.getHeight() };

        // The zoom buttons on the right, ending where the controls above end;
        // then a gap. Each button is 26 wide; the 1px lines between them
        // come out of that width (see paint).
        constexpr int zoomButtonWidth = 26;
        zoomButtonsArea = zoomArea.removeFromRight (zoomButtonWidth * 3).withSizeKeepingCentre (zoomButtonWidth * 3, 22);
        auto inside = zoomButtonsArea.reduced (1);   // inside the outline, so a hovered button doesn't cover it
        zoomOutButton.setBounds (inside.removeFromLeft (zoomButtonWidth - 1));
        inside.removeFromLeft (1);
        zoomResetButton.setBounds (inside.removeFromLeft (zoomButtonWidth - 2));
        inside.removeFromLeft (1);
        zoomInButton.setBounds (inside);
        zoomArea.removeFromRight (20);

        zoomIconArea = zoomArea.removeFromLeft (14).withSizeKeepingCentre (14, 14);
        zoomArea.removeFromLeft (8);
        zoomSlider.setBounds (zoomArea.withSizeKeepingCentre (zoomArea.getWidth(), 22));

        timeScrollBar.setBounds (strip.withTrimmedTop (1).withSizeKeepingCentre (strip.getWidth(), scrollBarHeight));
    }
    viewport.setBounds (r);

    // The first time there's room, zoom so 16 bars fill the lanes (as milestone 1 did).
    if (! zoomInitialised && getLaneWidth() > 100)
    {
        zoomInitialised = true;
        geometry.tempo = engine.getTempoMap();
        geometry.pixelsPerBeat = juce::jlimit (minPixelsPerBeat, maxPixelsPerBeat,
                                               getLaneWidth() / geometry.tempo.barToBeat (16));
        startPixelsPerBeat = geometry.pixelsPerBeat;

        // Double-clicking the slider resets it (to here). The slider runs
        // 0..1, so the value it returns to is this zoom's slider position.
        zoomSlider.setDoubleClickReturnValue (true, zoomToSlider (startPixelsPerBeat));
    }

    layoutContent();

    if (zoomInitialised && pendingViewPosition.has_value())
        setViewPosition (*pendingViewPosition);

    // Overlays: ghosts over the lanes; the playhead over the ruler and lanes,
    // but not the track headers column.
    overlay->setBounds (headerWidth, r.getY(), getLaneWidth(), viewport.getMaximumVisibleHeight());
    playhead->setBounds (headerWidth, 0, getLaneWidth(), r.getBottom());

    timelineChanged();
}

//==============================================================================
// Region gestures
RegionComponent* ArrangementView::findRegion (RegionId id) const
{
    return content->findRegion (id);
}

int ArrangementView::rowOf (TrackId id) const
{
    for (size_t i = 0; i < tracks.size(); ++i)
        if (tracks[i].id == id)
            return (int) i;

    return -1;
}

int ArrangementView::rowTop (int row) const
{
    int y = 0;

    for (int i = 0; i < row && i < (int) tracks.size(); ++i)
        y += rowHeightOf (i);

    return y;
}

int ArrangementView::rowHeightOf (int row) const
{
    const bool automationShown = juce::isPositiveAndBelow (row, (int) tracks.size()) && tracks[(size_t) row].automationShown;
    return geometry.rowHeight + (automationShown ? automationPickerHeight : 0);
}

int ArrangementView::getTracksHeight() const
{
    return rowTop ((int) tracks.size());
}

int ArrangementView::rowAt (int y) const
{
    const int contentY = y - viewport.getY() + viewport.getViewPositionY();
    int bottom = 0;

    for (int row = 0; row < (int) tracks.size(); ++row)
    {
        bottom += rowHeightOf (row);

        if (contentY < bottom)
            return row;
    }

    return juce::jmax (0, (int) tracks.size() - 1);
}

juce::Rectangle<float> ArrangementView::rowAreaInOverlay (int row) const
{
    return { 0.0f, (float) (rowTop (row) - viewport.getViewPositionY()), (float) getLaneWidth(), (float) rowHeightOf (row) };
}

void ArrangementView::regionMouseDown (RegionComponent& comp, const juce::MouseEvent& e)   { regionDrag->regionMouseDown (comp, e); }
void ArrangementView::regionMouseDrag (RegionComponent& comp, const juce::MouseEvent& e)   { regionDrag->regionMouseDrag (comp, e); }
void ArrangementView::regionMouseUp (RegionComponent& comp, const juce::MouseEvent& e)     { regionDrag->regionMouseUp (comp, e); }

void ArrangementView::regionDoubleClicked (RegionComponent& comp, RegionComponent::Zone zone)
{
    if (zone == RegionComponent::Zone::nameStrip)
    {
        comp.startRenaming();
        return;
    }

    const auto& info = comp.getInfo();
    selection.selectRegion (info.trackId, info.id);

    if (onOpenRegionInEditor)
        onOpenRegionInEditor (info.id);
}

void ArrangementView::showTakeMenu (RegionComponent& comp)
{
    const auto info = comp.getInfo();
    selection.selectRegion (info.trackId, info.id);

    auto& takes = engine.getTracks().takes();
    juce::PopupMenu menu;
    int itemId = 1;   // PopupMenu item IDs must not be 0

    // The takes first (ticked: the one playing), then what can be done with them.
    for (auto& take : takes.getTakes (info.id))
    {
        const int index = itemId - 1;
        menu.addItem (juce::PopupMenu::Item (take.name)
                          .setID (itemId++)
                          .setTicked (take.current)
                          .setAction ([&takes, id = info.id, index] { takes.setCurrentTake (id, index); }));
    }

    menu.addSeparator();
    menu.addItem (juce::PopupMenu::Item ("Delete Unused Takes")
                      .setID (itemId++)
                      .setEnabled (info.takeCount > 1)
                      .setAction ([&takes, id = info.id] { takes.deleteUnusedTakes (id); }));
    menu.addItem (juce::PopupMenu::Item ("Flatten (Keep Only This Take)")
                      .setID (itemId++)
                      .setAction ([&takes, id = info.id] { takes.flattenTakes (id); }));

    // Async: menus must not block the message loop. The actions capture the
    // region's ID, not the component, which may be rebuilt meanwhile.
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&comp)
                                                  .withTargetScreenArea (comp.localAreaToGlobal (comp.getTakeBadgeBounds())));
}

void ArrangementView::showRegionMenu()
{
    // The same commands as the Region menu, so names, shortcuts and enabled
    // state all come from one place (MainComponent::getCommandInfo).
    juce::PopupMenu menu;

    for (auto id : { CommandIDs::splitRegion, CommandIDs::duplicateRegion })
        menu.addCommandItem (&commands, id);

    menu.addSeparator();

    for (auto id : { CommandIDs::cutRegions, CommandIDs::copyRegions, CommandIDs::pasteRegions })
        menu.addCommandItem (&commands, id);

    menu.addSeparator();
    menu.addCommandItem (&commands, CommandIDs::renameRegion);
    menu.addCommandItem (&commands, CommandIDs::deleteRegion);
    menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition());
}

void ArrangementView::startRenaming (RegionId id)
{
    auto region = engine.getTracks().regions().getRegion (id);
    const int row = region ? rowOf (region->trackId) : -1;

    if (row < 0)
        return;

    // Scroll the region into view first, so the text field can be seen.
    const double visibleEnd = geometry.scrollBeats + getVisibleBeats();

    if (region->startBeat < geometry.scrollBeats || region->startBeat > visibleEnd - 1.0)
        scrollToBeat (region->startBeat - geometry.tempo.beatsPerBarAt (region->startBeat));

    scrollRowIntoView (row);

    if (auto* comp = findRegion (id))
        comp->startRenaming();
}

void ArrangementView::startRenamingTrack (TrackId id)
{
    const int row = rowOf (id);

    if (row < 0)
        return;

    scrollRowIntoView (row);

    if (auto* header = content->findHeader (id))
        header->startRenaming();
}

juce::Component* ArrangementView::getArmButton (TrackId id)
{
    const int row = rowOf (id);

    if (row < 0)
        return nullptr;

    scrollRowIntoView (row);

    if (auto* header = content->findHeader (id))
        return &header->getArmButton();

    return nullptr;
}

void ArrangementView::scrollRowIntoView (int row)
{
    const int rowY = rowTop (row);

    if (rowY < viewport.getViewPositionY() || rowY + rowHeightOf (row) > viewport.getViewPositionY() + viewport.getMaximumVisibleHeight())
        viewport.setViewPosition (0, rowY);
}

//==============================================================================
// Track context menus
juce::PopupMenu ArrangementView::createTrackMenu()
{
    // Commands, as in the Track menu, so names, shortcuts and enabled state
    // come from one place (MainComponent::getCommandInfo).
    juce::PopupMenu menu;
    const auto track = selection.getTrack();

    for (auto id : { CommandIDs::newTrack, CommandIDs::newAudioTrack,
                     CommandIDs::duplicateTrackSettings, CommandIDs::duplicateTrack })
        menu.addCommandItem (&commands, id);

    menu.addSeparator();
    menu.addCommandItem (&commands, CommandIDs::renameTrack);
    menu.addSubMenu ("Track Icon", TrackHeader::createIconMenu (engine, track), track != 0);
    menu.addCommandItem (&commands, CommandIDs::moveTrackUp);
    menu.addCommandItem (&commands, CommandIDs::moveTrackDown);
    menu.addSeparator();

    for (auto id : { CommandIDs::muteTrack, CommandIDs::soloTrack, CommandIDs::armTrack })
        menu.addCommandItem (&commands, id);

    menu.addSeparator();
    menu.addCommandItem (&commands, CommandIDs::deleteTrack);
    return menu;
}

void ArrangementView::showTrackMenu()
{
    createTrackMenu().showMenuAsync (juce::PopupMenu::Options().withMousePosition());
}

void ArrangementView::showLaneMenu (TrackId track, double beat)
{
    auto& tc = engine.getTracks();
    const auto info = tc.getTrack (track);

    if (! info)
        return;

    const auto at = snapBeat (juce::jmax (0.0, beat));
    const auto regions = tc.regions().getRegions (track);
    juce::PopupMenu menu;

    if (info->kind == TrackKind::instrument)
    {
        menu.addItem (juce::PopupMenu::Item ("Add Chord Region Here").setAction ([this, track, at]
        {
            if (auto rid = engine.getTracks().regions().createChordRegion (track, at))
                selection.selectRegion (track, rid);
        }));
    }

    menu.addItem (juce::PopupMenu::Item ("Paste Here").setEnabled (tc.regions().canPaste()).setAction ([this, track, at]
    {
        auto pasted = engine.getTracks().regions().pasteRegions (track, at);

        if (auto first = pasted.empty() ? std::nullopt : engine.getTracks().regions().getRegion (pasted.front()))
            selection.selectRegions (first->trackId, pasted);
    }));

    menu.addItem (juce::PopupMenu::Item ("Select All Regions on Track").setEnabled (! regions.empty()).setAction ([this, track]
    {
        std::vector<RegionId> ids;

        for (auto& r : engine.getTracks().regions().getRegions (track))
            ids.push_back (r.id);

        selection.selectRegions (track, ids);
    }));

    menu.addItem (juce::PopupMenu::Item ("Move Playhead Here").setAction ([this, at] { engine.setPositionBeats (at); }));
    menu.addSeparator();
    menu.addSubMenu ("Track", createTrackMenu());
    menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition());
}

//==============================================================================
void ArrangementView::dragPlayheadTo (float laneX)
{
    const auto width = (float) getLaneWidth();

    // Past an edge: scroll by a fraction of the overshoot, so the further out
    // the mouse is, the faster the timeline scrolls.
    if (laneX < 0.0f || laneX > width)
    {
        const float overshoot = laneX < 0.0f ? laneX : laneX - width;
        scrollToBeat (geometry.scrollBeats + overshoot * 0.25f / geometry.pixelsPerBeat);
    }

    const auto beat = snapBeat (juce::jmax (0.0, geometry.xToBeat (juce::jlimit (0.0f, width, laneX))));
    engine.setPositionBeats (beat);
    playhead->setX (geometry.beatToX (beat));
}

//==============================================================================
// Audio files dragged in from the file manager
ArrangementView::DropTarget ArrangementView::findDropTarget (int x, int y) const
{
    DropTarget target;
    target.beat = snapBeat (juce::jmax (0.0, geometry.xToBeat ((float) (juce::jmax (x, headerWidth) - headerWidth))));

    const int contentY = y - viewport.getY() + viewport.getViewPositionY();

    if (y >= viewport.getY() && contentY < getTracksHeight())
    {
        target.row = rowAt (y);

        // On an audio track: add to it. On an instrument track: a new audio track.
        if (tracks[(size_t) target.row].kind == TrackKind::audio)
            target.track = tracks[(size_t) target.row].id;
    }

    return target;
}

bool ArrangementView::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files)
        if (engine.getTracks().regions().isSupportedAudioFile (juce::File (f)))
            return true;

    return false;
}

void ArrangementView::fileDragEnter (const juce::StringArray& files, int x, int y)
{
    fileDragMove (files, x, y);
}

void ArrangementView::fileDragMove (const juce::StringArray&, int x, int y)
{
    auto target = findDropTarget (x, y);
    juce::Rectangle<float> area;
    juce::String text;

    if (target.track != 0)
    {
        area = rowAreaInOverlay (target.row).reduced (2.0f);
        text = "Add to " + tracks[(size_t) target.row].name;
    }
    else if (target.row >= 0)
    {
        area = rowAreaInOverlay (target.row).reduced (2.0f);
        text = "Add on a new audio track";
    }
    else
    {
        // Below the tracks: the drop zone.
        area = { (float) dropZoneMargin, (float) (getTracksHeight() + dropZoneMargin - viewport.getViewPositionY()),
                 (float) (getLaneWidth() - dropZoneMargin * 2), (float) dropZoneHeight };
        text = "Add on a new audio track";
    }

    overlay->setDropTarget (area, geometry.beatToX (target.beat), text);
}

void ArrangementView::fileDragExit (const juce::StringArray&)
{
    overlay->clear();
}

//==============================================================================
namespace
{
    // The files in a text drag: lines that are file:// links or full paths.
    juce::StringArray filesFromText (const juce::String& text)
    {
        juce::StringArray files;

        for (auto line : juce::StringArray::fromLines (text))
        {
            line = line.trim();

            if (line.startsWithIgnoreCase ("file://"))
                line = juce::URL (line).getLocalFile().getFullPathName();

            if (juce::File::isAbsolutePath (line) && juce::File (line).existsAsFile())
                files.add (line);
        }

        return files;
    }
}

bool ArrangementView::isInterestedInTextDrag (const juce::String& text)
{
    return isInterestedInFileDrag (filesFromText (text));
}

void ArrangementView::textDragEnter (const juce::String& text, int x, int y)  { fileDragEnter (filesFromText (text), x, y); }
void ArrangementView::textDragMove (const juce::String& text, int x, int y)   { fileDragMove (filesFromText (text), x, y); }
void ArrangementView::textDragExit (const juce::String& text)                 { fileDragExit (filesFromText (text)); }
void ArrangementView::textDropped (const juce::String& text, int x, int y)    { filesDropped (filesFromText (text), x, y); }

//==============================================================================
// Loops and audio files dragged from the library: the same as dropping a
// file from the file manager.
bool ArrangementView::isInterestedInDragSource (const SourceDetails& details)
{
    return dragtypes::getLibraryFile (details.description).existsAsFile()
        || dragtypes::getLibrarySound (details.description).isNotEmpty();
}

void ArrangementView::itemDragEnter (const SourceDetails& details)
{
    itemDragMove (details);
}

// A sound goes on the instrument track it's dropped on; anywhere else, it
// makes a new instrument track. Returns that track's row, or -1 for a new one.
int ArrangementView::findSoundDropRow (int y) const
{
    const int contentY = y - viewport.getY() + viewport.getViewPositionY();

    if (y < viewport.getY() || contentY >= getTracksHeight())
        return -1;

    const int row = rowAt (y);
    return tracks[(size_t) row].kind == TrackKind::instrument ? row : -1;
}

void ArrangementView::itemDragMove (const SourceDetails& details)
{
    const auto soundId = dragtypes::getLibrarySound (details.description);

    if (soundId.isEmpty())
    {
        fileDragMove ({}, details.localPosition.x, details.localPosition.y);
        return;
    }

    const int row = findSoundDropRow (details.localPosition.y);
    juce::String name ("this sound");

    if (auto sound = engine.getSounds().findSound (soundId))
        name = sound->name;
    else if (auto plugin = engine.getPlugins().findPlugin (soundId.fromFirstOccurrenceOf ("plugin:", false, false)))
        name = plugin->name;

    if (row >= 0)
    {
        overlay->setDropTarget (rowAreaInOverlay (row).reduced (2.0f), -1.0f, "Use " + name + " on " + tracks[(size_t) row].name);
    }
    else
    {
        // The drop zone below the tracks.
        juce::Rectangle<float> area { (float) dropZoneMargin, (float) (getTracksHeight() + dropZoneMargin - viewport.getViewPositionY()),
                                      (float) (getLaneWidth() - dropZoneMargin * 2), (float) dropZoneHeight };
        overlay->setDropTarget (area, -1.0f, "New instrument track with " + name);
    }
}

void ArrangementView::itemDragExit (const SourceDetails&)
{
    overlay->clear();
}

void ArrangementView::itemDropped (const SourceDetails& details)
{
    overlay->clear();
    const auto soundId = dragtypes::getLibrarySound (details.description);

    if (soundId.isNotEmpty())
    {
        auto& tc = engine.getTracks();
        const int row = findSoundDropRow (details.localPosition.y);

        if (row >= 0)
        {
            tc.effects().setSound (tracks[(size_t) row].id, soundId);
            selection.selectTrack (tracks[(size_t) row].id);
        }
        else if (auto id = tc.addInstrumentTrack())
        {
            tc.effects().setSound (id, soundId, false);   // part of the "Add Track" undo step
            selection.selectTrack (id);
        }

        return;
    }

    engine.getPreview().stop();
    filesDropped ({ dragtypes::getLibraryFile (details.description).getFullPathName() },
                  details.localPosition.x, details.localPosition.y);
}

void ArrangementView::filesDropped (const juce::StringArray& files, int x, int y)
{
    overlay->clear();
    auto target = findDropTarget (x, y);
    auto& tc = engine.getTracks();
    auto ids = tc.regions().importAudioFiles (target.track, files, target.beat);

    if (ids.empty())
        return;

    refresh();

    if (auto first = tc.regions().getRegion (ids.front()))
        selection.selectRegions (first->trackId, ids);
}

} // namespace ikaros
