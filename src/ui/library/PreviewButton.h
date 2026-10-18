#pragma once

#include "ui/widgets/IconButton.h"

namespace ikaros
{

//==============================================================================
/**
    The round play button on a library row, for hearing a sound or loop
    before using it. It's the same everywhere in the library (instrument
    sounds, loops, audio files, the loop browser): a light circle at the
    row's right edge, shown while the row is hovered, and kept showing with
    a stop icon while that row's item plays.

    The row owns it as a child component and calls updateVisibility() from
    its mouseEnter and mouseExit, and whenever what's playing changes.
    Set onStateChange to do the same, because leaving the button for
    somewhere outside the row sends the row no mouseExit (the row wasn't the
    component under the mouse; the button was).
*/
class PreviewButton  : public IconButton
{
public:
    /** `itemName` is for screen readers: "Preview Soft Piano". */
    explicit PreviewButton (const juce::String& itemName);

    /** Playing shows a stop icon. Not available greys it out, with a
        tooltip saying why (sounds that can't be previewed yet). */
    void setState (bool playing, bool available = true);

    /** Shows it while `row` (or this button) is under the mouse, or while
        its item plays. */
    void updateVisibility (juce::Component& row);

    /** Where it goes in a row with these bounds: at the right edge,
        vertically centred. */
    static juce::Rectangle<int> getBoundsIn (juce::Rectangle<int> row);

private:
    bool playing = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PreviewButton)
};

} // namespace ikaros
