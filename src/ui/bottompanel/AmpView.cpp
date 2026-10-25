#include "ui/bottompanel/AmpView.h"
#include "ui/widgets/PaintProbe.h"
#include "engine/TrackController.h"
#include "ui/theme/Fonts.h"
#include "ui/theme/IkarosLookAndFeel.h"
#include "ui/theme/Theme.h"
#include "ui/theme/Tokens.h"
#include "ui/widgets/Icons.h"

namespace ikaros
{

using namespace tokens;

namespace
{
    // The layout, in pixels from the Amp's top left (it's always
    // AmpView::width x AmpView::height).
    constexpr int inset = 10;
    constexpr int topRowHeight = 24;
    constexpr int faceplateTop = inset + topRowHeight + 6, faceplateHeight = 66;
    constexpr int grilleTop = faceplateTop + faceplateHeight + 6;   // to inset from the bottom (~94 px)
    constexpr int knobSize = 38, capSize = 30, knobTopInFaceplate = 6;
    constexpr int labelHeight = 14;
    constexpr int speakerSize = 84;
    constexpr int pillHeight = 24, pillGap = 6;
    constexpr float cornerRadius = 10.0f, panelRadius = 6.0f;

    // The pills' text, light on their dark translucent backgrounds (on
    // every voice's covering).
    const juce::Colour pillText (0xffe6e8eb);

    juce::Rectangle<int> faceplateArea()    { return { inset, faceplateTop, AmpView::width - inset * 2, faceplateHeight }; }
    juce::Rectangle<int> grilleArea()       { return { inset, grilleTop, AmpView::width - inset * 2, AmpView::height - inset - grilleTop }; }

    juce::Font pillFont()                   { return fonts::uiSemiBold (textSize::md); }

    juce::String chevron()                  { return juce::String::fromUTF8 ("\xe2\x96\xbe"); }   // a small down triangle

