#pragma once

#include "ui/theme/Family.h"
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

    /** Copies the current theme's colours into the LookAndFeel's colour
        table, which JUCE's standard widgets (popup menus, text fields,
        tooltips...) read with findColour(). Call it after switching themes;
        our own drawing code reads the theme directly. */
    void applyThemeColours();

    // Fonts
    juce::Typeface::Ptr getTypefaceForFont (const juce::Font&) override;
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
    juce::Font getLabelFont (juce::Label&) override;
    juce::Font getPopupMenuFont() override;
    void getIdealPopupMenuItemSize (const juce::String& text, bool isSeparator, int standardMenuItemHeight,
                                    int& idealWidth, int& idealHeight) override;
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

    // The value bubble shown while a slider is dragged (setPopupDisplayEnabled)
    juce::Font getSliderPopupFont (juce::Slider&) override;
    int getSliderThumbRadius (juce::Slider&) override;

    // Menu bar (Linux/Windows; macOS uses the native one)
    void drawMenuBarBackground (juce::Graphics&, int width, int height, bool isMouseOverBar,
                                juce::MenuBarComponent&) override;
    void drawMenuBarItem (juce::Graphics&, int width, int height, int itemIndex,
                          const juce::String& itemText, bool isMouseOverItem, bool isMenuOpen,
                          bool isMouseOverBar, juce::MenuBarComponent&) override;

    // Window frame (we draw our own title bar, so the window border is ours too)
    void drawResizableWindowBorder (juce::Graphics&, int width, int height,
                                    const juce::BorderSize<int>&, juce::ResizableWindow&) override;

    // Popovers (CallOutBox), e.g. the effect settings
    void drawCallOutBoxBackground (juce::CallOutBox&, juce::Graphics&, const juce::Path&, juce::Image&) override;
    float getCallOutBoxCornerSize (const juce::CallOutBox&) override;

    // Text fields
    void fillTextEditorBackground (juce::Graphics&, int width, int height, juce::TextEditor&) override;
    void drawTextEditorOutline (juce::Graphics&, int width, int height, juce::TextEditor&) override;

    // Scrollbars: slim, dark
    void drawScrollbar (juce::Graphics&, juce::ScrollBar&, int x, int y, int width, int height,
                        bool isScrollbarVertical, int thumbStartPosition, int thumbSize,
                        bool isMouseOver, bool isMouseDown) override;
    int getDefaultScrollbarWidth() override;

    /** Styles a text field that edits something in place (a section name,
        a tempo value): raised fill with a light grey outline. Sets the
        property "inline" = true, which fillTextEditorBackground and
        drawTextEditorOutline check when they paint. */
    static void styleInlineEditor (juce::TextEditor&);

    /** Shared keyboard-focus ring, used by our custom components too. */
    static void drawFocusRing (juce::Graphics&, juce::Rectangle<float> bounds, float cornerRadius);

    /** Colour ID for a knob's value arc: the default is neutral grey (see
        setFamilyColour for knobs that follow a track). Rotary sliders with
        the property "bipolar" = true draw the arc from the centre. */
    static constexpr int knobArcColourId = 0x7f00001;

    /** Which shade of a family colour a slider uses: ink for controls on
        the chrome (pan knobs, inspector sliders), knobArc for the Smart
        Controls knobs on their cards. See ThemePalette. */
    enum class FamilyShade { ink, knobArc };

    /** Colours a knob's value arc, or a linear slider's fill, in an
        instrument family's colour. Only the family is stored (as slider
        properties); the colour is looked up from the current theme each
        time the slider is drawn, so it follows theme changes. */
    static void setFamilyColour (juce::Slider&, tokens::Family, FamilyShade = FamilyShade::ink);

private:
    // The colour of a slider's value arc or fill: its family's, if it has
    // one, else the given colour ID.
    static juce::Colour valueColourFor (juce::Slider&, int colourId);

    // drawRotarySlider for knobs with the property "compact" = true.
    void drawCompactKnob (juce::Graphics&, juce::Rectangle<float> area, float sliderPos,
                          float startAngle, float endAngle, juce::Slider&);
};

} // namespace ikaros
