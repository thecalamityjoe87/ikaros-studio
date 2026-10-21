#include "ui/theme/IkarosLookAndFeel.h"
#include "ui/theme/Fonts.h"
#include "ui/theme/Tokens.h"
#include <tuple>

namespace ikaros
{

using namespace tokens;

IkarosLookAndFeel::IkarosLookAndFeel()
{
    applyThemeColours();

    // Makes the bundled Figtree the fallback for any Font that doesn't name a typeface.
    setDefaultSansSerifTypeface (fonts::defaultTypeface());
}

void IkarosLookAndFeel::applyThemeColours()
{
    // LookAndFeel_V4 is driven by a ColourScheme; set ours so any widget we
    // don't customise below still matches the palette.
    setColourScheme ({ theme().windowBg,        // windowBackground
                       theme().toolbarBg,       // widgetBackground
                       theme().wellBg,          // menuBackground
                       theme().border,          // outline
                       theme().text,            // defaultText
                       theme().raised,          // defaultFill
                       theme().text,            // highlightedText
                       theme().neutralActive,   // highlightedFill (not the accent: that only means "on")
                       theme().text });         // menuText

    setColour (juce::ResizableWindow::backgroundColourId, theme().windowBg);
    setColour (juce::DocumentWindow::textColourId, theme().text);

    setColour (juce::TextButton::buttonColourId, theme().raised);
    setColour (juce::TextButton::buttonOnColourId, theme().neutralActive);
    setColour (juce::TextButton::textColourOffId, theme().text);
    setColour (juce::TextButton::textColourOnId, theme().text);

    setColour (juce::Label::textColourId, theme().text);
    setColour (juce::Label::textWhenEditingColourId, theme().text);
    setColour (juce::Label::backgroundWhenEditingColourId, theme().raised);
    setColour (juce::Label::outlineWhenEditingColourId, theme().highlight);

    // Slider fills and knob arcs default to neutral grey; track controls set
    // their own (the track colour) per slider.
    setColour (juce::Slider::trackColourId, theme().sliderFill);
    setColour (juce::Slider::backgroundColourId, theme().sliderTrack);
    setColour (juce::Slider::thumbColourId, theme().sliderThumb);
    setColour (knobArcColourId, theme().textSecondary);

    setColour (juce::TextEditor::backgroundColourId, theme().inputBg);
    setColour (juce::TextEditor::textColourId, theme().text);
    setColour (juce::TextEditor::highlightColourId, theme().highlight.withAlpha (0.25f));
    setColour (juce::TextEditor::outlineColourId, theme().inputBorder);
    setColour (juce::TextEditor::focusedOutlineColourId, theme().selectedBorder);
    setColour (juce::CaretComponent::caretColourId, theme().text);

    setColour (juce::PopupMenu::backgroundColourId, theme().popupBg);
    setColour (juce::PopupMenu::textColourId, theme().text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, theme().neutralActive);
    setColour (juce::PopupMenu::highlightedTextColourId, theme().text);
    setColour (juce::PopupMenu::headerTextColourId, theme().textSecondary);

    setColour (juce::ScrollBar::thumbColourId, theme().borderStrong);
    setColour (juce::TooltipWindow::backgroundColourId, theme().raised);
    setColour (juce::TooltipWindow::textColourId, theme().text);
    setColour (juce::TooltipWindow::outlineColourId, theme().border);

    // The value bubble a slider shows while it's dragged (see
    // Slider::setPopupDisplayEnabled). Its text uses the tooltip's colour.
    setColour (juce::BubbleComponent::backgroundColourId, theme().raised);
    setColour (juce::BubbleComponent::outlineColourId, theme().border);

    setColour (juce::ComboBox::backgroundColourId, theme().raised);
    setColour (juce::ComboBox::outlineColourId, theme().border);
    setColour (juce::ComboBox::textColourId, theme().text);
    setColour (juce::ComboBox::arrowColourId, theme().textSecondary);
    setColour (juce::ListBox::backgroundColourId, theme().wellBg);
    setColour (juce::ListBox::outlineColourId, theme().borderSoft);
    setColour (juce::ToggleButton::textColourId, theme().text);
    setColour (juce::ToggleButton::tickColourId, theme().accent);
    setColour (juce::AlertWindow::backgroundColourId, theme().dialogBg);
    setColour (juce::AlertWindow::textColourId, theme().text);
    setColour (juce::AlertWindow::outlineColourId, theme().border);
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
    return fonts::uiSemiBold (juce::jmin (textSize::lg, (float) buttonHeight * 0.45f));
}

juce::Font IkarosLookAndFeel::getLabelFont (juce::Label& l)
{
    return l.getFont();
}

juce::Font IkarosLookAndFeel::getPopupMenuFont()
{
    return fonts::ui (textSize::lg);
}

void IkarosLookAndFeel::getIdealPopupMenuItemSize (const juce::String& text, bool isSeparator, int standardHeight,
                                                   int& idealWidth, int& idealHeight)
{
    // Roomier menu items than JUCE's default (~18 px): easier to hit.
    juce::LookAndFeel_V4::getIdealPopupMenuItemSize (text, isSeparator, isSeparator ? standardHeight : juce::jmax (standardHeight, 30),
                                                     idealWidth, idealHeight);
    idealWidth += 16;
}

juce::Font IkarosLookAndFeel::getMenuBarFont (juce::MenuBarComponent&, int, const juce::String&)
{
    return fonts::ui (textSize::md);
}

//==============================================================================
void IkarosLookAndFeel::styleInlineEditor (juce::TextEditor& editor)
{
    // A flag rather than colours, so the colours are read from the theme
    // each time the field paints.
    editor.getProperties().set ("inline", true);
    editor.repaint();
}

void IkarosLookAndFeel::setFamilyColour (juce::Slider& s, Family f, FamilyShade shade)
{
    // getProperties() is a free-form NamedValueSet every component carries.
    // A juce::var holds an int, not our enums, hence the casts. set() says
    // whether the value changed: views call this on every refresh (many a
    // second while automation plays), so only repaint when it did.
    const bool familyChanged = s.getProperties().set ("family", static_cast<int> (f));
    const bool shadeChanged = s.getProperties().set ("familyShade", static_cast<int> (shade));

    if (familyChanged || shadeChanged)
        s.repaint();
}

juce::Colour IkarosLookAndFeel::valueColourFor (juce::Slider& s, int colourId)
{
    const auto& family = s.getProperties()["family"];   // a void var if it was never set

    if (family.isVoid())
        return s.findColour (colourId);

    const auto f = static_cast<Family> ((int) family);
    const auto shade = static_cast<FamilyShade> ((int) s.getProperties()["familyShade"]);
    return shade == FamilyShade::knobArc ? theme().knobArc (f) : theme().ink (f);
}

void IkarosLookAndFeel::drawFocusRing (juce::Graphics& g, juce::Rectangle<float> bounds, float cornerRadius)
{
    g.setColour (theme().highlight);
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

    g.setColour (theme().border);
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

    // An automated control (see setAutomatedLook) has an amber thumb: the
    // automation moves it, not the user.
    const bool automated = s.getProperties()["automated"];
    auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h);
    auto trackColour = valueColourFor (s, juce::Slider::trackColourId);

