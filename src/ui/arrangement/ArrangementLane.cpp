#include "ui/arrangement/ArrangementLane.h"
#include "ui/widgets/PaintProbe.h"
#include "ui/arrangement/ArrangementLayout.h"
#include "ui/arrangement/ArrangementRegionDrag.h"
#include "engine/LiveWaveform.h"
#include "ui/theme/Fonts.h"
#include "ui/theme/IkarosLookAndFeel.h"

namespace ikaros
{

using namespace tokens;
using namespace arrangement;

namespace
{
    /** The outline for one of this lane's regions: white on the selected
        track, fainter on the regions left out while some are picked. */
    RegionComponent::Outline outlineFor (const Selection& selection, bool trackSelected, RegionId region)
    {
        if (! trackSelected)
            return RegionComponent::Outline::normal;

        return selection.hasRegions() && ! selection.isSelected (region) ? RegionComponent::Outline::selectedTrackFaint
                                                                         : RegionComponent::Outline::selectedTrack;
    }
}

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
    // Quieter when muted; fainter still under automation, so the curve stands out.
    const float newFade = (t.silencedBySolo || t.muted ? 0.4f : 1.0f) * (t.automationShown ? 0.45f : 1.0f);

    // The lane's own drawing only changes with the selection and the fade.
    if (isSelected != selected || newFade != fade)
        repaint();

    family = colour::familyOf (t);
    selected = isSelected;
    fade = newFade;
    isAudio = t.kind == TrackKind::audio;
    setTooltip (isAudio ? "Drop an audio file here, or press R to record"
                        : "Double-click to add a 4-bar chord region");
}

void ArrangementView::Lane::updateTrack (const TrackInfo& t, bool isSelected)
{
    setTrackInfo (t, isSelected);

    // The regions keep what they show; only the colour and selection may differ.
    for (auto& [id, comp] : regions)
        comp->update (comp->getInfo(), family, owner.selection.isSelected (id), outlineFor (owner.selection, selected, id));
}

void ArrangementView::Lane::updateRegion (const RegionInfo& info)
{
    if (auto* comp = findRegion (info.id))
    {
        comp->update (info, family, owner.selection.isSelected (info.id), outlineFor (owner.selection, selected, info.id));
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
            comp = std::make_unique<RegionComponent> (info, family);
            addAndMakeVisible (*comp);

            // The arrangement handles the gestures: it knows about the
            // other selected regions and the other tracks.
            comp->onMouseDown   = [this] (RegionComponent& c, const juce::MouseEvent& e) { owner.regionMouseDown (c, e); };
            comp->onMouseDrag   = [this] (RegionComponent& c, const juce::MouseEvent& e) { owner.regionMouseDrag (c, e); };
            comp->onMouseUp     = [this] (RegionComponent& c, const juce::MouseEvent& e) { owner.regionMouseUp (c, e); };
            comp->onDoubleClick = [this] (RegionComponent& c, RegionComponent::Zone z)   { owner.regionDoubleClicked (c, z); };
            comp->onEscape      = [this] { return owner.regionDrag->cancel(); };
            comp->onTakeBadge   = [this] (RegionComponent& c)                        { owner.showTakeMenu (c); };

            auto rid = info.id;
            comp->onRename = [this, rid] (const juce::String& name) { owner.engine.getTracks().regions().renameRegion (rid, name); };
            comp->onOpenEditor = [this, rid]
            {
                owner.selection.selectRegion (trackId, rid);

                if (owner.onOpenRegionInEditor)
                    owner.onOpenRegionInEditor (rid);
            };
        }

        comp->update (info, family, owner.selection.isSelected (info.id), outlineFor (owner.selection, selected, info.id));
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
    IKAROS_PERF_PAINT ("ArrangementView::Lane::paintOverChildren");
    if (recordEnd > recordStart)
    {
        // A red region that grows with the playhead while recording.
        auto& geo = owner.geometry;
        auto x1 = geo.beatToX (recordStart), x2 = geo.beatToX (recordEnd);
        auto r = juce::Rectangle<float> (x1, (float) regionTop, juce::jmax (4.0f, x2 - x1), (float) (getHeight() - regionGap));

        g.setColour (theme().arrangement.record.withAlpha (0.25f));
        g.fillRoundedRectangle (r, size::radius);
        g.setColour (theme().arrangement.record);
        g.drawRoundedRectangle (r, size::radius, 1.5f);

        g.setFont (fonts::uiBold (textSize::xs));
        g.drawText ("Recording", r.reduced (8.0f, 4.0f).removeFromTop (14.0f), juce::Justification::centredLeft, true);

        paintLiveWaveform (g, r.withTrimmedTop (18.0f).reduced (0.0f, 4.0f));
    }

    // Faded: a veil of the background the lane sits on. That looks just
    // like setAlpha would (the lanes sit on a plain laneBg), but setAlpha
    // draws the lane and its regions into a separate image and blends it
    // back on every repaint, which was most of the drawing time with
    // automation shown.
    if (fade < 1.0f)
        g.fillAll (theme().arrangement.laneBg.withAlpha (1.0f - fade));
}

