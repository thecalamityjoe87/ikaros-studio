#pragma once

// Internal to ArrangementView: only its own .cpp files include this.

#include "ui/arrangement/ArrangementLane.h"       // Content owns its Lanes...
#include "ui/arrangement/ArrangementOverlays.h"   // ...and holds a DropZone by value

namespace ikaros
{

//==============================================================================
// Everything that scrolls vertically: headers on the left, lanes on the right.
class ArrangementView::Content  : public juce::Component
{
public:
    explicit Content (ArrangementView&);

    void sync (const std::vector<TrackInfo>& tracks,
               const std::map<TrackId, std::vector<RegionInfo>>& regions);

    /** Updates the headers and lanes of the tracks already shown, without
        re-reading their regions. Returns false (and does nothing) if the
        tracks themselves changed: added, removed or reordered. */
    bool syncTracks (const std::vector<TrackInfo>& tracks);

    /** Updates one region already shown on its track's lane. Returns false
        if it isn't there (it's new, or moved to another track). */
    bool syncRegion (const RegionInfo&);

    TrackHeader* findHeader (TrackId) const;
    RegionComponent* findRegion (RegionId) const;

    /** Where the last region on any track ends. */
    double getEndBeat() const;

    /** Re-positions regions and redraws the grid after a zoom or scroll. */
    void timelineChanged();

    void setRecording (TrackId track, double startBeat, double endBeat);
    int getIdealHeight() const;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    ArrangementView& owner;
    std::map<TrackId, std::unique_ptr<TrackHeader>> headers;
    std::map<TrackId, std::unique_ptr<Lane>> lanes;
    std::vector<TrackId> order;
    DropZone dropZone;
};

} // namespace ikaros
