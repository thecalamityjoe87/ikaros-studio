#include "ui/arrangement/ArrangementView.h"
#include "app/CommandIDs.h"
#include "ui/theme/Fonts.h"
#include "ui/theme/IkarosLookAndFeel.h"
#include "ui/theme/Tokens.h"

namespace ikaros
{

using namespace tokens;

namespace
{
    constexpr int headerWidth = size::trackHeaderWidth;
    constexpr int rowHeight = size::trackRowHeight;
    constexpr int dropZoneHeight = 84;
    constexpr int dropZoneMargin = 12;
    constexpr int loopStripHeight = 14;    // top part of the ruler where dragging sets the loop
    constexpr int scrollBarHeight = 12;
    constexpr int regionTop = 5;           // a region's gap from the top of its lane...
    constexpr int regionGap = 11;          // ...and the total gap above and below it
    constexpr int dragThreshold = 4;       // pixels the mouse must move before a click becomes a drag

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

    // Zoomed far out, drawing every bar would be a solid smear. This finds a
    // bar step (1, 2, 4, 8...) that keeps things at least minPixels apart.
    int barStride (const ArrangementView::Geometry& geo, double minPixels)
    {
        const double barWidth = geo.pixelsPerBeat * geo.beatsPerBar;
        int stride = 1;

        while (barWidth * stride < minPixels && stride < 4096)
            stride *= 2;

        return stride;
    }

    // Calls fn (barIndex, x) for every stride-th bar visible in a lane of the given width.
    template <typename Fn>
    void forEachVisibleBar (const ArrangementView::Geometry& geo, int width, int stride, Fn&& fn)
    {
        const int firstBar = juce::jmax (0, (int) std::floor (geo.scrollBeats / geo.beatsPerBar));
        const int lastBar = (int) std::ceil (geo.xToBeat ((float) width) / geo.beatsPerBar);

        for (int bar = firstBar - firstBar % stride; bar <= lastBar; bar += stride)
            fn (bar, juce::roundToInt (geo.beatToX (bar * geo.beatsPerBar)));
    }
}

//==============================================================================
// Ruler: bar numbers + loop range. Click/drag the lower part to move the
// playhead; drag along the top strip to set the loop range. The left part
// (above the track headers) holds the snap and zoom controls.
class ArrangementView::Ruler  : public juce::Component,
                               public juce::SettableTooltipClient   // adds setTooltip()
{
public:
    explicit Ruler (ArrangementView& o) : owner (o)
    {
        setTitle ("Bar ruler");
        setTooltip ("Click to move the playhead. Drag along the top edge to set the loop.");
    }

    void setLoop (bool on, double start, double end)
    {
        if (on != looping || start != loopStart || end != loopEnd)
        {
            looping = on;
            loopStart = start;
            loopEnd = end;
            repaint();
        }
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds();
        g.fillAll (colour::sidebarBg);
        g.setColour (colour::divider);
        g.fillRect (r.removeFromBottom (1));

        auto label = r.removeFromLeft (headerWidth);
        g.setColour (colour::divider);
        g.fillRect (label.removeFromRight (1));
        g.setColour (colour::textSecondary);
        g.setFont (fonts::ui (11.0f).withExtraKerningFactor (0.1f));
        g.drawText ("TRACKS", label.withTrimmedLeft (12), juce::Justification::centredLeft);

        // Everything below scrolls with the timeline; keep it out of the header part.
        juce::Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (r);
        g.setOrigin (r.getX(), 0);
        auto& geo = owner.geometry;

        // Loop range: an amber bar along the top.
        if (looping && loopEnd > loopStart)
        {
            auto x1 = geo.beatToX (loopStart);
            auto x2 = geo.beatToX (loopEnd);
            g.setColour (colour::accent.withAlpha (0.4f));
            g.fillRoundedRectangle (x1, 4.0f, x2 - x1, 7.0f, 3.5f);
        }

        // Bar numbers with a tick on the left of each. Zoomed out, only every
        // 2nd/4th/... bar gets a number so they don't overlap.
        const int labelStride = barStride (geo, 36.0);
        const int bottom = getHeight() - 1;
        g.setFont (fonts::mono (11.0f));

        forEachVisibleBar (geo, r.getWidth(), barStride (geo, 6.0), [&] (int bar, int x)
        {
            const bool labelled = bar % labelStride == 0;
            g.setColour (colour::borderStrong);
            g.fillRect (x, bottom - (labelled ? 16 : 7), 1, labelled ? 14 : 5);

            if (labelled)
            {
                g.setColour (colour::textSecondary);
                g.drawText (juce::String (bar + 1), x + 4, bottom - 16, 40, 14, juce::Justification::centredLeft);
            }
        });
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.x < headerWidth)
            return;

        draggingLoop = e.y < loopStripHeight;
        dragStartBeat = beatAt (e.x);
        mouseDrag (e);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (e.mouseDownPosition.x < headerWidth)
            return;

        auto beat = beatAt (e.x);
        auto& engine = owner.engine;

        if (draggingLoop)
        {
            // Snap the loop to whole bars.
            auto bpb = owner.geometry.beatsPerBar;
            auto a = std::floor (juce::jmin (dragStartBeat, beat) / bpb) * bpb;
            auto b = std::ceil (juce::jmax (dragStartBeat, beat) / bpb) * bpb;

            if (b <= a)
                b = a + bpb;

            engine.setLoopRangeBeats (a, b);
            engine.setLooping (true);
        }
        else
        {
            engine.setPositionBeats (owner.snapBeat (beat));
        }
    }

    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override
    {
        owner.handleWheel (e, wheel, true);   // over the ruler, the wheel always scrolls in time
    }

private:
    ArrangementView& owner;
    bool looping = false, draggingLoop = false;
    double loopStart = 0.0, loopEnd = 0.0, dragStartBeat = 0.0;