void ArrangementView::Lane::paintLiveWaveform (juce::Graphics& g, juce::Rectangle<float> area)
{
    // An audio take's waveform so far, as the input sends it (see
    // LiveWaveform). It's replaced by the take's real waveform when
    // recording stops.
    const auto& live = owner.engine.getLiveRecording();

    if (live.track != trackId || live.isEmpty() || area.getHeight() <= 2.0f)
        return;

    auto& geo = owner.geometry;
    const int left = juce::jmax (0, (int) std::floor (area.getX()));
    const int right = juce::jmin (getWidth(), (int) std::ceil (area.getRight()));

    if (right <= left)
        return;

    // The loudest slice under each pixel column. Slices left of the screen
    // are skipped with a binary search (their beats rise).
    std::vector<float> columns ((size_t) (right - left), 0.0f);
    auto first = std::lower_bound (live.beats.begin(), live.beats.end(), geo.xToBeat ((float) left));
    auto i = (size_t) std::distance (live.beats.begin(), first);
    i = i > 0 ? i - 1 : 0;

    for (; i < live.peaks.size(); ++i)
    {
        const int column = (int) std::floor (geo.beatToX (live.beats[i])) - left;

        if (column >= (int) columns.size())
            break;

        if (column >= 0)
            columns[(size_t) column] = juce::jmax (columns[(size_t) column], live.peaks[i]);
    }

    // Mirrored about the middle, like a finished region's waveform.
    const auto mid = area.getCentreY(), halfHeight = area.getHeight() * 0.5f;
    juce::Path wave;
    wave.startNewSubPath ((float) left, mid);

    for (int x = 0; x < (int) columns.size(); ++x)
        wave.lineTo ((float) (left + x), mid - columns[(size_t) x] * halfHeight);

    for (int x = (int) columns.size(); --x >= 0;)
        wave.lineTo ((float) (left + x), mid + columns[(size_t) x] * halfHeight);

    wave.closeSubPath();
    g.setColour (theme().arrangement.record.withAlpha (0.85f));
    g.fillPath (wave);
}

void ArrangementView::Lane::paint (juce::Graphics& g)
{
    IKAROS_PERF_PAINT ("ArrangementView::Lane");
    auto r = getLocalBounds();

    if (selected)
        g.fillAll (theme().arrangement.selectedLane);

    // Beat lines (faint), when zoomed in enough for them to be useful...
    auto& geo = owner.geometry;

    if (geo.pixelsPerBeat >= 6.0)
    {
        g.setColour (theme().arrangement.gridBeat);

        for (double beat = std::ceil (geo.scrollBeats); geo.beatToX (beat) <= (float) getWidth(); beat += 1.0)
            g.fillRect (juce::roundToInt (geo.beatToX (beat)), 0, 1, r.getHeight());
    }

    // ...and bar lines (stronger).
    g.setColour (theme().arrangement.gridBar);
    forEachVisibleBar (geo, getWidth(), barStride (geo, 8.0), [&] (int, int x) { g.fillRect (x, 0, 1, r.getHeight()); });

    g.setColour (theme().arrangement.laneDivider);
    g.fillRect (r.removeFromBottom (1));

    if (showFocusRing && hasKeyboardFocus (false))
        IkarosLookAndFeel::drawFocusRing (g, getLocalBounds().toFloat().reduced (1.0f), size::radiusXs);
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
    auto& tracks = owner.engine.getTracks();

    // On a Drummer track, a new Drummer region; on other instrument tracks, chords.
    const auto rid = tracks.drummer().isDrummerTrack (trackId) ? tracks.drummer().addDrummerRegionAt (trackId, beat)
                                                               : tracks.regions().createChordRegion (trackId, beat);

    if (rid != 0)
        owner.selection.selectRegion (trackId, rid);
}

} // namespace ikaros
