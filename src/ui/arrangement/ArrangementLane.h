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

    /** Re-reads everything: the track's settings and all its regions. */
    void update (const TrackInfo&, const std::vector<RegionInfo>& regionInfos, bool isSelected);

    /** Just the track's settings (colour, mute, selection), keeping the regions. */
    void updateTrack (const TrackInfo&, bool isSelected);

    /** Just one region, which must already be on this lane. */
    void updateRegion (const RegionInfo&);

    RegionComponent* findRegion (RegionId) const;

    /** Where the last region on this lane ends (0 if it has none). */
    double getEndBeat() const;

    /** The take being recorded on this lane, from startBeat to the playhead
        (nothing when endBeat <= startBeat). */
    void setRecordingRange (double startBeat, double endBeat);
    void paintLiveWaveform (juce::Graphics&, juce::Rectangle<float> area);   // in the recording region

    void paintOverChildren (juce::Graphics&) override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

private:
    ArrangementView& owner;
    const TrackId trackId;
    tokens::Family family = tokens::Family::loops;   // the track's, for its regions
    bool selected = false, isAudio = false, showFocusRing = false;
    float fade = 1.0f;   // how much of the lane shows through: less when muted or under automation
    double recordStart = 0.0, recordEnd = 0.0;
    std::map<RegionId, std::unique_ptr<RegionComponent>> regions;

    void setTrackInfo (const TrackInfo&, bool isSelected);
};

} // namespace ikaros
