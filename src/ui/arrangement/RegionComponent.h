#pragma once

#include "engine/EngineTypes.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace ikaros
{

//==============================================================================
/**
    A region (Tracktion clip) drawn on a track lane: rounded body tinted with
    the track colour, a darker label strip on top, and mini notes for MIDI or a
    waveform for audio.

    Click selects it, double-click opens it in the Editor, Delete removes it.
*/
class RegionComponent  : public juce::Component
{
public:
    RegionComponent (const RegionInfo&, juce::Colour trackColour);

    void update (const RegionInfo&, juce::Colour trackColour, bool isSelected);
    const RegionInfo& getInfo() const noexcept   { return info; }

    std::function<void()> onSelect;
    std::function<void()> onOpenEditor;
    std::function<void()> onDelete;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;
    void focusGained (FocusChangeType) override     { repaint(); }
    void focusLost (FocusChangeType) override       { repaint(); }

    // TODO(milestone-2): drag to move, drag edges to resize, loop by dragging the top-right corner.

private:
    RegionInfo info;
    juce::Colour colour;
    bool selected = false;

    void paintMidiNotes (juce::Graphics&, juce::Rectangle<float>);
    void paintWaveform (juce::Graphics&, juce::Rectangle<float>);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RegionComponent)
};

} // namespace ikaros
