#pragma once

#include "ui/bottompanel/PedalView.h"
#include "ui/state/Selection.h"

namespace ikaros
{

//==============================================================================
/**
    The bottom panel's Pedals tab, for guitar and bass tracks (see
    EffectsController::hasPedalboard): the track's pedals as a row of
    stompboxes on a darker floor, like a guitarist's pedalboard.

        IN ---[pedal]---[pedal]---[pedal]---[ + Add pedal ]

    The panel's header names the track and holds the Tuner and Add Pedal
    buttons (see BottomPanel), so the board itself is all floor.

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

    /** False when there's no track, or its chain is full. */
    bool canAddPedal() const;

    /** Shows the menu of pedals to add at the end of the board, under
        `target` (the panel's Add Pedal button, or the board's Add slot). */
    void showAddMenu (juce::Component& target);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class Board;
    class AddSlot;

    AudioEngine& engine;
    Selection& selection;
    TrackId trackId = 0;   // the audio track shown (0 = none)

    juce::Viewport viewport;
    std::unique_ptr<Board> board;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PedalboardView)
};

} // namespace ikaros
