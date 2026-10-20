#pragma once

#include "ui/state/Selection.h"
#include "ui/widgets/ClipLight.h"
#include "ui/widgets/IconButton.h"
#include "ui/widgets/LevelMeter.h"

namespace ikaros
{

//==============================================================================
/**
    The "Recording" section of the track inspector.

      Audio track:      input, monitoring on/off, and an input level meter
                        with a clip light
      Instrument track: which keyboards it records from
*/
class RecordingView  : public juce::Component
{
public:
    RecordingView (AudioEngine&, Selection&);

    void refresh();
    void updateMeter();          // ~30 Hz, from the owner's timer
    int getIdealHeight() const;

    std::function<void()> onHeightChanged;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    AudioEngine& engine;
    Selection& selection;
    TrackId trackId = 0;
    bool isAudio = false;
    std::vector<AudioInput> inputs;
    juce::String shownState;   // what the section shows now (see refresh)

    juce::ComboBox inputBox;
    IconButton monitorButton { "Monitoring", std::nullopt, "Monitoring" };
    LevelMeter meter;
    ClipLight clipLight;
    juce::Rectangle<int> inputLabelArea, hintArea;

    static constexpr int sidePadding = 12;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RecordingView)
};

} // namespace ikaros
