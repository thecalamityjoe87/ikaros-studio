#include "ui/theme/IkarosLookAndFeel.h"
#include "ui/theme/Fonts.h"
#include "ui/theme/Tokens.h"

namespace ikaros
{

using namespace tokens;

IkarosLookAndFeel::IkarosLookAndFeel()
{
    // LookAndFeel_V4 is driven by a ColourScheme; set ours so any widget we
    // don't customise below still matches the palette.
    setColourScheme ({ colour::windowBg,        // windowBackground
                       colour::toolbarBg,       // widgetBackground
                       colour::wellBg,          // menuBackground
                       colour::border,          // outline
                       colour::text,            // defaultText
                       colour::raised,          // defaultFill
                       colour::textOnAccent,    // highlightedText
                       colour::accent,          // highlightedFill
                       colour::text });         // menuText

    setColour (juce::ResizableWindow::backgroundColourId, colour::windowBg);
    setColour (juce::DocumentWindow::textColourId, colour::text);

    setColour (juce::TextButton::buttonColourId, colour::raised);
    setColour (juce::TextButton::buttonOnColourId, colour::accent);
    setColour (juce::TextButton::textColourOffId, colour::text);
    setColour (juce::TextButton::textColourOnId, colour::textOnAccent);

    setColour (juce::Label::textColourId, colour::text);

    setColour (juce::Slider::trackColourId, colour::accent);
    setColour (juce::Slider::backgroundColourId, colour::controlFill);
    setColour (juce::Slider::thumbColourId, colour::text);
    setColour (knobArcColourId, colour::accent);

    setColour (juce::TextEditor::backgroundColourId, colour::wellBg);
    setColour (juce::TextEditor::textColourId, colour::text);
    setColour (juce::TextEditor::highlightColourId, colour::accent.withAlpha (0.35f));
    setColour (juce::TextEditor::outlineColourId, colour::borderSoft);
    setColour (juce::TextEditor::focusedOutlineColourId, colour::accent);
    setColour (juce::CaretComponent::caretColourId, colour::accent);

    setColour (juce::PopupMenu::backgroundColourId, colour::toolbarBg);
    setColour (juce::PopupMenu::textColourId, colour::text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, colour::accent);
    setColour (juce::PopupMenu::highlightedTextColourId, colour::textOnAccent);
    setColour (juce::PopupMenu::headerTextColourId, colour::textSecondary);

    setColour (juce::ScrollBar::thumbColourId, colour::borderStrong);
    setColour (juce::TooltipWindow::backgroundColourId, colour::raised);
    setColour (juce::TooltipWindow::textColourId, colour::text);
    setColour (juce::TooltipWindow::outlineColourId, colour::border);

    setColour (juce::ComboBox::backgroundColourId, colour::raised);
    setColour (juce::ComboBox::outlineColourId, colour::border);
    setColour (juce::ComboBox::textColourId, colour::text);
    setColour (juce::ComboBox::arrowColourId, colour::textSecondary);
    setColour (juce::ListBox::backgroundColourId, colour::wellBg);
    setColour (juce::ToggleButton::textColourId, colour::text);
    setColour (juce::ToggleButton::tickColourId, colour::accent);
    setColour (juce::AlertWindow::backgroundColourId, colour::toolbarBg);
    setColour (juce::AlertWindow::textColourId, colour::text);
    setColour (juce::AlertWindow::outlineColourId, colour::border);

    // Makes the bundled Figtree the fallback for any Font that doesn't name a typeface.
    setDefaultSansSerifTypeface (fonts::defaultTypeface());
}

//==============================================================================
juce::Typeface::Ptr IkarosLookAndFeel::getTypefaceForFont (const juce::Font& f)
{
    // Fonts created via ikaros::fonts already carry their typeface. Anything
    // else (default JUCE widgets) falls back to Figtree.
    if (f.getTypefaceName() == juce::Font::getDefaultSansSerifFontName())
        return fonts::defaultTypeface();

    return juce::LookAndFeel_V4::getTypefaceForFont (f);
}

juce::Font IkarosLookAndFeel::getTextButtonFont (juce::TextButton&, int buttonHeight)
{
    return fonts::uiSemiBold (juce::jmin (14.0f, (float) buttonHeight * 0.45f));
}

juce::Font IkarosLookAndFeel::getLabelFont (juce::Label& l)
{
    return l.getFont();
}

juce::Font IkarosLookAndFeel::getPopupMenuFont()
{
    return fonts::ui (14.0f);
}

juce::Font IkarosLookAndFeel::getMenuBarFont (juce::MenuBarComponent&, int, const juce::String&)
{
    return fonts::ui (13.0f);
}

//==============================================================================
void IkarosLookAndFeel::drawFocusRing (juce::Graphics& g, juce::Rectangle<float> bounds, float cornerRadius)
{
    g.setColour (colour::accent);
    g.drawRoundedRectangle (bounds.reduced (1.0f), cornerRadius, 2.0f);
}

void IkarosLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour& bg,
                                              bool over, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    auto fill = b.getToggleState() ? b.findColour (juce::TextButton::buttonOnColourId) : bg;