    // One speaker, `size` across, centred on `centre`: a metal frame with
    // four bolts, the rolled surround lit from the top left, the paper cone
    // (faint radial ribs, darker towards the middle) and the domed dust cap.
    void drawSpeaker (juce::Graphics& g, juce::Point<float> centre, float size)
    {
        const auto r = size * 0.5f;
        auto circle = [centre] (float radius)    { return juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (centre); };

        // The frame, and its bolts on the diagonals.
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff6a6e75), centre.x - r, centre.y - r,
                                                 juce::Colour (0xff1f2125), centre.x + r, centre.y + r, false));
        g.fillEllipse (circle (r));
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.drawEllipse (circle (r).reduced (0.5f), 1.0f);

        for (int i = 0; i < 4; ++i)
        {
            const auto angle = juce::MathConstants<float>::pi * (0.25f + 0.5f * (float) i);
            const auto bolt = centre.getPointOnCircumference (r * 0.88f, angle);
            g.setColour (juce::Colour (0xffb9bec6));
            g.fillEllipse (juce::Rectangle<float> (3.6f, 3.6f).withCentre (bolt));
            g.setColour (juce::Colours::black.withAlpha (0.5f));
            g.drawEllipse (juce::Rectangle<float> (3.6f, 3.6f).withCentre (bolt), 0.6f);
        }

        // The surround: a ring lit from the top left.
        const auto surroundOuter = r * 0.78f, surroundInner = r * 0.64f;
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff4a4d53), centre.x - surroundOuter, centre.y - surroundOuter,
                                                 juce::Colour (0xff0d0e10), centre.x + surroundOuter, centre.y + surroundOuter, false));
        g.fillEllipse (circle (surroundOuter));

        // The cone: darker towards the middle.
        juce::ColourGradient cone (juce::Colour (0xff121315), centre.x, centre.y,
                                   juce::Colour (0xff3a3c41), centre.x + surroundInner, centre.y, true);
        g.setGradientFill (cone);
        g.fillEllipse (circle (surroundInner));

        g.setColour (juce::Colours::white.withAlpha (0.05f));

        for (int i = 0; i < 24; ++i)
        {
            const auto angle = juce::MathConstants<float>::twoPi * (float) i / 24.0f;
            g.drawLine ({ centre.getPointOnCircumference (r * 0.26f, angle), centre.getPointOnCircumference (surroundInner, angle) }, 0.7f);
        }

        // The dust cap: a dome, with a highlight towards the top left.
        const auto capRadius = r * 0.24f;
        juce::ColourGradient dome (juce::Colour (0xff55585e), centre.x - capRadius * 0.4f, centre.y - capRadius * 0.4f,
                                   juce::Colour (0xff141517), centre.x + capRadius, centre.y + capRadius, true);
        g.setGradientFill (dome);
        g.fillEllipse (circle (capRadius));
        g.setColour (juce::Colours::white.withAlpha (0.25f));
        g.fillEllipse (juce::Rectangle<float> (capRadius * 0.7f, capRadius * 0.45f)
                           .withCentre (centre.translated (-capRadius * 0.3f, -capRadius * 0.45f)));
    }

    // The grille cloth's weave over `area`, in light and dark threads.
    void drawWeave (juce::Graphics& g, juce::Rectangle<float> area, AmpGrille weave)
    {
        const auto light = juce::Colours::white, dark = juce::Colours::black;

        switch (weave)
        {
            case AmpGrille::plain:
                // A fine grid of threads.
                for (float x = area.getX(); x < area.getRight(); x += 3.0f)
                {
                    g.setColour (light.withAlpha (0.06f));
                    g.fillRect (x, area.getY(), 1.0f, area.getHeight());
                }

                for (float y = area.getY(); y < area.getBottom(); y += 3.0f)
                {
                    g.setColour (dark.withAlpha (0.18f));
                    g.fillRect (area.getX(), y, area.getWidth(), 1.0f);
                }
                break;

            case AmpGrille::diamond:
            {
                // Diagonal threads both ways, 12 px apart: a diamond lattice.
                juce::Path lattice;
                const auto span = area.getWidth() + area.getHeight();

                for (float d = -area.getHeight(); d < span; d += 12.0f)
                {
                    lattice.startNewSubPath (area.getX() + d, area.getY());
                    lattice.lineTo (area.getX() + d + area.getHeight(), area.getBottom());
                    lattice.startNewSubPath (area.getX() + d + area.getHeight(), area.getY());
                    lattice.lineTo (area.getX() + d, area.getBottom());
                }

                g.setColour (juce::Colour (0xffd9b48a).withAlpha (0.28f));
                g.strokePath (lattice, juce::PathStrokeType (1.0f));
                break;
            }

            case AmpGrille::basketweave:
                // Little blocks of threads, alternately across and down.
                for (float y = area.getY(), row = 0; y < area.getBottom(); y += 4.0f, ++row)
                {
                    for (float x = area.getX(), column = 0; x < area.getRight(); x += 4.0f, ++column)
                    {
                        const bool across = ((int) row + (int) column) % 2 == 0;
                        g.setColour (light.withAlpha (0.07f));
                        g.fillRect (across ? juce::Rectangle<float> (x, y + 1.0f, 3.5f, 1.0f)
                                           : juce::Rectangle<float> (x + 1.0f, y, 1.0f, 3.5f));
                        g.setColour (dark.withAlpha (0.2f));
                        g.fillRect (across ? juce::Rectangle<float> (x, y + 2.5f, 3.5f, 1.0f)
                                           : juce::Rectangle<float> (x + 2.5f, y, 1.0f, 3.5f));
                    }
                }
                break;

            case AmpGrille::perforated:
                // A metal sheet's holes, in staggered rows.
                for (float y = area.getY() + 3.0f, row = 0; y < area.getBottom(); y += 6.0f, ++row)
                {
                    for (float x = area.getX() + ((int) row % 2 == 0 ? 3.0f : 6.0f); x < area.getRight(); x += 6.0f)
                    {
                        g.setColour (dark.withAlpha (0.45f));
                        g.fillEllipse (x - 1.5f, y - 1.5f, 3.0f, 3.0f);
                        g.setColour (light.withAlpha (0.06f));
                        g.drawEllipse (x - 1.5f, y - 1.0f, 3.0f, 3.0f, 0.5f);
                    }
                }
                break;
        }
    }
}