    double beatAt (int x) const   { return juce::jmax (0.0, owner.geometry.xToBeat ((float) (x - headerWidth))); }
};

//==============================================================================
// One track's timeline lane: grid + regions. Double-click creates a region.
class ArrangementView::Lane  : public juce::Component,
                              public juce::SettableTooltipClient
{
public:
    Lane (ArrangementView& o, TrackId id) : owner (o), trackId (id)
    {
        setTitle ("Track lane");

        // Clicking a lane takes keyboard focus, so focus doesn't stay on (say)
        // a Mute button that would then swallow the Space key.
        setWantsKeyboardFocus (true);
    }

    void focusGained (FocusChangeType cause) override
    {
        // Only show a focus ring when focus arrived from the keyboard (Tab).
        showFocusRing = cause != focusChangedByMouseClick;
        repaint();
    }

    void focusLost (FocusChangeType) override    { showFocusRing = false; repaint(); }

    void update (const TrackInfo& t, const std::vector<RegionInfo>& regionInfos, bool isSelected)
    {
        colour = t.colour;
        selected = isSelected;
        isAudio = t.kind == TrackKind::audio;
        setAlpha (t.silencedBySolo || t.muted ? 0.4f : 1.0f);
        setTooltip (isAudio ? "Drop an audio file here, or press R to record"
                            : "Double-click to add a 4-bar chord region");

        // Reconcile region components by ID: keep existing ones (so keyboard
        // focus and a drag in progress survive), add new ones, remove deleted ones.
        std::map<RegionId, std::unique_ptr<RegionComponent>> next;

        for (auto& info : regionInfos)
        {
            auto it = regions.find (info.id);
            std::unique_ptr<RegionComponent> comp;

            if (it != regions.end())
            {
                comp = std::move (it->second);
            }
            else
            {
                comp = std::make_unique<RegionComponent> (info, colour);
                addAndMakeVisible (*comp);

                // The arrangement handles the gestures: it knows about the
                // other selected regions and the other tracks.
                comp->onMouseDown   = [this] (RegionComponent& c, const juce::MouseEvent& e) { owner.regionMouseDown (c, e); };
                comp->onMouseDrag   = [this] (RegionComponent& c, const juce::MouseEvent& e) { owner.regionMouseDrag (c, e); };
                comp->onMouseUp     = [this] (RegionComponent& c, const juce::MouseEvent& e) { owner.regionMouseUp (c, e); };
                comp->onDoubleClick = [this] (RegionComponent& c, RegionComponent::Zone z)   { owner.regionDoubleClicked (c, z); };

                auto rid = info.id;
                comp->onRename = [this, rid] (const juce::String& name) { owner.engine.getTracks().renameRegion (rid, name); };
                comp->onOpenEditor = [this, rid]
                {
                    owner.selection.selectRegion (trackId, rid);

                    if (owner.onOpenRegionInEditor)
                        owner.onOpenRegionInEditor (rid);
                };
            }

            comp->update (info, colour, owner.selection.isSelected (info.id));
            next[info.id] = std::move (comp);
        }

        regions = std::move (next);   // components not moved across are destroyed here
        resized();
        repaint();
    }

    RegionComponent* findRegion (RegionId id) const
    {
        auto it = regions.find (id);
        return it != regions.end() ? it->second.get() : nullptr;
    }

    /** The take being recorded on this lane, from startBeat to the playhead
        (nothing when endBeat <= startBeat). */
    void setRecordingRange (double startBeat, double endBeat)
    {
        if (startBeat != recordStart || endBeat != recordEnd)
        {
            recordStart = startBeat;
            recordEnd = endBeat;
            repaint();
        }
    }

    void paintOverChildren (juce::Graphics& g) override
    {
        if (recordEnd <= recordStart)
            return;

        // A red region that grows with the playhead while recording.
        auto& geo = owner.geometry;
        auto x1 = geo.beatToX (recordStart), x2 = geo.beatToX (recordEnd);
        auto r = juce::Rectangle<float> (x1, (float) regionTop, juce::jmax (4.0f, x2 - x1), (float) (getHeight() - regionGap));

        g.setColour (colour::record.withAlpha (0.25f));
        g.fillRoundedRectangle (r, 8.0f);
        g.setColour (colour::record);
        g.drawRoundedRectangle (r, 8.0f, 1.5f);

        g.setFont (fonts::uiBold (11.0f));
        g.drawText ("Recording", r.reduced (8.0f, 4.0f).removeFromTop (14.0f), juce::Justification::centredLeft, true);
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds();

        if (selected)
            g.fillAll (colour::accent.withAlpha (0.05f));

        // Beat lines (faint), when zoomed in enough for them to be useful...
        auto& geo = owner.geometry;

        if (geo.pixelsPerBeat >= 6.0)
        {
            g.setColour (juce::Colours::white.withAlpha (0.025f));

            for (double beat = std::ceil (geo.scrollBeats); geo.beatToX (beat) <= (float) getWidth(); beat += 1.0)
                g.fillRect (juce::roundToInt (geo.beatToX (beat)), 0, 1, r.getHeight());
        }

        // ...and bar lines (stronger).
        g.setColour (juce::Colours::white.withAlpha (0.05f));
        forEachVisibleBar (geo, getWidth(), barStride (geo, 8.0), [&] (int, int x) { g.fillRect (x, 0, 1, r.getHeight()); });

        g.setColour (colour::divider);
        g.fillRect (r.removeFromBottom (1));

        if (showFocusRing && hasKeyboardFocus (false))
            IkarosLookAndFeel::drawFocusRing (g, getLocalBounds().toFloat().reduced (1.0f), 4.0f);
    }

    void resized() override
    {
        auto& geo = owner.geometry;

        for (auto& [id, comp] : regions)   // structured binding: unpacks each map entry into key + value
        {
            // The display range includes an edge that's being dragged.
            auto range = comp->getDisplayRange();
            auto x = juce::roundToInt (geo.beatToX (range.getStart()));
            auto w = juce::jmax (8, juce::roundToInt (geo.beatsToWidth (range.getLength())));
            comp->setBounds (x, regionTop, w, getHeight() - regionGap);
        }
    }

    void mouseDown (const juce::MouseEvent&) override
    {
        owner.selection.selectRegion (trackId, 0);   // select the track, deselect regions
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        auto beat = owner.geometry.xToBeat ((float) e.x);

        if (auto rid = owner.engine.getTracks().createChordRegion (trackId, beat))
            owner.selection.selectRegion (trackId, rid);
    }

private:
    ArrangementView& owner;
    const TrackId trackId;
    juce::Colour colour;
    bool selected = false, isAudio = false, showFocusRing = false;
    double recordStart = 0.0, recordEnd = 0.0;
    std::map<RegionId, std::unique_ptr<RegionComponent>> regions;
};

