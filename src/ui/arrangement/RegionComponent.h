#pragma once

#include "engine/EngineTypes.h"
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
        corner is the top-right corner: dragging it repeats the content. */
    enum class Zone { body, nameStrip, leftEdge, rightEdge, loopCorner };

    RegionComponent (const RegionInfo&, juce::Colour trackColour);

    void update (const RegionInfo&, juce::Colour trackColour, bool isSelected);
    const RegionInfo& getInfo() const noexcept   { return info; }

    Zone getZoneAt (juce::Point<int>) const;

    /** While an edge is being dragged, the region is drawn with these edges
        (in beats) instead of its real ones. std::nullopt = back to normal.
        asLoop: the content repeats to fill the new length (a loop drag). */
    void setTrimPreview (std::optional<juce::Range<double>>, bool asLoop = false);

    /** The start and end beat as currently drawn (the trim preview, if any). */
    juce::Range<double> getDisplayRange() const;

    /** Shows a text field over the name strip. */
    void startRenaming();

    std::function<void (RegionComponent&, const juce::MouseEvent&)> onMouseDown, onMouseDrag, onMouseUp;
    std::function<void (RegionComponent&, Zone)> onDoubleClick;
    std::function<void (const juce::String& newName)> onRename;
    std::function<void()> onOpenEditor;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;
    void focusGained (FocusChangeType) override     { repaint(); }
    void focusLost (FocusChangeType) override       { repaint(); }

private:
    RegionInfo info;
    juce::Colour colour;
    bool selected = false;
    std::optional<juce::Range<double>> trimPreview;
    bool loopPreview = false;
    bool hoveringLoopCorner = false;

    juce::TextEditor nameEditor;
    bool renaming = false;

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

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RegionComponent)
};

} // namespace ikaros