//==============================================================================
// The power switch: a round button with the power symbol. On, its ring and
// symbol are lit in the accent colour (with a soft glow); bypassed, they're
// grey. A real button, so Tab reaches it and Space presses it.
class AmpView::PowerButton  : public juce::Button
{
public:
    explicit PowerButton (AmpView& a) : juce::Button ("Power"), amp (a)
    {
        setMouseClickGrabsKeyboardFocus (false);
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    void paintButton (juce::Graphics& g, bool highlighted, bool down) override
    {
        const auto r = getLocalBounds().toFloat().reduced (1.5f);
        const bool on = amp.settings.on;

        if (on)
        {
            g.setColour (theme().accent.withAlpha (0.22f));
            g.fillEllipse (r.expanded (1.0f));
        }

        g.setColour (juce::Colours::black.withAlpha (down ? 0.6f : highlighted ? 0.4f : 0.5f));
        g.fillEllipse (r.reduced (1.5f));
        g.setColour (on ? theme().accent : juce::Colours::white.withAlpha (highlighted ? 0.4f : 0.22f));
        g.drawEllipse (r.reduced (1.5f), 1.5f);

        const auto symbol = on ? theme().accent : pillText.withAlpha (highlighted ? 0.8f : 0.55f);
        icons::draw (g, Icon::power, r.reduced (5.0f), symbol, r.getWidth() - 10.0f, 2.0f);

        if (hasKeyboardFocus (false))
            IkarosLookAndFeel::drawFocusRing (g, getLocalBounds().toFloat(), getHeight() * 0.5f);
    }

private:
    AmpView& amp;
};

//==============================================================================
// Bright, Voice and Cab: a dark translucent pill with text, e.g.
// "Voice  Crunch v". `caption` is drawn dimmer before the value; a lit pill
// (Bright, on) has an accent border and text.
class AmpView::PillButton  : public juce::Button
{
public:
    PillButton (const juce::String& name, float backgroundAlpha) : juce::Button (name), alpha (backgroundAlpha)
    {
        setMouseClickGrabsKeyboardFocus (false);
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    void setText (const juce::String& newCaption, const juce::String& newValue, bool menu, bool isLit)
    {
        if (newCaption == caption && newValue == value && menu == hasMenu && isLit == lit)
            return;

        caption = newCaption;
        value = newValue;
        hasMenu = menu;
        lit = isLit;
        repaint();
    }

    /** The width the pill needs for its text. */
    int getIdealWidth() const
    {
        const auto font = pillFont();
        auto textWidth = [&font] (const juce::String& s)    { return (int) std::ceil (juce::GlyphArrangement::getStringWidth (font, s)); };
        int w = 12 + textWidth (value) + 12;

        if (caption.isNotEmpty())
            w += textWidth (caption) + 8;

        if (hasMenu)
            w += 14;

        return w;
    }

    void paintButton (juce::Graphics& g, bool highlighted, bool down) override
    {
        const auto r = getLocalBounds().toFloat().reduced (0.5f);
        const auto radius = size::radiusXs + 2.0f;

        g.setColour (juce::Colours::black.withAlpha (alpha + (down ? 0.15f : highlighted ? 0.08f : 0.0f)));
        g.fillRoundedRectangle (r, radius);
        g.setColour (lit ? theme().accent : juce::Colours::white.withAlpha (0.12f));
        g.drawRoundedRectangle (r, radius, lit ? 1.5f : 1.0f);

        auto text = getLocalBounds().reduced (12, 0);
        g.setFont (pillFont());

        if (hasMenu)
        {
            g.setColour (pillText.withAlpha (0.8f));
            g.drawText (chevron(), text.removeFromRight (10), juce::Justification::centred);
            text.removeFromRight (4);
        }

        if (caption.isNotEmpty())
        {
            const auto captionWidth = (int) std::ceil (juce::GlyphArrangement::getStringWidth (pillFont(), caption));
            g.setColour (pillText.withAlpha (0.6f));
            g.drawText (caption, text.removeFromLeft (captionWidth), juce::Justification::centredLeft);
            text.removeFromLeft (8);
        }

        g.setColour (lit ? theme().accent : pillText);
        g.drawFittedText (value, text, juce::Justification::centredLeft, 1, 0.8f);

        if (hasKeyboardFocus (false))
            IkarosLookAndFeel::drawFocusRing (g, getLocalBounds().toFloat(), radius);
    }

private:
    juce::String caption, value;
    bool hasMenu = false, lit = false;
    const float alpha;
};

//==============================================================================
AmpView::AmpView (AudioEngine& e) : engine (e)
{
    setTitle ("Amp");
    setWantsKeyboardFocus (true);   // so the keyboard can remove it and open its menu
    setMouseClickGrabsKeyboardFocus (false);

    for (int i = 0; i < ampKnobCount; ++i)
    {
        auto* knob = knobs.add (new juce::Slider (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox));
        knob->getProperties().set ("pedal", true);          // drawn by IkarosLookAndFeel's drawPedalKnob...
        knob->getProperties().set ("capSize", capSize);     // ...with a bigger cap than a pedal's
        knob->setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
        knob->setRange (0.0, 1.0);
        knob->setMouseDragSensitivity (180);
        knob->setWantsKeyboardFocus (true);   // with it, the arrow keys turn it
        knob->setMouseClickGrabsKeyboardFocus (false);
        knob->setTitle (ampKnobLabels[(size_t) i]);
        knob->textFromValueFunction = [this, i] (double) { return settings.knobText[(size_t) i]; };

        // A mouse drag is one undo step (started when it begins). A change
        // that isn't a drag (an arrow key, the mouse wheel) gets a step of
        // its own (see knobChanged).
        knob->onDragStart = [this, i]
        {
            dragging[(size_t) i] = true;
            engine.beginUndoStep (juce::String ("Amp ") + ampKnobLabels[(size_t) i]);
        };
        knob->onDragEnd = [this, i] { dragging[(size_t) i] = false; };
        knob->onValueChange = [this, i] { knobChanged (i); };
        addAndMakeVisible (knob);
    }

    power = std::make_unique<PowerButton> (*this);
    power->onClick = [this] { engine.getTracks().effects().setEffectEnabled (trackId, settings.pluginId, ! settings.on); };
    addAndMakeVisible (*power);

    brightButton = std::make_unique<PillButton> ("Bright", 0.3f);
    brightButton->onClick = [this] { engine.getTracks().effects().setAmpBright (trackId, ! settings.bright); };
    addAndMakeVisible (*brightButton);

    voiceButton = std::make_unique<PillButton> ("Voice", 0.3f);
    voiceButton->onClick = [this] { showVoiceMenu(); };
    addAndMakeVisible (*voiceButton);

    cabButton = std::make_unique<PillButton> ("Cab", 0.55f);
    cabButton->onClick = [this] { showCabMenu(); };
    addAndMakeVisible (*cabButton);

    styleKnobs();
}

AmpView::~AmpView() = default;

const AmpVoiceStyle& AmpView::style() const
{
    auto& voices = getAmpVoices();
    return voices[(size_t) juce::jlimit (0, (int) voices.size() - 1, settings.voice)];
}

void AmpView::styleKnobs()
{
    // The knobs' caps and value arcs follow the voice.
    const auto& s = style();
    const char* cap = s.knobCap == AmpKnobCap::chrome ? "chrome" : s.knobCap == AmpKnobCap::cream ? "cream" : "black";

    for (auto* knob : knobs)
    {
        knob->getProperties().set ("knobCap", cap);
        knob->setColour (IkarosLookAndFeel::knobArcColourId, s.lettering);
        knob->repaint();
    }
}

void AmpView::update (TrackId track, const EffectsController::AmpSettings& newSettings)
{
    const bool lookChanged = newSettings.voice != settings.voice || newSettings.cab != settings.cab;
    const bool onChanged = newSettings.on != settings.on;
    trackId = track;
    settings = newSettings;

    if (lookChanged)
    {
        styleKnobs();
        repaint();
    }
    else if (onChanged)
    {
        repaint();
    }

    for (int i = 0; i < knobs.size(); ++i)
    {
        knobs[i]->setTooltip (juce::String (ampKnobLabels[(size_t) i]) + ": " + settings.knobText[(size_t) i]);

        // Leave a knob the mouse is turning alone, so it doesn't jitter.
        if (! dragging[(size_t) i])
            knobs[i]->setValue (settings.knobs[(size_t) i], juce::dontSendNotification);
    }

    const auto& s = style();
    brightButton->setText ({}, "Bright", false, settings.bright);
    brightButton->setTooltip (settings.bright ? "Bright is on: more sparkle before the preamp" : "Turn on Bright: more sparkle before the preamp");
    voiceButton->setText ("Voice", s.name, true, false);
    voiceButton->setTooltip ("Pick the amp's voice (it sets the cab and knobs too)");
    cabButton->setText ("Cab", getAmpCabName (settings.cab), true, false);
    cabButton->setTooltip ("Pick the speaker cabinet, or Off to use your own");
    resized();   // the pills' widths follow their text (setBounds does nothing if they haven't changed)
    power->setTitle (settings.on ? "Amp, on" : "Amp, bypassed");
    power->setTooltip (settings.on ? "Turn the amp off (bypass)" : "Turn the amp on");
    setDescription (s.name + " amp, " + getAmpCabName (settings.cab) + " cab");
}

void AmpView::knobChanged (int index)
{
    if (! dragging[(size_t) index])
        engine.beginUndoStep (juce::String ("Amp ") + ampKnobLabels[(size_t) index]);

    engine.getTracks().effects().setAmpKnob (trackId, index, (float) knobs[index]->getValue());
    // The new value text arrives with the next update(), when the engine
    // reports the change (see PedalboardView::refresh).
}

//==============================================================================
void AmpView::showVoiceMenu()
{
    // The menu's items run their lambdas after it closes; they capture ids
    // by value and reach the engine through a reference that outlives us.
    auto& effects = engine.getTracks().effects();
    const auto track = trackId;
    juce::PopupMenu menu;
    auto& voices = getAmpVoices();

    for (int i = 0; i < (int) voices.size(); ++i)
        menu.addItem (voices[(size_t) i].name, true, i == settings.voice, [&effects, track, i] { effects.setAmpVoice (track, i); });

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (voiceButton.get()));
}

void AmpView::showCabMenu()
{
    auto& effects = engine.getTracks().effects();
    const auto track = trackId;
    juce::PopupMenu menu;
    const auto off = (int) getAmpCabs().size();

    for (int i = 0; i <= off; ++i)
    {
        if (i == off)
            menu.addSeparator();

        menu.addItem (getAmpCabName (i), true, i == settings.cab, [&effects, track, i] { effects.setAmpCab (track, i); });
    }

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (cabButton.get()));
}