//==============================================================================
// The dashed "Drop a sound, loop or audio file here" zone. Clicking it adds a
// track; dropping audio files on it adds them on a new audio track (the
// ArrangementView handles the drop).
class ArrangementView::DropZone  : public juce::Button
{
public:
    DropZone() : juce::Button ("Add a track")
    {
        setTitle ("Add a track");
        setMouseClickGrabsKeyboardFocus (false);
        setTooltip ("Click to add a new track, or drop audio files here");
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    void paintButton (juce::Graphics& g, bool over, bool) override
    {
        auto r = getLocalBounds().toFloat().reduced (1.0f);

        juce::Path outline, dashed;
        outline.addRoundedRectangle (r, 12.0f);
        const float dashes[] = { 5.0f, 4.0f };
        juce::PathStrokeType (1.0f).createDashedStroke (dashed, outline, dashes, 2);

        if (over)
        {
            g.setColour (colour::raised.withAlpha (0.4f));
            g.fillPath (outline);
        }

        g.setColour (over ? colour::textSecondary : colour::borderStrong);
        g.fillPath (dashed);

        auto text = juce::String ("Drop a sound, loop or audio file here");
        auto font = fonts::ui (14.0f);
        auto textW = juce::GlyphArrangement::getStringWidth (font, text);
        auto group = r.withSizeKeepingCentre (textW + 26.0f, 20.0f);

        icons::draw (g, Icon::plus, group.removeFromLeft (18.0f), colour::textSecondary, 18.0f, 2.0f);
        group.removeFromLeft (8.0f);
        g.setColour (colour::textSecondary);
        g.setFont (font);
        g.drawText (text, group, juce::Justification::centredLeft);

        if (hasKeyboardFocus (false))
            IkarosLookAndFeel::drawFocusRing (g, r, 12.0f);
    }
};

//==============================================================================
// The playhead: amber line with a rounded head, drawn over the ruler and lanes.
class ArrangementView::Playhead  : public juce::Component
{
public:
    Playhead()
    {
        setInterceptsMouseClicks (false, false);   // clicks pass through to the ruler/lanes
        setAccessible (false);
    }