    if (vertical)
    {
        // Mixer fader: dark slot with a light rectangular cap.
        auto slot = bounds.withSizeKeepingCentre (6.0f, bounds.getHeight());
        g.setColour (theme().wellBg);
        g.fillRoundedRectangle (slot, size::radiusTiny);

        auto cap = juce::Rectangle<float> (26.0f, 14.0f).withCentre ({ slot.getCentreX(), sliderPos });
        g.setColour (automated ? theme().automated : theme().faderCap);
        g.fillRoundedRectangle (cap, size::radiusXs);
        g.setColour (theme().lcdLabel);
        g.fillRect (cap.removeFromBottom (2.0f).reduced (3.0f, 0.0f));
    }
    else
    {
        // Track header volume: thin track, filled in trackColourId, round thumb.
        // JUCE hands us the thumb's travel, which is inset by the thumb's
        // radius at each end; the track reaches past it to the slider's
        // edges, as far as the thumb itself goes.
        auto track = bounds.expanded ((float) getSliderThumbRadius (s), 0.0f);
        track = track.withSizeKeepingCentre (track.getWidth(), 4.0f);
        g.setColour (theme().sliderTrack);
        g.fillRoundedRectangle (track, size::radiusHair);

        g.setColour (trackColour);
        g.fillRoundedRectangle (track.withRight (sliderPos), size::radiusHair);

        // The thumb, with a border and soft shadow in light mode (both
        // transparent in dark).
        auto thumb = juce::Rectangle<float> (12.0f, 12.0f).withCentre ({ sliderPos, track.getCentreY() });
        juce::Path disc;
        disc.addEllipse (thumb);
        juce::DropShadow (theme().thumbShadow, 3, { 0, 1 }).drawForPath (g, disc);
        g.setColour (automated ? theme().automated : theme().sliderThumb);
        g.fillEllipse (thumb);
        g.setColour (theme().thumbBorder);
        g.drawEllipse (thumb.reduced (0.5f), 1.0f);
    }

