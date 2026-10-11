#include "ui/arrangement/ArrangementView.h"
#include "ui/arrangement/ArrangementContent.h"
#include "ui/arrangement/ArrangementLayout.h"
#include "ui/arrangement/ArrangementOverlays.h"
#include "ui/arrangement/ArrangementRegionDrag.h"
#include "ui/arrangement/ArrangementRuler.h"
#include "app/CommandIDs.h"
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
    content = std::make_unique<Content> (*this);
    overlay = std::make_unique<Overlay>();
    playhead = std::make_unique<Playhead> (*this);
    regionDrag = std::make_unique<RegionDrag> (*this);

    addAndMakeVisible (*ruler);

    viewport.setViewedComponent (content.get(), false);
    viewport.setScrollBarsShown (true, false);
    addAndMakeVisible (viewport);

    timeScrollBar.setAutoHide (false);
    timeScrollBar.addListener (this);
    addAndMakeVisible (timeScrollBar);

    // Snap and zoom live above the track headers, next to "TRACKS".
    auto snapColours = IconButton::raisedColours();
    snapColours.border = colour::border;
    snapButton.setColours (snapColours);
    snapButton.setFont (fonts::ui (12.0f));
    snapButton.setCornerRadius (size::radiusSmall);
    snapButton.setTooltip ("Snap to grid: where moved regions and edges land");
    snapButton.onClick = [this] { showSnapMenu(); };
    addAndMakeVisible (snapButton);

    zoomSlider.setTitle ("Zoom");
    zoomSlider.setTooltip ("Zoom (Ctrl + mouse wheel)");
    zoomSlider.setRange (0.0, 1.0);
    zoomSlider.onValueChange = [this] { setZoom (sliderToZoom (zoomSlider.getValue()), (float) getLaneWidth() * 0.5f); };
    addAndMakeVisible (zoomSlider);

    addAndMakeVisible (*overlay);
    addAndMakeVisible (*playhead);   // added last so it's drawn on top

    setSnap (Snap::beat);
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
    contentEndBeat = 0.0;

    for (auto& t : tracks)
    {
        auto& list = regions[t.id] = tc.getRegions (t.id);

        for (auto& r : list)
            contentEndBeat = juce::jmax (contentEndBeat, r.getEndBeat());
    }

    geometry.beatsPerBar = engine.beatsPerBar();
    content->sync (tracks, regions);
    layoutContent();
    timelineChanged();
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
    const double bpb = geometry.beatsPerBar;
    return juce::jmax (32.0 * bpb, (std::ceil (contentEndBeat / bpb) + 16.0) * bpb);
}

void ArrangementView::setZoom (double pixelsPerBeat, float anchorX)
{
    // Keep the beat under anchorX (the mouse, say) in the same place.
    const double anchorBeat = geometry.xToBeat (anchorX);
    geometry.pixelsPerBeat = juce::jlimit (minPixelsPerBeat, maxPixelsPerBeat, pixelsPerBeat);
    geometry.scrollBeats = anchorBeat - anchorX / geometry.pixelsPerBeat;
    timelineChanged();
}

void ArrangementView::zoomIn()
{
    // Zoom around the playhead if it's on screen, or else the middle.
    auto x = geometry.beatToX (engine.getTransportState().positionBeats);
    setZoom (geometry.pixelsPerBeat * 1.5, juce::isPositiveAndBelow (x, (float) getLaneWidth()) ? x : (float) getLaneWidth() * 0.5f);
}

void ArrangementView::zoomOut()
{
    auto x = geometry.beatToX (engine.getTransportState().positionBeats);
    setZoom (geometry.pixelsPerBeat / 1.5, juce::isPositiveAndBelow (x, (float) getLaneWidth()) ? x : (float) getLaneWidth() * 0.5f);
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
    timeScrollBar.setSingleStepSize (geometry.beatsPerBar);
}

void ArrangementView::scrollBarMoved (juce::ScrollBar*, double newRangeStart)
{
    scrollToBeat (newRangeStart);
}

