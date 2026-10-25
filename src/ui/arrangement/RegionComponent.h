#pragma once

#include "engine/EngineTypes.h"
#include "ui/theme/Family.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <optional>

namespace ikaros
{

//==============================================================================
/**
    A region (Tracktion clip) drawn on a track lane: rounded body tinted with
    the track colour, a darker label strip on top, and mini notes for MIDI or a
    waveform for audio.

    Mouse gestures (select, move, trim) are handled by the ArrangementView,
    which knows about the other selected regions and tracks. This component
    reports where on the region the mouse is (getZoneAt) and draws itself.
*/
class RegionComponent  : public juce::Component
{
public:
    /** The parts of a region that react differently to the mouse. The loop
        "corner" is a small handle at the top of the right edge: dragging it
        repeats the content. A matching handle at the bottom-right trims (it
        reports rightEdge, like the rest of the edge). The fade handles
        sit on an audio region's waveform, at the top, where each fade ends.
        A region with takes has a take badge in its name strip, which opens
        the take menu. */
    enum class Zone { body, nameStrip, leftEdge, rightEdge, loopCorner, fadeIn, fadeOut, takeBadge };

    RegionComponent (const RegionInfo&, tokens::Family);

    /** How the outline is drawn. The selected track's regions get a white
        outline; while some of its regions are picked, the others get a faint
        one, so it's still clear which regions the edits will act on. */
    enum class Outline { normal, selectedTrack, selectedTrackFaint };

    void update (const RegionInfo&, tokens::Family, bool isSelected, Outline);
    const RegionInfo& getInfo() const noexcept   { return info; }

    Zone getZoneAt (juce::Point<int>) const;

    /** While an edge is being dragged, the region is drawn with these edges
        (in beats) instead of its real ones. std::nullopt = back to normal.
        asLoop: the content repeats to fill the new length (a loop drag). */
    void setTrimPreview (std::optional<juce::Range<double>>, bool asLoop = false);

    /** While a fade handle is being dragged, the fades are drawn with these
        lengths (in beats) instead. std::nullopt = the region's own. */
    void setFadePreview (std::optional<double> fadeInBeats, std::optional<double> fadeOutBeats);

    /** The start and end beat as currently drawn (the trim preview, if any). */
    juce::Range<double> getDisplayRange() const;

    /** Shows a text field over the name strip. */
    void startRenaming();

    std::function<void (RegionComponent&, const juce::MouseEvent&)> onMouseDown, onMouseDrag, onMouseUp;
    std::function<void (RegionComponent&, Zone)> onDoubleClick;
    std::function<void (const juce::String& newName)> onRename;
    std::function<void()> onOpenEditor;

    /** The take badge was clicked (the region has takes). */
    std::function<void (RegionComponent&)> onTakeBadge;

    /** Where the take badge is drawn (empty if the region has no takes or is too narrow). */
    juce::Rectangle<int> getTakeBadgeBounds() const;
    juce::Rectangle<int> getTrimHandleBounds() const;

    /** Escape was pressed on the region. Return true if it was used (it
        cancelled a drag), so it doesn't go on to the app's shortcuts. */
    std::function<bool()> onEscape;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;
    void focusGained (FocusChangeType cause) override
    {
        // Only show the focus ring when focus came from the keyboard (Tab):
        // a click already shows the region as selected.
        showFocusRing = cause != focusChangedByMouseClick;
        repaint();
    }

    void focusLost (FocusChangeType) override       { repaint(); }

private:
    RegionInfo info;
    tokens::Family family;   // its track's: the colour is looked up when painting
    juce::Colour trackColour() const;
    bool selected = false;
    Outline outline = Outline::normal;
    bool showFocusRing = false;
    std::optional<juce::Range<double>> trimPreview;
    bool loopPreview = false;
    bool hoveringLoopCorner = false;
    bool hoveringTrimHandle = false;
    bool hoveringTakeBadge = false;
    bool pressedTakeBadge = false;                  // the press went to the badge: no drag
    bool hovering = false;                          // the fade handles show while the mouse is over the region
    std::optional<double> fadeInPreview, fadeOutPreview;

    juce::TextEditor nameEditor;
    bool renaming = false;
    float nameWidth = 0.0f;   // of info.name as drawn, so paint() can tell when the text is outside the clip

    void finishRenaming (bool keepNewName);
    // How the content repeats: one loop's length and how far into the loop
    // the region starts. An unlooped region is one "loop" of its own length,
    // so the same drawing code works for both.
    struct Repeat
    {
        bool looping = false;
        double length = 0.0, offset = 0.0;
    };

    Repeat getRepeat() const;
    void paintMidiNotes (juce::Graphics&, juce::Rectangle<float>);
    void paintWaveform (juce::Graphics&, juce::Rectangle<float>);
    void paintLoopDividers (juce::Graphics&, juce::Rectangle<float>);
    void paintFades (juce::Graphics&, juce::Rectangle<float>);

    double getFadeLength (bool fadeIn) const;              // in beats, as drawn (the preview, if any)
    juce::Point<float> getFadeHandle (bool fadeIn) const;  // where a fade handle is drawn

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RegionComponent)
};

} // namespace ikaros
