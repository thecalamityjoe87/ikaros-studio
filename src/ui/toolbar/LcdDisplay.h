#pragma once

#include "engine/EngineTypes.h"
#include "engine/Tuner.h"
#include "ui/widgets/IconButton.h"

namespace ikaros
{

//==============================================================================
/**
    The black "LCD" in the transport: BAR.BEAT, then three clickable fields
    for the session's tempo, time signature and key.

      01.1        [120.0 ⌃⌄]  [4/4]  [C maj ⌄]
      BAR · BEAT   BPM         TIME   KEY

    Clicking the position switches it between bars and beats and the song's
    time (minutes, seconds and tenths: 1:23.4, MIN · SEC), and back.

    Each field is a slightly lighter box that brightens on hover:
      - BPM: click to type, drag up/down, scroll, or use the small steppers.
      - Time signature: opens a small editor (beats per bar, beat value).
      - Key: opens a key picker (major/minor, then the root note).

    While the tuner is on, it shows the tuner instead:

      E2    |  '  '  ' [|] '  '  '  |    0      [mute]
      NOTE  ♭           0           ♯    CENTS

    The note and cents are amber, and green once in tune (within 3 cents);
    the needle moves along the scale from 50 cents flat to 50 sharp.
*/
class LcdDisplay  : public juce::Component,
                    public juce::TooltipClient   // a tooltip for the position (see getTooltip)
{
public:
    LcdDisplay();

    void setState (const TransportState&, const juce::String& keyName);

    /** A new tempo from typing, a stepper or the wheel: one undo step each. */
    std::function<void (double bpm)> onBpmChanged;

    /** A BPM drag: onBpmDragStarted once (start the undo step there), then
        onBpmDragged for every move, so the whole drag undoes in one go. */
    std::function<void()> onBpmDragStarted;
    std::function<void (double bpm)> onBpmDragged;

    std::function<void (int numerator, int denominator)> onTimeSignatureChanged;
    std::function<void (int root, bool minor)> onKeyChanged;

    /** The position as the song's time (true) or bars and beats (false).
        Clicking the position switches it. */
    void setShowsTime (bool);
    bool isShowingTime() const noexcept     { return showsTime; }

    /** Shows the tuner instead of the position and session settings. */
    void setTunerShown (bool);
    bool isTunerShown() const noexcept      { return tunerShown; }

    /** What the tuner shows: its reading, whether the selected track has an
        audio input to listen to, and whether Mute while tuning is on. */
    void setTunerState (const TunerReading&, bool hasInput, bool muting);

    /** The mute button in the tuner: turn Mute while tuning on or off. */
    std::function<void()> onTunerMuteClicked;

    /** The width this component wants, given its content. */
    static constexpr int preferredWidth = 328;

    //==============================================================================
    // The LCD's drawing, for other readouts that look like it (Tune's
    // INPUT -> TARGET readout). The tuner draws with these too.

    /** The black panel with its border. */
    static void paintPanel (juce::Graphics&, juce::Rectangle<float>);

    /** A note as the tuner shows it: "E" and a smaller "2" beside it, on the
        same baseline, at the bottom left of `row` (the value row's height). */
    static void paintNote (juce::Graphics&, juce::Rectangle<int> row, const juce::String& name, int octave, juce::Colour);

    /** A short bar where a value would be, when there's nothing to show. */
    static void paintBlank (juce::Graphics&, juce::Rectangle<int> row, float width);

    /** A small caption under a value ("NOTE", "CENTS"). */
    static void paintCaption (juce::Graphics&, juce::Rectangle<int>, const juce::String&, juce::Justification);

    /** The height of the value row (the caption sits under it). */
    static int getValueRowHeight() noexcept;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    juce::String getTooltip() override;

    //==============================================================================
    /** One clickable value with its caption under it. Paints the lighter
        field and the hover state; onClick runs on a click (not a drag). */
    class Field  : public juce::Component,
                   public juce::SettableTooltipClient   // adds setTooltip()
    {
    public:
        Field (const juce::String& accessibleName, const juce::String& caption, bool showChevron);

        void setValueText (const juce::String&);
        std::function<void()> onClick;

        void paint (juce::Graphics&) override;
        void mouseUp (const juce::MouseEvent&) override;
        bool keyPressed (const juce::KeyPress&) override;

    protected:
        /** Where the value is drawn (the top row, minus any steppers or chevron). */
        juce::Rectangle<int> getValueArea() const;

        /** Extra width on the right of the value row, for steppers or a chevron. */
        int trailingWidth = 0;

        juce::String valueText;

    private:
        juce::String caption;
        bool showChevron;
    };

    /** The BPM field: also has up/down steppers, a vertical drag, the mouse
        wheel, and typing (click without dragging). */
    class TempoField  : public Field
    {
    public:
        explicit TempoField (LcdDisplay& owner);

        void setBpm (double);
        bool isBeingEdited() const      { return editor.isBeingEdited(); }

        void paint (juce::Graphics&) override;
        void resized() override;
        void mouseMove (const juce::MouseEvent&) override;
        void mouseExit (const juce::MouseEvent&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
        bool keyPressed (const juce::KeyPress&) override;

    private:
        enum class Part { value, stepUp, stepDown };

        LcdDisplay& owner;
        double bpm = 120.0;

        // Only shown while typing a new tempo: a Label turns into a text
        // field on showEditor(), and commits when it loses focus or Return.
        juce::Label editor;

        Part hoveredPart = Part::value, pressedPart = Part::value;
        double dragStartBpm = 0.0;
        bool dragging = false;

        juce::Rectangle<int> getStepperArea (Part) const;
        Part getPartAt (juce::Point<int>) const;
        void nudge (double amount);
    };

private:
    TempoField tempoField { *this };
    Field timeSigField { "Time signature", "TIME", false };
    Field keyField { "Key", "KEY", true };

    juce::String positionText { "01.1" };
    juce::String barText { "01.1" }, timeText { "0:00.0" }, countInText;   // the position each way (countInText: during a count-in)
    bool showsTime = false;            // the position as the song's time, not bars and beats
    int keyRoot = 0;
    bool keyMinor = false;
    double bpm = 120.0;
    int timeSigNum = 4, timeSigDen = 4;

    juce::Rectangle<int> positionArea;

    // The tuner
    IconButton tunerMuteButton { "Mute while tuning", Icon::mute };
    bool tunerShown = false;
    TunerReading tunerReading;
    bool tunerHasInput = false, tunerMuting = true;
    juce::Rectangle<int> noteArea, scaleArea, centsArea;

    void paintTuner (juce::Graphics&);
    void showTimeSignatureEditor();
    void showKeyPicker();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LcdDisplay)
};

} // namespace ikaros