    if (s.hasKeyboardFocus (false))
        drawFocusRing (g, bounds.expanded (2.0f), size::radiusXs);
}

// A pedal's knob (see PedalView): a dark knob with a white pointer and the
// value arc in the pedal's lettering colour (the slider's knobArcColourId).
// Pedals look the same in both themes, so this uses fixed colours, not the
// theme's. The Amp's knobs (see AmpView) are drawn here too, with two more
// slider properties: "knobCap" ("chrome" or "cream" for a light cap with a
// dark pointer; anything else is the dark cap) and "capSize" (the cap's
// diameter in pixels).
static void drawPedalKnob (juce::Graphics& g, juce::Rectangle<float> area, float pos,
                           float startAngle, float endAngle, juce::Slider& s)
{
    const auto size = juce::jmin (area.getWidth(), area.getHeight());
    const auto centre = area.getCentre();
    const auto arcWidth = juce::jmax (2.0f, size * 0.09f);
    const auto radius = (size - arcWidth) * 0.5f - 0.5f;
    const auto valueAngle = startAngle + pos * (endAngle - startAngle);
    const auto arcColour = s.findColour (IkarosLookAndFeel::knobArcColourId);
    const juce::PathStrokeType stroke (arcWidth, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);

    juce::Path ring;
    ring.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, startAngle, endAngle, true);
    g.setColour (arcColour.withAlpha (0.22f));
    g.strokePath (ring, stroke);

    if (valueAngle - startAngle > 0.02f)
    {
        juce::Path value;
        value.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, startAngle, valueAngle, true);
        g.setColour (arcColour);
        g.strokePath (value, stroke);
    }

    // The knob: a small shadow under it, then a body lit from the top left.
    const auto& properties = s.getProperties();
    const auto bodyRadius = properties.contains ("capSize") ? (float) properties["capSize"] * 0.5f
                                                            : radius - arcWidth * 0.5f - 2.0f;
    const auto body = juce::Rectangle<float> (bodyRadius * 2.0f, bodyRadius * 2.0f).withCentre (centre);
    juce::Path bodyPath;
    bodyPath.addEllipse (body);
    juce::DropShadow (juce::Colours::black.withAlpha (0.4f), 4, { 0, 2 }).drawForPath (g, bodyPath);

    // The cap's colours: lit spot, middle, edge.
    const auto cap = properties["knobCap"].toString();
    const bool lightCap = cap == "chrome" || cap == "cream";
    const auto [lit, middle, edge] = cap == "chrome" ? std::make_tuple (juce::Colour (0xfffbfcfd), juce::Colour (0xffc7ccd3), juce::Colour (0xff7c838d))
                                   : cap == "cream"  ? std::make_tuple (juce::Colour (0xfffbf4e3), juce::Colour (0xffeadcbc), juce::Colour (0xffb7a17a))
                                                     : std::make_tuple (juce::Colour (0xff42454c), juce::Colour (0xff2a2c31), juce::Colour (0xff121316));

    juce::ColourGradient shading (lit, body.getX() + body.getWidth() * 0.3f, body.getY() + (lightCap ? body.getHeight() * 0.25f : 0.0f),
                                  edge, body.getCentreX(), body.getBottom(), true);
    shading.addColour (0.5, middle);
    g.setGradientFill (shading);
    g.fillEllipse (body);
    g.setColour (juce::Colours::black.withAlpha (lightCap ? 0.35f : 0.6f));
    g.drawEllipse (body, 1.0f);

    g.setColour (lightCap ? juce::Colour (0xff1b1c1f) : juce::Colours::white);
    g.drawLine ({ centre.getPointOnCircumference (bodyRadius * 0.3f, valueAngle),
                  centre.getPointOnCircumference (bodyRadius * 0.85f, valueAngle) }, juce::jmax (1.8f, size * 0.07f));

    if (s.hasKeyboardFocus (false))
        IkarosLookAndFeel::drawFocusRing (g, juce::Rectangle<float> (size, size).withCentre (centre).expanded (2.0f), size * 0.5f);
}

void IkarosLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h,
                                          float pos, float startAngle, float endAngle, juce::Slider& s)
{
    // A pedal's knob, on the pedalboard. Set with
    // slider.getProperties().set ("pedal", true).
    if ((bool) s.getProperties()["pedal"])
    {
        drawPedalKnob (g, juce::Rectangle<int> (x, y, w, h).toFloat(), pos, startAngle, endAngle, s);
        return;
    }

    // A small knob (e.g. a track header's pan knob) has its own proportions,
    // so its ring and needle stay visible at that size. Set with
    // slider.getProperties().set ("compact", true).
    if ((bool) s.getProperties()["compact"])
    {
        drawCompactKnob (g, juce::Rectangle<int> (x, y, w, h).toFloat(), pos, startAngle, endAngle, s);
        return;
    }

    // Arc-style knob: grey track arc, coloured value arc, round cap and a needle.
    auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (4.0f);
    auto size = juce::jmin (bounds.getWidth(), bounds.getHeight());
    auto centre = bounds.getCentre();
    auto radius = size * 0.5f - 3.0f;
    auto arcWidth = size * 0.09f;
    auto valueAngle = startAngle + pos * (endAngle - startAngle);

    juce::Path track;
    track.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, startAngle, endAngle, true);
    g.setColour (theme().knobTrack);
    g.strokePath (track, { arcWidth, juce::PathStrokeType::curved, juce::PathStrokeType::rounded });

    // A "bipolar" knob (e.g. pan) draws its arc out from 12 o'clock instead of
    // from the start. Set with slider.getProperties().set ("bipolar", true).
    const bool bipolar = s.getProperties()["bipolar"];
    const auto arcStart = bipolar ? (startAngle + endAngle) * 0.5f : startAngle;

    // A bipolar knob gets a small tick at 12 o'clock, just outside the arc,
    // marking the centre. Exactly in the middle there's no arc at all: the
    // needle simply points at the tick.
    if (bipolar)
    {
        const auto top = (startAngle + endAngle) * 0.5f;
        g.setColour (theme().textSecondary);
        g.drawLine ({ centre.getPointOnCircumference (radius + arcWidth * 0.9f, top),
                      centre.getPointOnCircumference (radius + arcWidth * 2.2f, top) },
                    juce::jmax (1.0f, arcWidth * 0.5f));
    }

    const bool atMiddle = bipolar && std::abs (pos - 0.5f) < 0.001f;

    if (! atMiddle)
    {
        juce::Path value;
        value.addCentredArc (centre.x, centre.y, radius, radius, 0.0f,
                             juce::jmin (arcStart, valueAngle), juce::jmax (arcStart + 0.001f, valueAngle), true);
        g.setColour (valueColourFor (s, knobArcColourId));
        g.strokePath (value, { arcWidth, juce::PathStrokeType::curved, juce::PathStrokeType::rounded });
    }

    auto capRadius = radius * 0.68f;
    const auto cap = juce::Rectangle<float> (capRadius * 2.0f, capRadius * 2.0f).withCentre (centre);
    g.setColour (theme().knobBody);
    g.fillEllipse (cap);
    g.setColour (theme().knobRim);
    g.drawEllipse (cap, 1.0f);

    // getPointOnCircumference measures angles clockwise from 12 o'clock, like the slider does.
    auto inner = centre.getPointOnCircumference (capRadius * 0.27f, valueAngle);
    auto outer = centre.getPointOnCircumference (capRadius * 0.8f, valueAngle);
    // An automated knob's needle is in the "automated" colour (see drawLinearSlider).
    g.setColour ((bool) s.getProperties()["automated"] ? theme().automated : theme().knobPointer);
    g.drawLine ({ inner, outer }, size * 0.055f);

    if (s.hasKeyboardFocus (false))
        drawFocusRing (g, juce::Rectangle<float> (size, size).withCentre (centre).expanded (3.0f), size * 0.5f);
}

