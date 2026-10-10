#include "ui/arrangement/ArrangementView.h"
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
}

//==============================================================================
// Ruler: bar numbers + loop range. Click/drag the lower part to move the
// playhead; drag along the top strip to set the loop range.
class ArrangementView::Ruler  : public juce::Component,
                               public juce::SettableTooltipClient   // adds setTooltip()
{
public:
    Ruler (AudioEngine& e, const Geometry& g) : engine (e), geometry (g)
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

        // Loop range: an amber bar along the top.
        if (looping && loopEnd > loopStart)
        {
            auto x1 = (float) r.getX() + geometry.beatToX (loopStart);
            auto x2 = (float) r.getX() + geometry.beatToX (loopEnd);
            g.setColour (colour::accent.withAlpha (0.4f));
            g.fillRoundedRectangle (x1, 4.0f, x2 - x1, 7.0f, 3.5f);
        }

        // Bar numbers with a tick on the left of each.
        g.setFont (fonts::mono (11.0f));

        for (int bar = 0; bar < geometry.numBars; ++bar)
        {
            auto x = r.getX() + juce::roundToInt (geometry.beatToX (bar * geometry.beatsPerBar));
            g.setColour (colour::borderStrong);
            g.fillRect (x, r.getBottom() - 17, 1, 14);
            g.setColour (colour::textSecondary);
            g.drawText (juce::String (bar + 1), x + 4, r.getBottom() - 17, 40, 14, juce::Justification::centredLeft);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        draggingLoop = e.y < loopStripHeight;
        dragStartBeat = beatAt (e.x);
        mouseDrag (e);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        auto beat = beatAt (e.x);

        if (draggingLoop)
        {
            // Snap the loop to whole bars.
            auto bpb = geometry.beatsPerBar;
            auto a = std::floor (juce::jmin (dragStartBeat, beat) / bpb) * bpb;
            auto b = std::ceil (juce::jmax (dragStartBeat, beat) / bpb) * bpb;

            if (b <= a)
                b = a + bpb;

            engine.setLoopRangeBeats (a, b);
            engine.setLooping (true);
        }
        else
        {
            engine.setPositionBeats (beat);
        }
    }

private:
    AudioEngine& engine;
    const Geometry& geometry;
    bool looping = false, draggingLoop = false;
    double loopStart = 0.0, loopEnd = 0.0, dragStartBeat = 0.0;

    double beatAt (int x) const   { return juce::jmax (0.0, geometry.xToBeat ((float) (x - headerWidth))); }
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
        setTooltip ("Double-click to add a 4-bar chord region");

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

    void update (const TrackInfo& t, const std::vector<RegionInfo>& regionInfos, bool isSelected, RegionId selectedRegion)
    {
        colour = t.colour;
        selected = isSelected;
        setAlpha (t.silencedBySolo || t.muted ? 0.4f : 1.0f);

        // Reconcile region components by ID: keep existing ones (so keyboard
        // focus survives), add new ones, remove deleted ones.
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

                auto rid = info.id;
                comp->onSelect = [this, rid] { owner.selection.selectRegion (trackId, rid); };
                comp->onOpenEditor = [this, rid]
                {
                    owner.selection.selectRegion (trackId, rid);

                    if (owner.onOpenRegionInEditor)
                        owner.onOpenRegionInEditor (rid);
                };
                comp->onDelete = [this, rid] { owner.engine.getTracks().deleteRegion (rid); };
            }

            comp->update (info, colour, info.id == selectedRegion);
            next[info.id] = std::move (comp);
        }

        regions = std::move (next);   // components not moved across are destroyed here
        resized();
        repaint();
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
        auto r = juce::Rectangle<float> (x1, 5.0f, juce::jmax (4.0f, x2 - x1), (float) getHeight() - 11.0f);

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

        // Beat lines (faint) and bar lines (stronger).
        auto& geo = owner.geometry;
        const int totalBeats = juce::roundToInt (geo.numBars * geo.beatsPerBar);

        for (int beat = 0; beat <= totalBeats; ++beat)
        {
            const bool isBar = std::fmod ((double) beat, geo.beatsPerBar) == 0.0;
            g.setColour (juce::Colours::white.withAlpha (isBar ? 0.05f : 0.025f));
            g.fillRect (juce::roundToInt (geo.beatToX (beat)), 0, 1, r.getHeight());
        }

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
            auto& info = comp->getInfo();
            auto x = juce::roundToInt (geo.beatToX (info.startBeat));
            auto w = juce::jmax (8, juce::roundToInt (geo.beatToX (info.lengthBeats)));
            comp->setBounds (x, 5, w, getHeight() - 11);
        }
    }

    void mouseDown (const juce::MouseEvent&) override
    {
        owner.selection.selectRegion (trackId, 0);
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
    bool selected = false, showFocusRing = false;
    double recordStart = 0.0, recordEnd = 0.0;
    std::map<RegionId, std::unique_ptr<RegionComponent>> regions;
};

