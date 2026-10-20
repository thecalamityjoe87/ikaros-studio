#pragma once

#include "engine/AudioEngine.h"
#include "engine/PedalStyles.h"
#include "ui/widgets/CachedShadow.h"

namespace ikaros
{

//==============================================================================
/**
    One stompbox on the pedalboard, drawn from its PedalStyle row and its
    effect's settings. Everything is vector graphics drawn in code:

        o    (LED)    o         <- screws, and the LED (lit = on)
         (k)  (k)  (k)          <- 2-3 knobs (juce::Sliders)
        DRIVE TONE LEVEL
          Yellow Drive
           OVERDRIVE
             ( O )              <- the footswitch (a real button): bypass

    The knobs and footswitch talk to the engine themselves. Mouse presses
    on the rest of the pedal (and its keyboard shortcuts) go to the owner,
    PedalboardView, which drags pedals to reorder them and shows the menu.

    The pedal's own colours are the same in light and dark themes; a
    bypassed pedal is drawn slightly greyed and dimmed.
*/
class PedalView  : public juce::Component
{
public:
    PedalView (AudioEngine&, const PedalStyle&);
    ~PedalView() override;

    /** Shows the pedal's current state: on or bypassed, and its knobs'
        values (getEffectParameters' list). Knobs being dragged are left
        where the mouse has them. */
    void update (TrackId, const PluginSlot&, const std::vector<ParameterInfo>&);

    PluginId getPluginId() const noexcept           { return slot.id; }
    const PedalStyle& getStyle() const noexcept     { return style; }
    bool isOn() const noexcept                      { return slot.enabled; }

    //==============================================================================
    // For the owner (PedalboardView).

    /** Mouse presses, drags and releases on the pedal's body (not on a
        knob or the footswitch). A right-click is onMenuRequested instead. */
    std::function<void (PedalView&, const juce::MouseEvent&)> onBodyMouseDown, onBodyMouseDrag, onBodyMouseUp;

    /** Right-click, or Shift+F10 while the pedal has keyboard focus. */
    std::function<void (PedalView&)> onMenuRequested;

    /** Ctrl+Left/Right (Cmd on a Mac) while it has keyboard focus: -1 or +1. */
    std::function<void (PedalView&, int direction)> onMoveRequested;

    /** Delete or Backspace while it has keyboard focus. */
    std::function<void (PedalView&)> onRemoveRequested;

    /** Draws a pedal's drop shadow. Call it from the parent's paint(): the
        shadow falls outside the pedal's own bounds. `cache` keeps the blurred
        shadow between paints; every pedal is the same size, so the parent
        needs only one. */
    static void drawShadow (juce::Graphics&, const PedalView&, CachedShadow& cache);

    /** A picture of a pedal, switched on and with its knobs at noon, drawn
        `scale` times its size (e.g. 0.5 at half size, times the screen's
        scale for sharp pixels). For showing pedals that aren't on a board
        (the Add Pedal dialog). It never touches the engine. */
    static juce::Image createPreview (AudioEngine&, const PedalStyle&, float scale);

    static constexpr int width = 156, height = 216;

    //==============================================================================
    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;
    void focusGained (FocusChangeType) override     { repaint(); }
    void focusLost (FocusChangeType) override       { repaint(); }

private:
    class Footswitch;

    AudioEngine& engine;
    const PedalStyle& style;   // a row of the static table (see getPedalStyles), so it outlives us
    TrackId trackId = 0;
    PluginSlot slot;

    // One per PedalStyle::knobs entry. juce::OwnedArray deletes them with the pedal.
    juce::OwnedArray<juce::Slider> knobs;
    std::vector<int> parameterIndex;   // each knob's index in getEffectParameters (-1 = not found)
    std::vector<juce::String> valueText;
    std::vector<bool> dragging;        // a knob the mouse is turning (one undo step for the whole drag)
    std::unique_ptr<Footswitch> footswitch;

    juce::Colour lettering() const;
    juce::Rectangle<int> knobArea (int index) const;
    void knobChanged (int index);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PedalView)
};

} // namespace ikaros