void AmpView::showMenu()
{
    // The item runs after the menu closes, when we may be gone: it captures
    // ids by value, not `this`.
    auto& effects = engine.getTracks().effects();
    const auto track = trackId;
    const auto id = settings.pluginId;
    juce::PopupMenu menu;
    menu.addItem ("Remove", [&effects, track, id] { effects.removeEffect (track, id); });

    // Right-clicked: the menu opens where the mouse is. From the keyboard
    // (Shift+F10): beside the Amp.
    auto options = juce::PopupMenu::Options();
    menu.showMenuAsync (isMouseOver (true) ? options.withMousePosition() : options.withTargetComponent (this));
}

void AmpView::remove()
{
    // This may delete us (the pedalboard refreshes when the engine reports
    // the change), so nothing may touch members after it.
    engine.getTracks().effects().removeEffect (trackId, settings.pluginId);
}

void AmpView::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
        showMenu();
}

bool AmpView::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress (juce::KeyPress::F10Key, juce::ModifierKeys::shiftModifier, 0))
    {
        showMenu();
        return true;
    }

    if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)
    {
        remove();
        return true;
    }

    return false;
}

//==============================================================================
void AmpView::drawShadow (juce::Graphics& g, const AmpView& amp, CachedShadow& cache)
{
    cache.drawForRoundedRectangle (g, juce::DropShadow (juce::Colours::black.withAlpha (0.38f), 14, { 0, 6 }),
                                   amp.getBounds().toFloat(), cornerRadius);
}