void IkarosLookAndFeel::drawCompactKnob (juce::Graphics& g, juce::Rectangle<float> area, float pos,
                                         float startAngle, float endAngle, juce::Slider& s)
{
    // The Smart Controls knob, scaled down: a grey ring with the value arc
    // along it, a dark cap inside and a white needle.
    const auto size = juce::jmin (area.getWidth(), area.getHeight());
    const auto centre = area.getCentre();
    const auto arcWidth = juce::jmax (2.0f, size * 0.11f);
    const auto radius = (size - arcWidth) * 0.5f - 0.5f;   // the ring's centre line, inside the bounds
    const auto valueAngle = startAngle + pos * (endAngle - startAngle);
    const juce::PathStrokeType stroke (arcWidth, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);

    // The ring is a shade lighter than the big knobs' so it still shows on
    // a selected track's lighter background.
    juce::Path ring;
    ring.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, startAngle, endAngle, true);
    g.setColour (theme().compactKnobTrack);
    g.strokePath (ring, stroke);

    // Bipolar (pan): the arc grows out from 12 o'clock; at the centre there's none.
    const bool bipolar = s.getProperties()["bipolar"];
    const auto arcStart = bipolar ? (startAngle + endAngle) * 0.5f : startAngle;

    if (std::abs (valueAngle - arcStart) > 0.02f)
    {
        juce::Path value;
        value.addCentredArc (centre.x, centre.y, radius, radius, 0.0f,
                             juce::jmin (arcStart, valueAngle), juce::jmax (arcStart, valueAngle), true);
        g.setColour (valueColourFor (s, knobArcColourId));
        g.strokePath (value, stroke);
    }

    // The cap sits just inside the ring, with a small gap.
    const auto capRadius = radius - arcWidth * 0.5f - juce::jmax (1.5f, size * 0.07f);
    const auto cap = juce::Rectangle<float> (capRadius * 2.0f, capRadius * 2.0f).withCentre (centre);
    g.setColour (theme().compactKnobBody);
    g.fillEllipse (cap);
    g.setColour (theme().compactKnobRim);
    g.drawEllipse (cap, 1.0f);

    g.setColour ((bool) s.getProperties()["automated"] ? theme().automated : theme().compactKnobPointer);
    g.drawLine ({ centre.getPointOnCircumference (capRadius * 0.2f, valueAngle),
                  centre.getPointOnCircumference (capRadius * 0.75f, valueAngle) }, juce::jmax (1.6f, size * 0.07f));

    if (s.hasKeyboardFocus (false))
        drawFocusRing (g, juce::Rectangle<float> (size, size).withCentre (centre).expanded (2.0f), size * 0.5f);
}

int IkarosLookAndFeel::getSliderThumbRadius (juce::Slider& s)
{
    // How far the thumb's centre stays from a slider's ends. Our horizontal
    // thumbs are 12px round, so 6 lets them reach the slider's edges, and
    // the slider's bounds are where it visibly starts and ends (JUCE's
    // default is up to 12, which leaves a gap). Vertical faders keep it.
    return s.isHorizontal() ? 6 : juce::LookAndFeel_V4::getSliderThumbRadius (s);
}

juce::Font IkarosLookAndFeel::getSliderPopupFont (juce::Slider&)
{
    return fonts::mono (textSize::xs);
}