    if (down)       fill = fill.brighter (0.12f);
    else if (over)  fill = fill.brighter (0.06f);

    g.setColour (fill);
    g.fillRoundedRectangle (r, size::radius);

    g.setColour (colour::border);
    g.drawRoundedRectangle (r, size::radius, 1.0f);

    if (b.hasKeyboardFocus (false))
        drawFocusRing (g, r, size::radius);
}

//==============================================================================
void IkarosLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h,
                                          float sliderPos, float, float,
                                          juce::Slider::SliderStyle style, juce::Slider& s)
{
    const bool vertical = style == juce::Slider::LinearVertical;
    auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h);
    auto trackColour = s.findColour (juce::Slider::trackColourId);

    if (vertical)
    {
        // Mixer fader: dark slot with a light rectangular cap.
        auto slot = bounds.withSizeKeepingCentre (6.0f, bounds.getHeight());
        g.setColour (colour::wellBg);
        g.fillRoundedRectangle (slot, 3.0f);

        auto cap = juce::Rectangle<float> (26.0f, 14.0f).withCentre ({ slot.getCentreX(), sliderPos });
        g.setColour (juce::Colour (0xffc9d0da));
        g.fillRoundedRectangle (cap, 4.0f);
        g.setColour (colour::lcdLabel);
        g.fillRect (cap.removeFromBottom (2.0f).reduced (3.0f, 0.0f));
    }
    else
    {
        // Track header volume: thin track, accent fill, round thumb.
        auto track = bounds.withSizeKeepingCentre (bounds.getWidth(), 4.0f);
        g.setColour (colour::controlFill);
        g.fillRoundedRectangle (track, 2.0f);

        g.setColour (trackColour);
        g.fillRoundedRectangle (track.withRight (sliderPos), 2.0f);

        auto thumb = juce::Rectangle<float> (12.0f, 12.0f).withCentre ({ sliderPos, track.getCentreY() });
        g.setColour (colour::text);
        g.fillEllipse (thumb);
    }

    if (s.hasKeyboardFocus (false))
        drawFocusRing (g, bounds.expanded (2.0f), 4.0f);
}

void IkarosLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h,
                                          float pos, float startAngle, float endAngle, juce::Slider& s)
{
    // Arc-style knob: grey track arc, coloured value arc, round cap and a needle.
    auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (4.0f);
    auto size = juce::jmin (bounds.getWidth(), bounds.getHeight());
    auto centre = bounds.getCentre();
    auto radius = size * 0.5f - 3.0f;
    auto arcWidth = size * 0.09f;
    auto valueAngle = startAngle + pos * (endAngle - startAngle);

    juce::Path track;
    track.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, startAngle, endAngle, true);
    g.setColour (colour::controlFill);
    g.strokePath (track, { arcWidth, juce::PathStrokeType::curved, juce::PathStrokeType::rounded });

    juce::Path value;
    value.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, startAngle, juce::jmax (startAngle + 0.001f, valueAngle), true);
    g.setColour (s.findColour (knobArcColourId));
    g.strokePath (value, { arcWidth, juce::PathStrokeType::curved, juce::PathStrokeType::rounded });

    auto capRadius = radius * 0.68f;
    g.setColour (colour::controlFill);
    g.fillEllipse (juce::Rectangle<float> (capRadius * 2.0f, capRadius * 2.0f).withCentre (centre));
    g.setColour (colour::borderStrong);
    g.drawEllipse (juce::Rectangle<float> (capRadius * 2.0f, capRadius * 2.0f).withCentre (centre), 1.0f);

    // getPointOnCircumference measures angles clockwise from 12 o'clock, like the slider does.
    auto inner = centre.getPointOnCircumference (capRadius * 0.27f, valueAngle);
    auto outer = centre.getPointOnCircumference (capRadius * 0.8f, valueAngle);
    g.setColour (colour::text);
    g.drawLine ({ inner, outer }, size * 0.055f);

    if (s.hasKeyboardFocus (false))
        drawFocusRing (g, juce::Rectangle<float> (size, size).withCentre (centre).expanded (3.0f), size * 0.5f);
}