    void setX (float newX)
    {
        if (std::abs (newX - x) < 0.25f)
            return;

        // Only repaint the thin strips that changed, not the whole timeline.
        repaint (stripAt (x));
        x = newX;
        repaint (stripAt (x));
    }

    void paint (juce::Graphics& g) override
    {
        if (x < -8.0f || x > (float) getWidth() + 8.0f)
            return;

        g.setColour (colour::accent);
        g.fillRect (x - 1.0f, 0.0f, 2.0f, (float) getHeight());
        g.fillRoundedRectangle (x - 7.0f, 0.0f, 14.0f, 12.0f, 4.0f);
    }

private:
    float x = -100.0f;

    juce::Rectangle<int> stripAt (float px) const
    {
        return { juce::roundToInt (px) - 9, 0, 18, getHeight() };
    }
};

//==============================================================================
// Drawn over the lanes: outlines showing where dragged regions will land, and
// where dragged-in audio files will go.
class ArrangementView::Overlay  : public juce::Component
{
public:
    struct Ghost
    {
        juce::Rectangle<float> area;
        juce::Colour colour;
        juce::String name;
    };

    Overlay()
    {
        setInterceptsMouseClicks (false, false);
        setAccessible (false);
    }

    void setGhosts (std::vector<Ghost> newGhosts)
    {
        ghosts = std::move (newGhosts);
        repaint();
    }

    void setDropTarget (juce::Rectangle<float> area, float markerX, const juce::String& text)
    {
        dropArea = area;
        dropMarkerX = markerX;
        dropText = text;
        repaint();
    }

