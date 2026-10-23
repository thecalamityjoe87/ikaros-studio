#include "ui/arrangement/ArrangementContent.h"
#include "ui/arrangement/ArrangementLayout.h"

namespace ikaros
{

using namespace tokens;
using namespace arrangement;

ArrangementView::Content::Content (ArrangementView& o) : owner (o)
{
    addAndMakeVisible (dropZone);

    addTrackButton.onClick = [this]
    {
        if (owner.onAddTrack)
            owner.onAddTrack();
    };
    addAndMakeVisible (addTrackButton);
}

void ArrangementView::Content::sync (const std::vector<TrackInfo>& tracks,
                                     const std::map<TrackId, std::vector<RegionInfo>>& regions)
{
    std::map<TrackId, std::unique_ptr<TrackHeader>> nextHeaders;
    std::map<TrackId, std::unique_ptr<Lane>> nextLanes;
    std::map<TrackId, std::unique_ptr<AutomationLane>> nextAutomationLanes;
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
            header->onDownloadPack = [this] (const juce::String& packId)
            {
                if (owner.onDownloadPackRequested)
                    owner.onDownloadPackRequested (packId);
            };
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

        // Moved across first (if there are any), so updateAutomation finds them.
        nextAutomationLanes[t.id] = std::move (automationLanes[t.id]);

        order.push_back (t.id);
        nextHeaders[t.id] = std::move (header);
        nextLanes[t.id] = std::move (lane);
    }

    headers = std::move (nextHeaders);
    lanes = std::move (nextLanes);
    automationLanes = std::move (nextAutomationLanes);

    for (auto& t : tracks)
        updateAutomation (t);
}

void ArrangementView::Content::updateAutomation (const TrackInfo& t)
{
    auto& lane = automationLanes[t.id];

    if (lane == nullptr)
    {
        lane = std::make_unique<AutomationLane> (owner, t.id);
        addChildComponent (*lane);
    }

    if (t.automationShown)
    {
        lane->setBounds (lanes[t.id]->getBounds());

        // Above the track's lane (false: without taking focus). Only when it
        // isn't already: toFront repaints the whole lane even when nothing
        // moves, and this runs on every change the engine reports (many a
        // second while automation plays), which redrew every lane each time.
        if (getIndexOfChildComponent (lane.get()) < getIndexOfChildComponent (lanes[t.id].get()))
            lane->toFront (false);

        lane->update (t);
    }

    lane->setVisible (t.automationShown);
}

bool ArrangementView::Content::syncTracks (const std::vector<TrackInfo>& tracks)
{
    if (tracks.size() != order.size())
        return false;

    for (size_t i = 0; i < tracks.size(); ++i)
        if (tracks[i].id != order[i])
            return false;

    auto selectedTrack = owner.selection.getTrack();

    bool rowHeightsChanged = false;

    for (auto& t : tracks)
    {
        rowHeightsChanged |= automationLanes[t.id]->isVisible() != t.automationShown;
        headers[t.id]->update (t, t.id == selectedTrack);
        lanes[t.id]->updateTrack (t, t.id == selectedTrack);
        updateAutomation (t);
    }

    // Automation opened or closed: that row's height changed, and the rows below move.
    if (rowHeightsChanged)
        owner.layoutContent();

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

void ArrangementView::Content::updateMeters()
{
    for (auto& entry : headers)
        entry.second->updateMeter();
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

    for (auto& [id, lane] : automationLanes)
        if (lane->isVisible())
            lane->repaint();
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
    return owner.getTracksHeight() + juce::jmax (dropZoneHeight + dropZoneMargin * 2, size::addTrackHeight + addTrackGap * 2);
}

void ArrangementView::Content::paint (juce::Graphics& g)
{
    // The lanes are part of the arrangement (dark in both themes); the
    // track headers column on the left is chrome.
    g.fillAll (theme().arrangement.laneBg);
    g.setColour (theme().trackColumnBg);
    g.fillRect (0, 0, headerWidth - 1, getHeight());
    g.setColour (theme().divider);
    g.fillRect (headerWidth - 1, 0, 1, getHeight());
}

void ArrangementView::Content::resized()
{
    int y = 0;

    for (size_t row = 0; row < order.size(); ++row)
    {
        const auto id = order[row];
        const int height = owner.rowHeightOf ((int) row);
        headers[id]->setBounds (0, y, headerWidth - 1, height);
        lanes[id]->setBounds (headerWidth, y, getWidth() - headerWidth, height);
        automationLanes[id]->setBounds (lanes[id]->getBounds());
        y += height;
    }

    addTrackButton.setBounds (columnPadding, y + addTrackGap, columnRight - columnPadding, size::addTrackHeight);
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