void AmpView::drawBackground (juce::Graphics& g) const
{
    const auto& s = style();
    const auto bounds = getLocalBounds().toFloat();

    // The covering: a fine diagonal two-tone texture, clipped to the
    // cabinet's rounded corners.
    {
        juce::Graphics::ScopedSaveState state (g);
        juce::Path body;
        body.addRoundedRectangle (bounds, cornerRadius);
        g.reduceClipRegion (body);
        g.setColour (s.tolex);
        g.fillAll();
        g.setColour (s.tolex2);

        for (float d = -bounds.getHeight(); d < bounds.getWidth(); d += 4.0f)
            g.drawLine (d, bounds.getBottom(), d + bounds.getHeight(), 0.0f, 1.5f);

        // Lighter at the top (the light comes from above).
        g.setGradientFill (juce::ColourGradient::vertical (juce::Colours::white.withAlpha (0.06f), 0.0f,
                                                          juce::Colours::black.withAlpha (0.12f), bounds.getHeight()));
        g.fillAll();
    }

    g.setColour (juce::Colours::black.withAlpha (0.45f));
    g.drawRoundedRectangle (bounds.reduced (0.5f), cornerRadius, 1.0f);

    // The piping: a thin line inset from the edge.
    g.setColour (s.piping.withAlpha (0.85f));
    g.drawRoundedRectangle (bounds.reduced (4.0f), cornerRadius - 3.0f, 1.5f);

    // The faceplate, a little lighter at the top.
    const auto plate = faceplateArea().toFloat();
    g.setGradientFill (juce::ColourGradient::vertical (s.faceplate.brighter (0.1f), plate.getY(), s.faceplate.darker (0.08f), plate.getBottom()));
    g.fillRoundedRectangle (plate, panelRadius);
    g.setColour (juce::Colours::black.withAlpha (0.3f));
    g.drawRoundedRectangle (plate.reduced (0.5f), panelRadius, 1.0f);
    g.setColour (juce::Colours::white.withAlpha (0.12f));
    g.drawLine (plate.getX() + panelRadius, plate.getY() + 1.0f, plate.getRight() - panelRadius, plate.getY() + 1.0f, 1.0f);

    // The knobs' labels.
    g.setColour (s.lettering);
    g.setFont (fonts::uiSemiBold (textSize::xs).withExtraKerningFactor (0.06f));
    const auto column = plate.getWidth() / (float) ampKnobCount;

    for (int i = 0; i < ampKnobCount; ++i)
        g.drawFittedText (juce::String (ampKnobLabels[(size_t) i]).toUpperCase(),
                          juce::Rectangle<float> (plate.getX() + column * (float) i, plate.getY() + knobTopInFaceplate + knobSize + 3.0f,
                                                  column, (float) labelHeight).toNearestInt(),
                          juce::Justification::centred, 1, 0.8f);

    // The grille: the cabinet's dark inside, the speakers in front of it,
    // then the cloth over them (a translucent tint and its weave), so the
    // speakers show through.
    const auto grille = grilleArea().toFloat();
    juce::Graphics::ScopedSaveState state (g);
    juce::Path grillePath;
    grillePath.addRoundedRectangle (grille, panelRadius);
    g.reduceClipRegion (grillePath);
    g.setColour (juce::Colour (0xff0b0c0e));
    g.fillAll();

    const auto& cabs = getAmpCabs();
    const int speakers = juce::isPositiveAndBelow (settings.cab, (int) cabs.size()) ? cabs[(size_t) settings.cab].speakers : 0;

    for (int i = 0; i < speakers; ++i)
        drawSpeaker (g, { grille.getX() + grille.getWidth() * ((float) i + 0.5f) / (float) speakers, grille.getCentreY() },
                     (float) speakerSize);

    g.setColour (s.grilleTint.withAlpha (0.72f));
    g.fillAll();
    drawWeave (g, grille, s.grille);

    // An inner shadow along the top edge, where the cloth meets the panel.
    g.setGradientFill (juce::ColourGradient::vertical (juce::Colours::black.withAlpha (0.35f), grille.getY(),
                                                      juce::Colours::transparentBlack, grille.getY() + 8.0f));
    g.fillRect (grille.withHeight (8.0f));
}