//==============================================================================
void IkarosLookAndFeel::drawMenuBarBackground (juce::Graphics& g, int w, int h, bool, juce::MenuBarComponent&)
{
    g.setColour (colour::menuBarBg);
    g.fillRect (0, 0, w, h);
}

void IkarosLookAndFeel::drawMenuBarItem (juce::Graphics& g, int w, int h, int, const juce::String& text,
                                         bool isMouseOverItem, bool isMenuOpen, bool, juce::MenuBarComponent&)
{
    auto r = juce::Rectangle<float> (0.0f, 2.0f, (float) w, (float) h - 4.0f);

    if (isMenuOpen || isMouseOverItem)
    {
        g.setColour (colour::raised);
        g.fillRoundedRectangle (r, size::radiusSmall);
    }

    g.setColour (isMenuOpen ? colour::text : colour::textMenu);
    g.setFont (fonts::ui (13.0f));
    g.drawFittedText (text, r.toNearestInt(), juce::Justification::centred, 1);
}

//==============================================================================
void IkarosLookAndFeel::drawResizableWindowBorder (juce::Graphics& g, int w, int h,
                                                   const juce::BorderSize<int>& border, juce::ResizableWindow&)
{
    // The border strip doubles as the resize handle. Fill it with the menu-bar
    // colour so it reads as part of the window, with a 1px outline so the
    // window edge stays visible against dark desktops.
    juce::Rectangle<int> r (w, h);
    g.setColour (colour::menuBarBg);
    g.fillRect (r.removeFromTop (border.getTop()));
    g.fillRect (r.removeFromBottom (border.getBottom()));
    g.fillRect (r.removeFromLeft (border.getLeft()));
    g.fillRect (r.removeFromRight (border.getRight()));

    g.setColour (colour::border);
    g.drawRect (0, 0, w, h, 1);
}

//==============================================================================
void IkarosLookAndFeel::fillTextEditorBackground (juce::Graphics& g, int w, int h, juce::TextEditor& e)
{
    g.setColour (e.findColour (juce::TextEditor::backgroundColourId));
    g.fillRoundedRectangle (juce::Rectangle<float> ((float) w, (float) h), size::radius);
}

void IkarosLookAndFeel::drawTextEditorOutline (juce::Graphics& g, int w, int h, juce::TextEditor& e)
{
    auto r = juce::Rectangle<float> ((float) w, (float) h).reduced (0.5f);
    const bool focused = e.hasKeyboardFocus (true) && ! e.isReadOnly();

    g.setColour (e.findColour (focused ? juce::TextEditor::focusedOutlineColourId
                                       : juce::TextEditor::outlineColourId));
    g.drawRoundedRectangle (r, size::radius, focused ? 2.0f : 1.0f);
}

//==============================================================================
void IkarosLookAndFeel::drawScrollbar (juce::Graphics& g, juce::ScrollBar&, int x, int y, int w, int h,
                                       bool vertical, int thumbStart, int thumbSize, bool over, bool down)
{
    auto thumb = vertical ? juce::Rectangle<int> (x, thumbStart, w, thumbSize)
                          : juce::Rectangle<int> (thumbStart, y, thumbSize, h);

    g.setColour (colour::borderStrong.brighter (down ? 0.3f : (over ? 0.15f : 0.0f)));
    g.fillRoundedRectangle (thumb.toFloat().reduced (2.5f), 3.0f);
}

} // namespace ikaros
