#include "ui/arrangement/ArrangementLane.h"
#include "ui/arrangement/ArrangementLayout.h"
#include "ui/theme/Fonts.h"
#include "ui/theme/IkarosLookAndFeel.h"

namespace ikaros
{

using namespace tokens;
using namespace arrangement;

ArrangementView::Lane::Lane (ArrangementView& o, TrackId id) : owner (o), trackId (id)
{
    setTitle ("Track lane");

    // Clicking a lane takes keyboard focus, so focus doesn't stay on (say)
    // a Mute button that would then swallow the Space key.
    setWantsKeyboardFocus (true);
}

void ArrangementView::Lane::focusGained (FocusChangeType cause)
{
    // Only show a focus ring when focus arrived from the keyboard (Tab).
    showFocusRing = cause != focusChangedByMouseClick;
    repaint();
}

void ArrangementView::Lane::setTrackInfo (const TrackInfo& t, bool isSelected)
{
    // The lane's own drawing (the selected tint) only changes with the
    // selection. (setAlpha repaints by itself, and only when it changes.)
    if (isSelected != selected)
        repaint();

    colour = t.colour;
    selected = isSelected;
    isAudio = t.kind == TrackKind::audio;
    setAlpha (t.silencedBySolo || t.muted ? 0.4f : 1.0f);
    setTooltip (isAudio ? "Drop an audio file here, or press R to record"
                        : "Double-click to add a 4-bar chord region");
}

void ArrangementView::Lane::updateTrack (const TrackInfo& t, bool isSelected)
{
    setTrackInfo (t, isSelected);

    // The regions keep what they show; only the colour and selection may differ.
    for (auto& [id, comp] : regions)
        comp->update (comp->getInfo(), colour, owner.selection.isSelected (id));
}

void ArrangementView::Lane::updateRegion (const RegionInfo& info)
{
    if (auto* comp = findRegion (info.id))
    {
        comp->update (info, colour, owner.selection.isSelected (info.id));
        resized();   // it may have moved or changed length
    }
}

double ArrangementView::Lane::getEndBeat() const
{
    double end = 0.0;

    for (auto& [id, comp] : regions)
        end = juce::jmax (end, comp->getInfo().getEndBeat());

    return end;
}

void ArrangementView::Lane::update (const TrackInfo& t, const std::vector<RegionInfo>& regionInfos, bool isSelected)
{
    setTrackInfo (t, isSelected);

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
            comp->onRename = [this, rid] (const juce::String& name) { owner.engine.getTracks().regions().renameRegion (rid, name); };
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

RegionComponent* ArrangementView::Lane::findRegion (RegionId id) const
{
    auto it = regions.find (id);
    return it != regions.end() ? it->second.get() : nullptr;
}

void ArrangementView::Lane::setRecordingRange (double startBeat, double endBeat)
{
    if (startBeat != recordStart || endBeat != recordEnd)
    {
        recordStart = startBeat;
        recordEnd = endBeat;
        repaint();
    }
}

void ArrangementView::Lane::paintOverChildren (juce::Graphics& g)
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

void ArrangementView::Lane::paint (juce::Graphics& g)
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

void ArrangementView::Lane::resized()
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

void ArrangementView::Lane::mouseDown (const juce::MouseEvent& e)
{
    owner.selection.selectRegion (trackId, 0);   // select the track, deselect regions

    if (e.mods.isPopupMenu())
        owner.showLaneMenu (trackId, owner.geometry.xToBeat ((float) e.x));
}

void ArrangementView::Lane::mouseDoubleClick (const juce::MouseEvent& e)
{
    auto beat = owner.geometry.xToBeat ((float) e.x);

    if (auto rid = owner.engine.getTracks().regions().createChordRegion (trackId, beat))
        owner.selection.selectRegion (trackId, rid);
}

} // namespace ikaros