void AmpView::paint (juce::Graphics& g)
{
    IKAROS_PERF_PAINT ("AmpView");
    // The covering, faceplate and grille come from the cached image,
    // redrawn when the voice, cab or screen scale has changed.
    const auto scale = g.getInternalContext().getPhysicalPixelScaleFactor();

    if (background.isNull() || backgroundVoice != settings.voice || backgroundCab != settings.cab || backgroundScale != scale)
    {
        background = juce::Image (juce::Image::ARGB, juce::roundToInt (width * scale), juce::roundToInt (height * scale), true);
        juce::Graphics imageGraphics (background);
        imageGraphics.addTransform (juce::AffineTransform::scale (scale));
        drawBackground (imageGraphics);
        backgroundVoice = settings.voice;
        backgroundCab = settings.cab;
        backgroundScale = scale;
    }

    g.drawImage (background, getLocalBounds().toFloat());

    // The LED beside the power button: lit while the Amp is on.
    const auto led = juce::Rectangle<float> (10.0f, 10.0f).withCentre ({ (float) power->getRight() + 11.0f,
                                                                          (float) power->getBounds().getCentreY() });

    if (settings.on)
    {
        g.setColour (theme().accent.withAlpha (0.3f));
        g.fillEllipse (led.expanded (3.0f));
        g.setColour (theme().accent);
        g.fillEllipse (led);
        g.setColour (theme().accent.brighter (0.6f).withAlpha (0.8f));
        g.fillEllipse (led.reduced (2.5f).translated (0.0f, -0.5f));
    }
    else
    {
        g.setColour (juce::Colour (0xff1b1c1f));
        g.fillEllipse (led);
        g.setColour (juce::Colours::white.withAlpha (0.12f));
        g.fillEllipse (led.reduced (2.5f).translated (-0.5f, -1.0f));
    }

    g.setColour (juce::Colours::black.withAlpha (0.45f));
    g.drawEllipse (led, 1.0f);

    // The voice's name, in capitals with wide letter spacing.
    const auto& s = style();
    g.setColour (s.piping);
    g.setFont (fonts::uiExtraBold (textSize::xl).withExtraKerningFactor (0.16f));
    const auto nameLeft = power->getRight() + 24;
    g.drawText (s.name.toUpperCase(), juce::Rectangle<int> (nameLeft, inset, brightButton->getX() - nameLeft - 8, topRowHeight),
                juce::Justification::centredLeft, true);
}

