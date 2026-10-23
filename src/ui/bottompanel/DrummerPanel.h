#pragma once

#include "engine/AudioEngine.h"
#include "engine/DrummerTypes.h"
#include "ui/theme/Family.h"
#include "ui/widgets/IconButton.h"
#include "ui/widgets/Knob.h"

namespace ikaros
{

//==============================================================================
/**
    The Drummer editor: the bottom panel's Editor tab for a Drummer region
    (see DrummerEditor), like GarageBand's.

        [ Rock        ]   +-----------------+   [Kick & Snare] [Hi-hat ]
        [ Pop         ]   |  Loud           |   [Cymbals     ] [Toms   ]
        [ Funk & Soul ]   |        o        |   [Hi-hat | Ride | Auto ]
        [ Hip-Hop     ]   |  Soft           |   (Fills) (Swing)
        [ Songwriter  ]   +-Simple--Complex-+   [New Variation]

    Left, a card per drummer; the chosen one is highlighted. In the middle,
    an XY pad: across is Simple to Complex, up is Soft to Loud. Dragging the
    puck changes the part as the song plays, and letting go makes the drag
    one undo step; the arrow keys move it too. On the right, the pieces
    that play (real toggle buttons), what keeps time, the Fills and Swing
    knobs, and New Variation. Every change is an undo step of its own.
*/
class DrummerPanel  : public juce::Component
{
public:
    explicit DrummerPanel (AudioEngine&);
    ~DrummerPanel() override;

    /** The region to show (a Drummer region), or nothing. Re-reads its
        settings, except what's being dragged. */
    void setRegion (std::optional<RegionInfo>, tokens::Family);

    void paint (juce::Graphics&) override;
    void resized() override;

    // The layout's sizes (see resized()), and from them the smallest the
    // panel can be with nothing cut off. Smaller than that, the bottom
    // panel scrolls it (sideways or up and down) instead.
    static constexpr int padding = 14;                 // tokens::size::panelPadding
    static constexpr int columnGap = 16;
    static constexpr int cardsWidth = 240, cardGap = 6, minCardHeight = 32;
    static constexpr int controlsWidth = 250;          // the piece buttons and the time-keeping switch
    static constexpr int gap = 8;
    static constexpr int variationHeight = 30;
    static constexpr int rightWidth = controlsWidth + columnGap + Knob::preferredWidth * 2 + gap;
    static constexpr int minPadWidth = 260;
    static constexpr int controlsHeight = 18 + 32 + gap + 32 + gap + 6 + 18 + 30;   // labels, two rows of pieces, the switch
    static constexpr int knobsHeight = Knob::preferredHeight + gap + variationHeight;

    static constexpr int minimumWidth = padding * 2 + cardsWidth + columnGap + minPadWidth + columnGap + rightWidth;
    static constexpr int minimumHeight = padding * 2 + juce::jmax (5 * minCardHeight + 4 * cardGap, controlsHeight, knobsHeight);

private:
    class DrummerCard;
    class XYPad;

    AudioEngine& engine;
    RegionId region = 0;
    tokens::Family family = tokens::Family::drums;
    DrummerSettings settings;
    bool previewing = false;                 // a drag (XY pad or knob) is playing its settings without undo
    std::vector<int> silentBars;             // bars the Drummer leaves out (not 4/4)

    juce::OwnedArray<DrummerCard> cards;     // OwnedArray deletes them with the panel
    std::unique_ptr<XYPad> pad;

    IconButton kickSnareButton { "Kick and snare", Icon::drums, "Kick & Snare" };
    IconButton hihatButton     { "Hi-hat", Icon::hihat, "Hi-hat" };
    IconButton cymbalsButton   { "Cymbals", Icon::cymbal, "Cymbals" };
    IconButton tomsButton      { "Toms", Icon::tom, "Toms" };

    // What keeps time: a segmented control.
    IconButton hihatKeeper { "Hi-hat keeps time", std::nullopt, "Hi-hat" };
    IconButton rideKeeper  { "Ride keeps time", std::nullopt, "Ride" };
    IconButton autoKeeper  { "Hi-hat or ride by loudness", std::nullopt, "Auto" };

    Knob fillsKnob { "Fills" }, swingKnob { "Swing" };
    IconButton variationButton { "New variation", Icon::reset, "New Variation" };

    juce::Rectangle<int> cardsArea, padArea, piecesLabel, keeperLabel, keeperArea, noticeArea;

    /** Changes the settings as one undo step. */
    void apply (const DrummerSettings&, const juce::String& undoName);

    /** During a drag: plays the settings at once, with no undo step yet. */
    void preview (const DrummerSettings&);
    void finishPreview (const juce::String& undoName);

    void togglePieces (DrumGroup);
    void updateControls();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DrummerPanel)
};

} // namespace ikaros