    void clear()
    {
        ghosts.clear();
        dropArea = {};
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        for (auto& ghost : ghosts)
        {
            g.setColour (ghost.colour.withAlpha (0.3f));
            g.fillRoundedRectangle (ghost.area, 8.0f);
            g.setColour (ghost.colour);
            g.drawRoundedRectangle (ghost.area.reduced (0.75f), 8.0f, 1.5f);
            g.setColour (colour::text);
            g.setFont (fonts::uiBold (11.0f));
            g.drawText (ghost.name, ghost.area.reduced (8.0f, 3.0f).removeFromTop (14.0f),
                        juce::Justification::centredLeft, true);
        }

        if (! dropArea.isEmpty())
        {
            g.setColour (colour::accent.withAlpha (0.12f));
            g.fillRoundedRectangle (dropArea, 10.0f);
            g.setColour (colour::accent);
            g.drawRoundedRectangle (dropArea.reduced (1.0f), 10.0f, 1.5f);
            g.fillRect (dropMarkerX - 1.0f, dropArea.getY(), 2.0f, dropArea.getHeight());

            g.setFont (fonts::uiBold (13.0f));
            g.drawText (dropText, dropArea.withLeft (dropMarkerX + 8.0f).reduced (0.0f, 6.0f),
                        juce::Justification::topLeft, true);
        }
    }

private:
    std::vector<Ghost> ghosts;
    juce::Rectangle<float> dropArea;
    float dropMarkerX = 0.0f;
    juce::String dropText;
};

//==============================================================================
// Everything that scrolls vertically: headers on the left, lanes on the right.
class ArrangementView::Content  : public juce::Component
{
public:
    explicit Content (ArrangementView& o) : owner (o)
    {
        dropZone.onClick = [this] { if (owner.onAddTrackRequested) owner.onAddTrackRequested (&dropZone); };
        addAndMakeVisible (dropZone);
    }

    void sync (const std::vector<TrackInfo>& tracks,
               const std::map<TrackId, std::vector<RegionInfo>>& regions)
    {
        std::map<TrackId, std::unique_ptr<TrackHeader>> nextHeaders;
        std::map<TrackId, std::unique_ptr<Lane>> nextLanes;
        order.clear();

        auto selectedTrack = owner.selection.getTrack();

        for (auto& t : tracks)
        {
            auto header = std::move (headers[t.id]);
            auto lane = std::move (lanes[t.id]);

            if (header == nullptr)
            {
                header = std::make_unique<TrackHeader> (owner.engine, owner.selection, t);
                addAndMakeVisible (*header);
            }

            if (lane == nullptr)
            {
                lane = std::make_unique<Lane> (owner, t.id);
                addAndMakeVisible (*lane);
            }

            header->update (t, t.id == selectedTrack);
            auto it = regions.find (t.id);
            lane->update (t, it != regions.end() ? it->second : std::vector<RegionInfo> {}, t.id == selectedTrack);

            order.push_back (t.id);
            nextHeaders[t.id] = std::move (header);
            nextLanes[t.id] = std::move (lane);
        }

        headers = std::move (nextHeaders);
        lanes = std::move (nextLanes);
    }

    RegionComponent* findRegion (RegionId id) const
    {
        for (auto& [trackId, lane] : lanes)
            if (auto* region = lane->findRegion (id))
                return region;

        return nullptr;
    }

    /** Re-positions regions and redraws the grid after a zoom or scroll. */
    void timelineChanged()
    {
        for (auto& [id, lane] : lanes)
        {
            lane->resized();
            lane->repaint();
        }
    }

    void setRecording (TrackId track, double startBeat, double endBeat)
    {
        for (auto& [id, lane] : lanes)
        {
            if (id == track)
                lane->setRecordingRange (startBeat, endBeat);
            else
                lane->setRecordingRange (0.0, 0.0);
        }
    }

    int getIdealHeight() const
    {
        return (int) order.size() * rowHeight + dropZoneHeight + dropZoneMargin * 2;
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (colour::windowBg);
        g.setColour (colour::sidebarBg);
        g.fillRect (0, 0, headerWidth - 1, getHeight());
        g.setColour (colour::divider);
        g.fillRect (headerWidth - 1, 0, 1, getHeight());
    }

