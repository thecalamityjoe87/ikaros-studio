#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace ikaros
{

//==============================================================================
/**
    The app's single LookAndFeel.

    In JUCE, a LookAndFeel is the object components ask "how do I draw a slider /
    button / popup menu?". Installing one with
    LookAndFeel::setDefaultLookAndFeel() restyles every standard widget in the
    app, including menus and dialogs. Our custom components (Knob, TrackHeader,
    ...) paint themselves but still read colours and fonts from the tokens.
*/
class IkarosLookAndFeel  : public juce::LookAndFeel_V4
{
public:
    IkarosLookAndFeel();

    // Fonts
    juce::Typeface::Ptr getTypefaceForFont (const juce::Font&) override;
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
    juce::Font getLabelFont (juce::Label&) override;
    juce::Font getPopupMenuFont() override;
    juce::Font getMenuBarFont (juce::MenuBarComponent&, int itemIndex, const juce::String& itemText) override;

    // Buttons
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                               bool isMouseOverButton, bool isButtonDown) override;

    // Sliders: thin linear track with a round thumb, and the arc-style knob.
    void drawLinearSlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPos, float minSliderPos, float maxSliderPos,
                           juce::Slider::SliderStyle, juce::Slider&) override;

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPosProportional, float rotaryStartAngle,
                           float rotaryEndAngle, juce::Slider&) override;

    // Menu bar (Linux/Windows; macOS uses the native one)
    void drawMenuBarBackground (juce::Graphics&, int width, int height, bool isMouseOverBar,
                                juce::MenuBarComponent&) override;
    void drawMenuBarItem (juce::Graphics&, int width, int height, int itemIndex,
                          const juce::String& itemText, bool isMouseOverItem, bool isMenuOpen,
                          bool isMouseOverBar, juce::MenuBarComponent&) override;

    // Window frame (we draw our own title bar, so the window border is ours too)
    void drawResizableWindowBorder (juce::Graphics&, int width, int height,
                                    const juce::BorderSize<int>&, juce::ResizableWindow&) override;

    // Text fields
    void fillTextEditorBackground (juce::Graphics&, int width, int height, juce::TextEditor&) override;
    void drawTextEditorOutline (juce::Graphics&, int width, int height, juce::TextEditor&) override;

    // Scrollbars: slim, dark
    void drawScrollbar (juce::Graphics&, juce::ScrollBar&, int x, int y, int width, int height,
                        bool isScrollbarVertical, int thumbStartPosition, int thumbSize,
                        bool isMouseOver, bool isMouseDown) override;
    int getDefaultScrollbarWidth() override    { return 10; }

    /** Shared keyboard-focus ring, used by our custom components too. */
    static void drawFocusRing (juce::Graphics&, juce::Rectangle<float> bounds, float cornerRadius);

    /** Colour ID for a knob's value arc (set per-slider to the track colour). */
    static constexpr int knobArcColourId = 0x7f00001;
};

} // namespace ikaros
