#pragma once

#include "engine/AudioEngine.h"
#include "engine/AmpStyles.h"
#include "engine/EffectsController.h"
#include "ui/widgets/CachedShadow.h"

namespace ikaros
{

//==============================================================================
/**
    The Amp at the end of the pedalboard: a combo amp drawn in code from its
    voice's AmpVoiceStyle row (see AmpStyles.h), the way PedalView draws a
    pedal from its PedalStyle. A new voice is a new row, not new UI code.

        (|) o CRUNCH                  [Bright] [Voice  Crunch v]   <- top row
        [ (k)   (k)   (k)   (k)    (k)     (k) ]                  <- faceplate
        [ GAIN  BASS  MID  TREBLE PRESENCE MASTER ]
        [  (spk)   (spk)   (spk)   (spk)   [Cab 4x10 Open Back v] ] <- grille

    The round power button bypasses the Amp; the LED beside it is lit while
    it's on.
    The knobs turn with a drag, the mouse wheel or the arrow keys; Bright,
    Voice and Cab are buttons (the last two open menus). Right-click the
    Amp (or Shift+F10) for Remove; Delete removes it too. Everything talks
    to the engine through EffectsController, one undo step per change.

    The cabinet's covering, grille and speakers depend only on the voice and
    the cab, so they're drawn once into an image and kept until one of those
    changes.
*/
class AmpView  : public juce::Component
{
public:
    explicit AmpView (AudioEngine&);
    ~AmpView() override;

    /** Shows the Amp's current settings (EffectsController::getAmp).
        Knobs being dragged are left where the mouse has them. */
    void update (TrackId, const EffectsController::AmpSettings&);

    PluginId getPluginId() const noexcept    { return settings.pluginId; }

    /** Draws the Amp's drop shadow from its parent's paint() (it falls
        outside the Amp's own bounds). */
    static void drawShadow (juce::Graphics&, const AmpView&, CachedShadow& cache);

    static constexpr int width = 470, height = 216;

    //==============================================================================
    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;
    void focusGained (FocusChangeType) override     { repaint(); }
    void focusLost (FocusChangeType) override       { repaint(); }

private:
    class PowerButton;
    class PillButton;

    AudioEngine& engine;
    TrackId trackId = 0;
    EffectsController::AmpSettings settings;

    juce::OwnedArray<juce::Slider> knobs;            // one per ampKnobIds entry
    std::array<bool, ampKnobCount> dragging {};      // a knob the mouse is turning (one undo step per drag)
    std::unique_ptr<PowerButton> power;
    std::unique_ptr<PillButton> brightButton, voiceButton, cabButton;

    // The cached covering, grille and speakers (see the class comment).
    juce::Image background;
    int backgroundVoice = -1, backgroundCab = -1;
    float backgroundScale = 0.0f;

    const AmpVoiceStyle& style() const;
    void styleKnobs();
    void knobChanged (int index);
    void showVoiceMenu();
    void showCabMenu();
    void showMenu();
    void remove();
    void drawBackground (juce::Graphics&) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AmpView)
};

} // namespace ikaros
