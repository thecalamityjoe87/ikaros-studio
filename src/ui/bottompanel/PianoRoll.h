#pragma once

#include "engine/EngineTypes.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <optional>

namespace ikaros
{

//==============================================================================
/**
    The "Editor" tab: a piano roll for the selected MIDI region.

    Read-only in this milestone: it shows the notes and the playhead.
    TODO(milestone-2): add, move, resize and delete notes (via TrackController,
    so every edit is undoable), velocity lane, quantise.
*/
class PianoRoll  : public juce::Component
{
public:
    PianoRoll();

    void setRegion (std::optional<RegionInfo>, juce::Colour trackColour, double beatsPerBar);
    void setPlayheadBeat (double editBeat);

    void paint (juce::Graphics&) override;

private:
    std::optional<RegionInfo> region;
    juce::Colour colour;
    double beatsPerBar = 4.0;
    double playheadBeat = -1.0;     // relative to the region start; <0 = hidden
    int lowNote = 60, highNote = 71;

    static constexpr int keysWidth = 44;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PianoRoll)
};

} // namespace ikaros
