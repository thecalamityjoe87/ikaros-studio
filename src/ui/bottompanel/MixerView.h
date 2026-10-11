#pragma once

#include "ui/widgets/LevelMeter.h"
#include "ui/state/Selection.h"
#include <map>

namespace ikaros
{

//==============================================================================
/**
    The "Mixer" tab: one channel strip per track (name, pan knob, Reverb and
    Delay send knobs, stereo meter, fader, dB readout) plus a master strip
    with its own fader.

    The send knobs feed the two shared effects (see SendBuses): turn one up
    to put the track in the reverb or echo that every track shares.
*/
class MixerView  : public juce::Component
{
public:
    MixerView (AudioEngine&, Selection&);
    ~MixerView() override;

    void refresh();            // model changed
    void updateMeters();       // ~30 Hz

    void resized() override;

private:
    class Strip;

    AudioEngine& engine;
    Selection& selection;
    juce::Viewport viewport;
    juce::Component stripHolder;
    std::map<TrackId, std::unique_ptr<Strip>> strips;
    std::vector<TrackId> order;
    std::unique_ptr<Strip> masterStrip;

    void layoutStrips();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MixerView)
};

} // namespace ikaros
