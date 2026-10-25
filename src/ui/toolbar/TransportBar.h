#pragma once

#include "ui/widgets/IconButton.h"
#include "ui/toolbar/LcdDisplay.h"
#include "ui/toolbar/MetronomeButton.h"

namespace ikaros
{

class AudioEngine;

//==============================================================================
/**
    The rounded "pill" in the middle of the toolbar:
    Go to start · Play · Record · Loop · LCD · Tuner · Metronome · Count-in.
*/
class TransportBar  : public juce::Component
{
public:
    explicit TransportBar (AudioEngine&);

    /** Called by the owner's timer (~30 Hz). */
    void update();

    int getPreferredWidth() const;

    /** For hints about recording to point at. */
    juce::Component& getRecordButton() noexcept     { return recordButton; }

    /** The LCD, for the session's view state (bars and beats or time). */
    LcdDisplay& getLcd() noexcept                   { return lcd; }

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    AudioEngine& engine;

    IconButton toStartButton   { "Go to start", Icon::toStart };
    IconButton playButton      { "Play", Icon::play };
    IconButton recordButton    { "Record", Icon::record };
    IconButton loopButton      { "Loop", Icon::loop };
    LcdDisplay lcd;
    IconButton tunerButton     { "Tuner", Icon::tuningFork };
    MetronomeButton metronomeButton;
    IconButton countInButton   { "Count-in", Icon::timer, "Count-in" };

    int bpmDragIndex = 0;   // the tempo change a BPM drag in the readout is changing

    bool wasPlaying = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TransportBar)
};

} // namespace ikaros