void AmpView::paintOverChildren (juce::Graphics& g)
{
    IKAROS_PERF_PAINT ("AmpView::paintOverChildren");
    const auto bounds = getLocalBounds().toFloat();

    // Bypassed: dimmed and greyed out, knobs and all.
    if (! settings.on)
    {
        g.setColour (juce::Colour::greyLevel (style().tolex.getPerceivedBrightness() * 0.72f).withAlpha (0.4f));
        g.fillRoundedRectangle (bounds, cornerRadius);
    }

    if (hasKeyboardFocus (false))
        IkarosLookAndFeel::drawFocusRing (g, bounds, cornerRadius);
}

void AmpView::resized()
{
    // Top row: the power button (then the LED and the name, painted), then Bright and Voice on the right.
    power->setBounds (inset, inset, topRowHeight, topRowHeight);

    auto row = juce::Rectangle<int> (inset, inset, width - inset * 2, topRowHeight);
    voiceButton->setBounds (row.removeFromRight (voiceButton->getIdealWidth()).withSizeKeepingCentre (voiceButton->getIdealWidth(), pillHeight));
    row.removeFromRight (pillGap);
    brightButton->setBounds (row.removeFromRight (brightButton->getIdealWidth()));

    // Six knobs evenly across the faceplate.
    const auto plate = faceplateArea();
    const auto column = (float) plate.getWidth() / (float) ampKnobCount;

    for (int i = 0; i < knobs.size(); ++i)
    {
        const auto centreX = juce::roundToInt ((float) plate.getX() + column * ((float) i + 0.5f));
        knobs[i]->setBounds (centreX - knobSize / 2, plate.getY() + knobTopInFaceplate, knobSize, knobSize);
    }

    // The cab badge, bottom right of the grille.
    const auto grille = grilleArea().reduced (8);
    const auto badgeWidth = cabButton->getIdealWidth();
    cabButton->setBounds (grille.getRight() - badgeWidth, grille.getBottom() - pillHeight, badgeWidth, pillHeight);
}

} // namespace ikaros
