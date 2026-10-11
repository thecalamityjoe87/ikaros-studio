#include "ui/arrangement/ArrangementContent.h"
#include "ui/arrangement/ArrangementLayout.h"

namespace ikaros
{

using namespace tokens;
using namespace arrangement;

ArrangementView::Content::Content (ArrangementView& o) : owner (o)
{
    addAndMakeVisible (dropZone);
}

void ArrangementView::Content::sync (const std::vector<TrackInfo>& tracks,
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
            header->onShowMenu = [this] { owner.showTrackMenu(); };
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

bool ArrangementView::Content::syncTracks (const std::vector<TrackInfo>& tracks)
{
    if (tracks.size() != order.size())
        return false;

    for (size_t i = 0; i < tracks.size(); ++i)
        if (tracks[i].id != order[i])
            return false;

    auto selectedTrack = owner.selection.getTrack();

    for (auto& t : tracks)
    {
        headers[t.id]->update (t, t.id == selectedTrack);
        lanes[t.id]->updateTrack (t, t.id == selectedTrack);
    }

    return true;
}

bool ArrangementView::Content::syncRegion (const RegionInfo& info)
{
    auto it = lanes.find (info.trackId);

    if (it == lanes.end() || it->second->findRegion (info.id) == nullptr)
        return false;

    it->second->updateRegion (info);
    return true;
}

double ArrangementView::Content::getEndBeat() const
{
    double end = 0.0;

    for (auto& [id, lane] : lanes)
        end = juce::jmax (end, lane->getEndBeat());

    return end;
}

TrackHeader* ArrangementView::Content::findHeader (TrackId id) const
{
    auto it = headers.find (id);
    return it != headers.end() ? it->second.get() : nullptr;
}

RegionComponent* ArrangementView::Content::findRegion (RegionId id) const
{
    for (auto& [trackId, lane] : lanes)
        if (auto* region = lane->findRegion (id))
            return region;

    return nullptr;
}

void ArrangementView::Content::timelineChanged()
{
    for (auto& [id, lane] : lanes)
    {
        lane->resized();
        lane->repaint();
    }
}

void ArrangementView::Content::setRecording (TrackId track, double startBeat, double endBeat)
{
    for (auto& [id, lane] : lanes)
    {
        if (id == track)
            lane->setRecordingRange (startBeat, endBeat);
        else
            lane->setRecordingRange (0.0, 0.0);
    }
}

int ArrangementView::Content::getIdealHeight() const
{
    return (int) order.size() * owner.geometry.rowHeight + dropZoneHeight + dropZoneMargin * 2;
}

void ArrangementView::Content::paint (juce::Graphics& g)
{
    g.fillAll (colour::windowBg);
    g.setColour (colour::sidebarBg);
    g.fillRect (0, 0, headerWidth - 1, getHeight());
    g.setColour (colour::divider);
    g.fillRect (headerWidth - 1, 0, 1, getHeight());
}

void ArrangementView::Content::resized()
{
    int y = 0;

    for (auto id : order)
    {
        headers[id]->setBounds (0, y, headerWidth - 1, owner.geometry.rowHeight);
        lanes[id]->setBounds (headerWidth, y, getWidth() - headerWidth, owner.geometry.rowHeight);
        y += owner.geometry.rowHeight;
    }

    dropZone.setBounds (headerWidth + dropZoneMargin, y + dropZoneMargin,
                        getWidth() - headerWidth - dropZoneMargin * 2, dropZoneHeight);
}

void ArrangementView::Content::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    // Wheel events bubble up here from the lanes and regions. Anything the
    // arrangement doesn't use goes on up to the Viewport (vertical scroll).
    if (! owner.handleWheel (e, wheel, false))
        juce::Component::mouseWheelMove (e, wheel);
}

} // namespace ikaros