//==============================================================================
void IkarosLookAndFeel::drawMenuBarBackground (juce::Graphics& g, int w, int h, bool, juce::MenuBarComponent&)
{
    g.setColour (theme().titleBarBg);
    g.fillRect (0, 0, w, h);
}

void IkarosLookAndFeel::drawMenuBarItem (juce::Graphics& g, int w, int h, int, const juce::String& text,
                                         bool isMouseOverItem, bool isMenuOpen, bool, juce::MenuBarComponent&)
{
    auto r = juce::Rectangle<float> (0.0f, 2.0f, (float) w, (float) h - 4.0f);

    if (isMenuOpen || isMouseOverItem)
    {
        g.setColour (theme().raised);
        g.fillRoundedRectangle (r, size::radiusSmall);
    }

    g.setColour (isMenuOpen ? theme().text : theme().textMenu);
    g.setFont (fonts::ui (textSize::md));
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
    g.setColour (theme().titleBarBg);
    g.fillRect (r.removeFromTop (border.getTop()));
    g.fillRect (r.removeFromBottom (border.getBottom()));
    g.fillRect (r.removeFromLeft (border.getLeft()));
    g.fillRect (r.removeFromRight (border.getRight()));

    g.setColour (theme().border);
    g.drawRect (0, 0, w, h, 1);
}

//==============================================================================
void IkarosLookAndFeel::drawCallOutBoxBackground (juce::CallOutBox&, juce::Graphics& g,
                                                  const juce::Path& path, juce::Image&)
{
    // A soft shadow, then the popover body in the toolbar colour with a border.
    theme().popoverShadow.drawForPath (g, path);

    g.setColour (theme().popupBg);
    g.fillPath (path);

    g.setColour (theme().border);
    g.strokePath (path, juce::PathStrokeType (1.0f));
}

float IkarosLookAndFeel::getCallOutBoxCornerSize (const juce::CallOutBox&)
{
    return size::radiusLarge;
}

//==============================================================================
void IkarosLookAndFeel::fillTextEditorBackground (juce::Graphics& g, int w, int h, juce::TextEditor& e)
{
    const bool inlineField = e.getProperties()["inline"];   // see styleInlineEditor
    g.setColour (inlineField ? theme().raised : e.findColour (juce::TextEditor::backgroundColourId));
    g.fillRoundedRectangle (juce::Rectangle<float> ((float) w, (float) h), size::radius);
}

void IkarosLookAndFeel::drawTextEditorOutline (juce::Graphics& g, int w, int h, juce::TextEditor& e)
{
    auto r = juce::Rectangle<float> ((float) w, (float) h).reduced (0.5f);
    const bool focused = e.hasKeyboardFocus (true) && ! e.isReadOnly();

    if ((bool) e.getProperties()["inline"])   // see styleInlineEditor
        g.setColour (theme().selectedBorder);
    else
        g.setColour (e.findColour (focused ? juce::TextEditor::focusedOutlineColourId
                                           : juce::TextEditor::outlineColourId));
    g.drawRoundedRectangle (r, size::radius, focused ? 2.0f : 1.0f);
}

//==============================================================================
int IkarosLookAndFeel::getDefaultScrollbarWidth()
{
    return size::scrollbarWidth;
}

void IkarosLookAndFeel::drawScrollbar (juce::Graphics& g, juce::ScrollBar& bar, int x, int y, int w, int h,
                                       bool vertical, int thumbStart, int thumbSize, bool over, bool down)
{
    auto thumb = vertical ? juce::Rectangle<int> (x, thumbStart, w, thumbSize)
                          : juce::Rectangle<int> (thumbStart, y, thumbSize, h);

    const bool onArrangement = bar.getProperties()["arrangement"];   // see ArrangementView
    const auto colour = onArrangement ? theme().arrangement.scrollbar : theme().borderStrong;
    g.setColour (colour.brighter (down ? 0.3f : (over ? 0.15f : 0.0f)));
    g.fillRoundedRectangle (thumb.toFloat().reduced (2.5f), size::radiusTiny);
}

} // namespace ikaros
