#pragma once

#include "ui/bottompanel/PedalView.h"
#include "ui/state/Selection.h"
#include "ui/widgets/IconButton.h"

namespace ikaros
{

//==============================================================================
/**
    The bottom panel's Pedals tab, for audio tracks: the track's pedals as
    a row of stompboxes, like a guitarist's pedalboard.

        [] Guitar 1  Pedalboard                       [Tuner] [+ Add Pedal]
        ---------------------------------------------------------------- (the floor)
        IN ---[pedal]---[pedal]---[pedal]---[ + Add pedal ]

    The pedals are the effects at the start of the track's chain that have a
    PedalStyle (see PedalStyles.h); the Plug-ins inspector lists the same
    effects, so both always agree. Drag a pedal sideways to reorder it;
    right-click it (or Shift+F10) for Move, Replace and Remove. The board
    scrolls sideways when the pedals don't fit.
*/
class PedalboardView  : public juce::Component
{
public:
    PedalboardView (AudioEngine&, Selection&);
    ~PedalboardView() override;

    /** Re-reads the selected track's pedals (model or selection changed). */
    void refresh();

    /** The Tuner button follows the tuner. Called by the owner's ~30 Hz timer. */
    void update();

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class Board;
    class AddSlot;

    AudioEngine& engine;
    Selection& selection;
    std::optional<TrackInfo> track;

    IconButton tunerButton { "Tuner", Icon::tuningFork, "Tuner" };
    IconButton addButton { "Add pedal", Icon::plus, "Add Pedal" };
    juce::Viewport viewport;
    std::unique_ptr<Board> board;
    juce::Rectangle<int> titleArea;

    void showAddMenu (juce::Component& target);

    static constexpr int headerHeight = 48;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PedalboardView)
};

} // namespace ikaros