    void resized() override
    {
        int y = 0;

        for (auto id : order)
        {
            headers[id]->setBounds (0, y, headerWidth - 1, rowHeight);
            lanes[id]->setBounds (headerWidth, y, getWidth() - headerWidth, rowHeight);
            y += rowHeight;
        }

        dropZone.setBounds (headerWidth + dropZoneMargin, y + dropZoneMargin,
                            getWidth() - headerWidth - dropZoneMargin * 2, dropZoneHeight);
    }

    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override
    {
        // Wheel events bubble up here from the lanes and regions. Anything the
        // arrangement doesn't use goes on up to the Viewport (vertical scroll).
        if (! owner.handleWheel (e, wheel, false))
            juce::Component::mouseWheelMove (e, wheel);
    }

private:
    ArrangementView& owner;
    std::map<TrackId, std::unique_ptr<TrackHeader>> headers;
    std::map<TrackId, std::unique_ptr<Lane>> lanes;
    std::vector<TrackId> order;
    DropZone dropZone;
};

//==============================================================================
ArrangementView::ArrangementView (AudioEngine& e, Selection& s, juce::ApplicationCommandManager& cm)
    : engine (e), selection (s), commands (cm)
{
    ruler = std::make_unique<Ruler> (*this);
    content = std::make_unique<Content> (*this);
    overlay = std::make_unique<Overlay>();
    playhead = std::make_unique<Playhead>();

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

    if (state.playing && wasVisible && ! isVisible && drag.mode == Drag::Mode::none
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

void ArrangementView::regionMouseDown (RegionComponent& comp, const juce::MouseEvent& e)
{
    const auto info = comp.getInfo();   // a copy: the component may be updated during the drag
    drag = {};

    if (e.mods.isPopupMenu())
    {
        // Right-click: act on the clicked region, keeping a selection it's part of.
        if (! selection.isSelected (info.id))
            selection.selectRegion (info.trackId, info.id);

        showRegionMenu();
        return;
    }

    if (e.mods.isShiftDown() || e.mods.isCommandDown())
    {
        selection.toggleRegion (info.trackId, info.id);

        if (! selection.isSelected (info.id))
            return;   // it was just deselected: nothing to drag
    }
    else if (! selection.isSelected (info.id))
    {
        selection.selectRegion (info.trackId, info.id);
    }
    else
    {
        // Already part of a selection: keep it so the whole group can be
        // dragged; a plain click (no drag) selects just this one on mouse-up.
        drag.selectOnlyAnchorOnClick = selection.getRegions().size() > 1;
    }

    const auto zone = comp.getZoneAt (e.getPosition());
    drag.anchor = info.id;
    drag.start = e.getEventRelativeTo (this).getPosition();
    drag.mode = zone == RegionComponent::Zone::leftEdge  ? Drag::Mode::trimStart
              : zone == RegionComponent::Zone::rightEdge ? Drag::Mode::trimEnd
                                                         : Drag::Mode::pending;
    drag.trim = { info.startBeat, info.getEndBeat() };

    for (auto id : selection.getRegions())
    {
        if (auto* c = findRegion (id))
        {
            drag.regions.push_back (c->getInfo());
            drag.rows.push_back (rowOf (c->getInfo().trackId));
        }
    }
}

void ArrangementView::regionMouseDrag (RegionComponent&, const juce::MouseEvent& e)
{
    const auto pos = e.getEventRelativeTo (this).getPosition();

    if (drag.mode == Drag::Mode::pending && pos.getDistanceFrom (drag.start) >= dragThreshold)
    {
        drag.mode = Drag::Mode::move;

        // Dim the originals; outlines show where they'll land.
        for (auto& r : drag.regions)
            if (auto* c = findRegion (r.id))
                c->setAlpha (0.35f);
    }

    if (drag.mode == Drag::Mode::move)
        updateMoveDrag (pos);
    else if (drag.mode == Drag::Mode::trimStart || drag.mode == Drag::Mode::trimEnd)
        updateTrimDrag (pos.x - drag.start.x);
}

void ArrangementView::updateMoveDrag (juce::Point<int> pos)
{
    auto anchor = std::find_if (drag.regions.begin(), drag.regions.end(),
                                [this] (const RegionInfo& r) { return r.id == drag.anchor; });

    if (anchor == drag.regions.end())
        return;

    const int anchorIndex = (int) std::distance (drag.regions.begin(), anchor);

    // Time: snap the clicked region's start to the grid; the others keep their spacing.
    double delta = (pos.x - drag.start.x) / geometry.pixelsPerBeat;
    delta = snapBeat (anchor->startBeat + delta) - anchor->startBeat;

    double earliest = anchor->startBeat;

    for (auto& r : drag.regions)
        earliest = juce::jmin (earliest, r.startBeat);

    delta = juce::jmax (delta, -earliest);   // nothing goes before the start of the song

    // Tracks: follow the mouse up and down, as long as every region lands on
    // a track of its own kind (MIDI on instrument tracks, audio on audio
    // tracks). Otherwise they stay on their own tracks.
    int rowDelta = rowAt (pos.y) - drag.rows[(size_t) anchorIndex];

    for (size_t i = 0; i < drag.regions.size(); ++i)
    {
        const int target = drag.rows[i] + rowDelta;

        if (drag.rows[i] < 0 || ! juce::isPositiveAndBelow (target, (int) tracks.size())
             || (tracks[(size_t) target].kind == TrackKind::audio) == drag.regions[i].isMidi)
        {
            rowDelta = 0;
            break;
        }
    }

    drag.beatDelta = delta;
    drag.rowDelta = rowDelta;

    std::vector<Overlay::Ghost> ghosts;

    for (size_t i = 0; i < drag.regions.size(); ++i)
    {
        auto& r = drag.regions[i];
        const int row = drag.rows[i] + rowDelta;

        if (! juce::isPositiveAndBelow (row, (int) tracks.size()))
            continue;

        auto area = rowAreaInOverlay (row);
        ghosts.push_back ({ { geometry.beatToX (r.startBeat + delta), area.getY() + (float) regionTop,
                              juce::jmax (8.0f, geometry.beatsToWidth (r.lengthBeats)), area.getHeight() - (float) regionGap },
                            tracks[(size_t) row].colour, r.name });
    }

    overlay->setGhosts (std::move (ghosts));
}

void ArrangementView::updateTrimDrag (int dx)
{
    auto* comp = findRegion (drag.anchor);

    if (comp == nullptr)
        return;

    auto& info = comp->getInfo();
    const double delta = dx / geometry.pixelsPerBeat;
    const double minLength = TrackController::minRegionBeats;

    if (drag.mode == Drag::Mode::trimStart)
    {
        auto start = snapBeat (info.startBeat + delta);
        start = juce::jlimit (juce::jmax (0.0, info.minStartBeat), info.getEndBeat() - minLength, start);
        drag.trim = { start, info.getEndBeat() };
    }
    else
    {
        auto end = snapBeat (info.getEndBeat() + delta);
        end = juce::jlimit (info.startBeat + minLength, juce::jmax (info.startBeat + minLength, info.maxEndBeat), end);
        drag.trim = { info.startBeat, end };
    }

    comp->setTrimPreview (drag.trim);

    if (auto* lane = comp->getParentComponent())
        lane->resized();
}

void ArrangementView::regionMouseUp (RegionComponent&, const juce::MouseEvent&)
{
    auto finished = std::exchange (drag, {});   // take the drag state and reset it
    auto& tc = engine.getTracks();

    if (finished.mode == Drag::Mode::move)
    {
        overlay->clear();

        for (auto& r : finished.regions)
            if (auto* c = findRegion (r.id))
                c->setAlpha (1.0f);

        if (finished.beatDelta != 0.0 || finished.rowDelta != 0)
        {
            std::vector<RegionPlacement> placements;

            for (size_t i = 0; i < finished.regions.size(); ++i)
            {
                const int row = finished.rows[i] + finished.rowDelta;

                if (juce::isPositiveAndBelow (row, (int) tracks.size()))
                    placements.push_back ({ finished.regions[i].id, tracks[(size_t) row].id,
                                            finished.regions[i].startBeat + finished.beatDelta });
            }

            tc.moveRegions (placements);

            // The selected track follows the clicked region to its new track.
            if (auto moved = tc.getRegion (finished.anchor))
                selection.selectRegions (moved->trackId, selection.getRegions());

            refresh();   // now, so the regions don't flash back to where they were
        }
    }
    else if (finished.mode == Drag::Mode::trimStart || finished.mode == Drag::Mode::trimEnd)
    {
        if (auto* comp = findRegion (finished.anchor))
        {
            const auto& info = comp->getInfo();
            const bool changed = finished.trim.getStart() != info.startBeat || finished.trim.getEnd() != info.getEndBeat();
            comp->setTrimPreview (std::nullopt);

            if (changed)
                tc.trimRegion (finished.anchor, finished.trim.getStart(), finished.trim.getEnd());

            refresh();
        }
    }
    else if (finished.mode == Drag::Mode::pending && finished.selectOnlyAnchorOnClick)
    {
        if (auto r = tc.getRegion (finished.anchor))
            selection.selectRegion (r->trackId, r->id);
    }
}

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

    const int rowY = row * rowHeight;

    if (rowY < viewport.getViewPositionY() || rowY + rowHeight > viewport.getViewPositionY() + viewport.getMaximumVisibleHeight())
        viewport.setViewPosition (0, rowY);

    if (auto* comp = findRegion (id))
        comp->startRenaming();
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