bool ArrangementView::handleWheel (const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel, bool wheelScrollsTime)
{
    auto e = event.getEventRelativeTo (this);

    if (e.mods.isCommandDown())
    {
        // Ctrl/Cmd + wheel: zoom around the mouse. One wheel notch zooms ~1.4×.
        const float anchorX = (float) (e.x - headerWidth);
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

double ArrangementView::snapBeat (double beat) const
{
    const double grid = snap == Snap::bar ? geometry.beatsPerBar : snap == Snap::beat ? 1.0 : 0.0;
    return grid > 0.0 ? std::round (beat / grid) * grid : beat;
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
    auto state = engine.getTransportState();
    ruler->setLoop (state.looping, state.loopStartBeats, state.loopEndBeats);

    auto x = geometry.beatToX (state.positionBeats);
    const auto width = (float) getLaneWidth();

    // While playing, turn the page when the playhead leaves the screen (it
    // ran off the right edge, or jumped back to the start of a loop), but
    // not while the user is dragging something.
    const bool wasVisible = lastPlayheadX >= 0.0f && lastPlayheadX <= width - 20.0f;
    const bool isVisible = x >= 0.0f && x <= width - 20.0f;

    if (state.playing && wasVisible && ! isVisible && ! regionDrag->isDragging()
         && ! juce::Component::isMouseButtonDownAnywhere())
    {
        scrollToBeat (state.positionBeats - getVisibleBeats() * 0.05);
        x = geometry.beatToX (state.positionBeats);
    }

    lastPlayheadX = x;
    playhead->setX (x);

    if (state.recording)
        content->setRecording (engine.getTracks().getArmedTrack(), state.recordStartBeats, state.positionBeats);
    else
        content->setRecording (0, 0.0, 0.0);
}

void ArrangementView::paint (juce::Graphics& g)
{
    g.fillAll (colour::windowBg);

    // The corner under the track headers, beside the scroll bar.
    auto corner = getLocalBounds().removeFromBottom (scrollBarHeight).withWidth (headerWidth);
    g.setColour (colour::sidebarBg);
    g.fillRect (corner.withTrimmedRight (1));
    g.setColour (colour::divider);
    g.fillRect (corner.removeFromRight (1));
}

void ArrangementView::paintOverChildren (juce::Graphics& g)
{
    icons::draw (g, Icon::search, zoomIconArea.toFloat(), colour::textSecondary, 13.0f, 1.6f);
}

void ArrangementView::resized()
{
    auto r = getLocalBounds();
    auto top = r.removeFromTop (size::rulerHeight);
    ruler->setBounds (top);

    // Controls in the ruler's header part: "TRACKS" | snap | zoom.
    snapButton.setBounds (68, top.getY() + 4, 90, top.getHeight() - 9);
    zoomIconArea = { 166, top.getY() + 8, 14, 14 };
    zoomSlider.setBounds (182, top.getY() + 4, headerWidth - 182 - 10, top.getHeight() - 9);

    timeScrollBar.setBounds (r.removeFromBottom (scrollBarHeight).withTrimmedLeft (headerWidth));
    viewport.setBounds (r);

    // The first time there's room, zoom so 16 bars fill the lanes (as milestone 1 did).
    if (! zoomInitialised && getLaneWidth() > 100)
    {
        zoomInitialised = true;
        geometry.beatsPerBar = engine.beatsPerBar();
        geometry.pixelsPerBeat = juce::jlimit (minPixelsPerBeat, maxPixelsPerBeat,
                                               getLaneWidth() / (16.0 * geometry.beatsPerBar));
    }

    layoutContent();

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

int ArrangementView::rowAt (int y) const
{
    const int contentY = y - viewport.getY() + viewport.getViewPositionY();
    return juce::jlimit (0, juce::jmax (0, (int) tracks.size() - 1), contentY / rowHeight);
}

juce::Rectangle<float> ArrangementView::rowAreaInOverlay (int row) const
{
    return { 0.0f, (float) (row * rowHeight - viewport.getViewPositionY()), (float) getLaneWidth(), (float) rowHeight };
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
    auto region = engine.getTracks().getRegion (id);
    const int row = region ? rowOf (region->trackId) : -1;

    if (row < 0)
        return;

    // Scroll the region into view first, so the text field can be seen.
    const double visibleEnd = geometry.scrollBeats + getVisibleBeats();

    if (region->startBeat < geometry.scrollBeats || region->startBeat > visibleEnd - 1.0)
        scrollToBeat (region->startBeat - geometry.beatsPerBar);

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

void ArrangementView::scrollRowIntoView (int row)
{
    const int rowY = row * rowHeight;

    if (rowY < viewport.getViewPositionY() || rowY + rowHeight > viewport.getViewPositionY() + viewport.getMaximumVisibleHeight())
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
    menu.addSubMenu ("Track Color", createColourMenu (track), track != 0);
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

juce::PopupMenu ArrangementView::createColourMenu (TrackId track)
{
    // Names for TrackController::getTrackColours(), in the same order.
    static const juce::StringArray names { "Blue", "Orange", "Green", "Purple", "Yellow" };

    juce::PopupMenu menu;
    const auto info = engine.getTracks().getTrack (track);
    const auto colours = TrackController::getTrackColours();

    for (size_t i = 0; i < colours.size(); ++i)
    {
        const auto c = colours[i];

        // A small rounded swatch beside each name.
        auto swatch = std::make_unique<juce::DrawablePath>();
        juce::Path shape;
        shape.addRoundedRectangle (0.0f, 0.0f, 14.0f, 14.0f, 3.0f);
        swatch->setPath (shape);
        swatch->setFill (c);

        menu.addItem (juce::PopupMenu::Item (names[(int) i].isNotEmpty() ? names[(int) i] : "Color " + juce::String (i + 1))
                          .setImage (std::move (swatch))
                          .setTicked (info && info->colour == c)
                          .setAction ([this, track, c] { engine.getTracks().setTrackColour (track, c); }));
    }

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
    const auto regions = tc.getRegions (track);
    juce::PopupMenu menu;

    if (info->kind == TrackKind::instrument)
    {
        menu.addItem (juce::PopupMenu::Item ("Add Chord Region Here").setAction ([this, track, at]
        {
            if (auto rid = engine.getTracks().createChordRegion (track, at))
                selection.selectRegion (track, rid);
        }));
    }

    menu.addItem (juce::PopupMenu::Item ("Paste Here").setEnabled (tc.canPaste()).setAction ([this, track, at]
    {
        auto pasted = engine.getTracks().pasteRegions (track, at);

        if (auto first = pasted.empty() ? std::nullopt : engine.getTracks().getRegion (pasted.front()))
            selection.selectRegions (first->trackId, pasted);
    }));

    menu.addItem (juce::PopupMenu::Item ("Select All Regions on Track").setEnabled (! regions.empty()).setAction ([this, track]
    {
        std::vector<RegionId> ids;

        for (auto& r : engine.getTracks().getRegions (track))
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

    if (y >= viewport.getY() && contentY < (int) tracks.size() * rowHeight)
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
        if (engine.getTracks().isSupportedAudioFile (juce::File (f)))
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
        area = { (float) dropZoneMargin, (float) ((int) tracks.size() * rowHeight + dropZoneMargin - viewport.getViewPositionY()),
                 (float) (getLaneWidth() - dropZoneMargin * 2), (float) dropZoneHeight };
        text = "Add on a new audio track";
    }

    overlay->setDropTarget (area, geometry.beatToX (target.beat), text);
}

void ArrangementView::fileDragExit (const juce::StringArray&)
{
    overlay->clear();
}

void ArrangementView::filesDropped (const juce::StringArray& files, int x, int y)
{
    overlay->clear();
    auto target = findDropTarget (x, y);
    auto& tc = engine.getTracks();
    auto ids = tc.importAudioFiles (target.track, files, target.beat);

    if (ids.empty())
        return;

    refresh();

    if (auto first = tc.getRegion (ids.front()))
        selection.selectRegions (first->trackId, ids);
}

} // namespace ikaros
