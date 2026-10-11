#pragma once

// Internal to ArrangementView: only its own .cpp files include this.

#include "ui/arrangement/ArrangementView.h"

namespace ikaros
{

//==============================================================================
// One track's timeline lane: grid + regions. Double-click creates a region.
class ArrangementView::Lane  : public juce::Component,
                              public juce::SettableTooltipClient
{
public:
    Lane (ArrangementView&, TrackId);

    void focusGained (FocusChangeType) override;
    void focusLost (FocusChangeType) override    { showFocusRing = false; repaint(); }

    void update (const TrackInfo&, const std::vector<RegionInfo>& regionInfos, bool isSelected);
    RegionComponent* findRegion (RegionId) const;

    /** The take being recorded on this lane, from startBeat to the playhead
        (nothing when endBeat <= startBeat). */
    void setRecordingRange (double startBeat, double endBeat);

    void paintOverChildren (juce::Graphics&) override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

private:
    ArrangementView& owner;
    const TrackId trackId;
    juce::Colour colour;
    bool selected = false, isAudio = false, showFocusRing = false;
    double recordStart = 0.0, recordEnd = 0.0;
    std::map<RegionId, std::unique_ptr<RegionComponent>> regions;
};

} // namespace ikaros