//==============================================================================
// The dashed "Drop a sound, loop or audio file here" zone. Clicking it adds a track.
class ArrangementView::DropZone  : public juce::Button
{
public:
    DropZone() : juce::Button ("Add a track")
    {
        setTitle ("Add a track");
        setMouseClickGrabsKeyboardFocus (false);
        setTooltip ("Click to add a new instrument track");
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    // TODO(milestone-3): implement juce::FileDragAndDropTarget here to accept
    // audio files and loops from the OS and from the library.

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
        auto selectedRegion = owner.selection.getRegion();

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
            lane->update (t, it != regions.end() ? it->second : std::vector<RegionInfo> {},
                          t.id == selectedTrack, selectedRegion);

            order.push_back (t.id);
            nextHeaders[t.id] = std::move (header);
            nextLanes[t.id] = std::move (lane);
        }

        headers = std::move (nextHeaders);
        lanes = std::move (nextLanes);
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

private:
    ArrangementView& owner;
    std::map<TrackId, std::unique_ptr<TrackHeader>> headers;
    std::map<TrackId, std::unique_ptr<Lane>> lanes;
    std::vector<TrackId> order;
    DropZone dropZone;
};

//==============================================================================
ArrangementView::ArrangementView (AudioEngine& e, Selection& s)
    : engine (e), selection (s)
{
    ruler = std::make_unique<Ruler> (engine, geometry);
    content = std::make_unique<Content> (*this);
    playhead = std::make_unique<Playhead>();

    addAndMakeVisible (*ruler);

    viewport.setViewedComponent (content.get(), false);
    viewport.setScrollBarsShown (true, false);
    addAndMakeVisible (viewport);

    addAndMakeVisible (*playhead);   // added last so it's drawn on top

    refresh();
}

// Defined here (not in the header) because the unique_ptr members point to
// classes that are only complete in this file.
ArrangementView::~ArrangementView() = default;

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
            contentEndBeat = juce::jmax (contentEndBeat, r.startBeat + r.lengthBeats);
    }

    updateGeometry();
    content->sync (tracks, regions);
    layoutContent();
    ruler->repaint();
}

void ArrangementView::updateGeometry()
{
    geometry.beatsPerBar = engine.beatsPerBar();

    // Show at least 16 bars, and always 4 empty bars after the last region,
    // rounded up to a multiple of 4.
    auto barsNeeded = (int) std::ceil (contentEndBeat / geometry.beatsPerBar) + 4;
    geometry.numBars = juce::jmax (16, ((barsNeeded + 3) / 4) * 4);

    auto laneWidth = juce::jmax (100, viewport.getMaximumVisibleWidth() - headerWidth);
    geometry.pixelsPerBeat = laneWidth / (geometry.numBars * geometry.beatsPerBar);
}

void ArrangementView::layoutContent()
{
    content->setSize (viewport.getMaximumVisibleWidth(),
                      juce::jmax (viewport.getMaximumVisibleHeight(), content->getIdealHeight()));

    // setSize only triggers resized() when the size actually changes, but rows
    // may have been added or removed, so always re-run the layout.
    content->resized();
}

void ArrangementView::updateTransport()
{
    auto state = engine.getTransportState();
    ruler->setLoop (state.looping, state.loopStartBeats, state.loopEndBeats);
    playhead->setX (geometry.beatToX (state.positionBeats));

    if (state.recording)
        content->setRecording (engine.getTracks().getArmedTrack(), state.recordStartBeats, state.positionBeats);
    else
        content->setRecording (0, 0.0, 0.0);
}

void ArrangementView::paint (juce::Graphics& g)
{
    g.fillAll (colour::windowBg);
}

void ArrangementView::resized()
{
    auto r = getLocalBounds();
    ruler->setBounds (r.removeFromTop (size::rulerHeight));
    viewport.setBounds (r);

    updateGeometry();
    layoutContent();

    // Playhead covers the ruler and lanes, but not the track headers column.
    playhead->setBounds (getLocalBounds().withTrimmedLeft (headerWidth)
                                         .withWidth (viewport.getMaximumVisibleWidth() - headerWidth));
    ruler->repaint();
}

} // namespace ikaros
