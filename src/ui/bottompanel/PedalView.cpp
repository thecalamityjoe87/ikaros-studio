#include "ui/bottompanel/PedalView.h"
#include "engine/TrackController.h"
#include "ui/theme/Fonts.h"
#include "ui/theme/IkarosLookAndFeel.h"
#include "ui/theme/Theme.h"
#include "ui/theme/Tokens.h"

namespace ikaros
{

using namespace tokens;

namespace
{
    // The layout, in pixels from the pedal's top left (it's always
    // PedalView::width x PedalView::height).
    constexpr float screwInset = 13.0f, screwRadius = 3.0f;
    constexpr float ledY = 14.0f, ledRadius = 5.0f;
    constexpr int knobTop = 25, knobSize = 38;
    constexpr int labelTop = 65, labelHeight = 14;
    constexpr int nameTop = 86, nameHeight = 22;
    constexpr int typeTop = 108, typeHeight = 14;
    constexpr int footswitchCentreY = 174, footswitchSize = 50;
    constexpr int lipHeight = 4;   // the darker strip along the bottom edge: the enclosure's thickness

    // Lettering: near-black on light enclosures, warm white on dark ones.
    const juce::Colour darkLettering (0xff23211d), lightLettering (0xfff5f2ec);

    juce::Font withSpacing (juce::Font f, float extraKerning)
    {
        return f.withExtraKerningFactor (extraKerning);
    }
}

//==============================================================================
// The footswitch: a metal button in a ring. On, the ring is lit in the
// accent colour. It's a real juce::Button, so Tab reaches it and Space or
// Enter presses it, and screen readers announce it.
class PedalView::Footswitch  : public juce::Button
{
public:
    explicit Footswitch (PedalView& p) : juce::Button ("Footswitch"), pedal (p)
    {
        setWantsKeyboardFocus (true);
        setMouseClickGrabsKeyboardFocus (false);   // Tab reaches it, but a click leaves no focus ring
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    void paintButton (juce::Graphics& g, bool highlighted, bool down) override
    {
        const auto centre = getLocalBounds().toFloat().getCentre();
        const auto enclosure = pedal.style.colour;

        // The recess the switch sits in.
        const auto ringRadius = getWidth() * 0.42f;
        g.setColour (enclosure.darker (0.45f));
        g.fillEllipse (juce::Rectangle<float> (ringRadius * 2.0f, ringRadius * 2.0f).withCentre (centre));

        if (pedal.slot.enabled)
        {
            g.setColour (theme().accent);
            g.drawEllipse (juce::Rectangle<float> (ringRadius * 2.0f, ringRadius * 2.0f).withCentre (centre).reduced (1.25f), 2.5f);
        }

        // The metal cap: a light spot towards the top left, darker at the
        // edge, so it reads as a dome. Pressed, it sinks a little.
        const auto capRadius = ringRadius * (down ? 0.6f : 0.64f);
        const auto cap = juce::Rectangle<float> (capRadius * 2.0f, capRadius * 2.0f).withCentre (centre);
        juce::Path capPath;
        capPath.addEllipse (cap);
        juce::DropShadow (juce::Colours::black.withAlpha (0.45f), 5, { 0, 2 }).drawForPath (g, capPath);

        juce::ColourGradient metal (juce::Colours::white, cap.getX() + cap.getWidth() * 0.38f, cap.getY() + cap.getHeight() * 0.3f,
                                    juce::Colour (0xff7d838d), cap.getRight(), cap.getBottom(), true);
        metal.addColour (0.45, juce::Colour (0xffcfd2d7));
        g.setGradientFill (metal);
        g.fillEllipse (cap);

        if (highlighted)
        {
            g.setColour (juce::Colours::white.withAlpha (0.15f));
            g.fillEllipse (cap);
        }

        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.drawEllipse (cap, 1.0f);

        if (hasKeyboardFocus (false))
            IkarosLookAndFeel::drawFocusRing (g, getLocalBounds().toFloat(), getWidth() * 0.5f);
    }

private:
    PedalView& pedal;
};

//==============================================================================
PedalView::PedalView (AudioEngine& e, const PedalStyle& s) : engine (e), style (s)
{
    setTitle (style.name);
    setDescription (style.typeLabel + " pedal");
    setWantsKeyboardFocus (true);   // so the keyboard can move, remove and open the menu of a pedal

    for (int i = 0; i < (int) style.knobs.size(); ++i)
    {
        auto* knob = knobs.add (new juce::Slider (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox));
        knob->getProperties().set ("pedal", true);   // drawn by IkarosLookAndFeel's drawPedalKnob
        knob->setColour (IkarosLookAndFeel::knobArcColourId, lettering());
        knob->setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
        knob->setRange (0.0, 1.0);
        knob->setMouseDragSensitivity (180);
        knob->setWantsKeyboardFocus (true);   // juce::Slider doesn't by default; with it, the arrow keys turn it
        knob->setTitle (style.knobs[(size_t) i].label);

        // Screen readers (and the tooltip) get the setting's own text, e.g. "350 ms".
        knob->textFromValueFunction = [this, i] (double) { return valueText[(size_t) i]; };

        // A mouse drag is one undo step (started when it begins). A change
        // that isn't a drag (an arrow key, the mouse wheel) gets a step of
        // its own.
        knob->onDragStart = [this, i]
        {
            dragging[(size_t) i] = true;
            engine.beginUndoStep (style.name + " " + style.knobs[(size_t) i].label);
        };
        knob->onDragEnd = [this, i] { dragging[(size_t) i] = false; };
        knob->onValueChange = [this, i] { knobChanged (i); };
        addAndMakeVisible (knob);
    }

    parameterIndex.assign (style.knobs.size(), -1);
    valueText.resize (style.knobs.size());
    dragging.assign (style.knobs.size(), false);

    footswitch = std::make_unique<Footswitch> (*this);
    footswitch->onClick = [this]
    {
        engine.getTracks().effects().setEffectEnabled (trackId, slot.id, ! slot.enabled);
    };
    addAndMakeVisible (*footswitch);
}

PedalView::~PedalView() = default;

juce::Colour PedalView::lettering() const
{
    return style.darkLettering ? darkLettering : lightLettering;
}

void PedalView::update (TrackId track, const PluginSlot& newSlot, const std::vector<ParameterInfo>& parameters)
{
    const bool onChanged = newSlot.enabled != slot.enabled;
    trackId = track;
    slot = newSlot;

    for (int i = 0; i < knobs.size(); ++i)
    {
        auto& knobStyle = style.knobs[(size_t) i];
        auto* knob = knobs[i];
        parameterIndex[(size_t) i] = -1;

        for (int p = 0; p < (int) parameters.size(); ++p)
            if (parameters[(size_t) p].id == knobStyle.parameterId)
                parameterIndex[(size_t) i] = p;

        const auto index = parameterIndex[(size_t) i];
        knob->setEnabled (index >= 0);   // (a style row naming a setting the effect doesn't have)

        if (index < 0)
            continue;

        auto& parameter = parameters[(size_t) index];
        valueText[(size_t) i] = parameter.valueText;
        knob->setTooltip (knobStyle.label + ": " + parameter.valueText);

        // Leave a knob the mouse is turning alone, so it doesn't jitter.
        if (! dragging[(size_t) i])
            knob->setValue (knobStyle.reversed ? 1.0f - parameter.value : parameter.value, juce::dontSendNotification);
    }

    footswitch->setTitle (style.name + (slot.enabled ? " footswitch, on" : " footswitch, bypassed"));
    footswitch->setTooltip (slot.enabled ? "Turn " + style.name + " off (bypass)" : "Turn " + style.name + " on");

    if (onChanged)
        repaint();
}

void PedalView::knobChanged (int index)
{
    const auto parameter = parameterIndex[(size_t) index];

    if (parameter < 0)
        return;

    if (! dragging[(size_t) index])
        engine.beginUndoStep (style.name + " " + style.knobs[(size_t) index].label);

    auto value = (float) knobs[index]->getValue();

    if (style.knobs[(size_t) index].reversed)
        value = 1.0f - value;

    engine.getTracks().effects().setEffectParameter (trackId, slot.id, parameter, value);
    // The new value text arrives with the next update(), when the engine
    // reports the change (see BottomPanel::refresh).
}

//==============================================================================
void PedalView::drawShadow (juce::Graphics& g, const PedalView& pedal)
{
    juce::Path body;
    body.addRoundedRectangle (pedal.getBounds().toFloat(), size::radiusCard);
    juce::DropShadow (juce::Colours::black.withAlpha (0.38f), 14, { 0, 6 }).drawForPath (g, body);
}

void PedalView::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    const auto colour = style.colour;
    const auto radius = size::radiusCard;
    const auto text = lettering();

    // The enclosure: a little lighter at the top (the light comes from
    // above), with a darker lip along the bottom edge.
    g.setGradientFill (juce::ColourGradient::vertical (colour.brighter (0.12f), bounds.getY(),
                                                      colour.darker (0.08f), bounds.getBottom()));
    g.fillRoundedRectangle (bounds, radius);

    {
        // Graphics::ScopedSaveState restores the clip region when it goes out of scope.
        juce::Graphics::ScopedSaveState state (g);
        g.reduceClipRegion (getLocalBounds().removeFromBottom (lipHeight));
        g.setColour (colour.darker (0.35f));
        g.fillRoundedRectangle (bounds, radius);
    }

    {
        // The top highlight: a thin lighter edge along the top.
        juce::Graphics::ScopedSaveState state (g);
        g.reduceClipRegion (getLocalBounds().removeFromTop ((int) radius));
        g.setColour (juce::Colours::white.withAlpha (0.3f));
        g.drawRoundedRectangle (bounds.reduced (0.5f), radius, 1.0f);
    }

    g.setColour (juce::Colours::black.withAlpha (0.2f));
    g.drawRoundedRectangle (bounds.reduced (0.5f), radius, 1.0f);

    // Two screws at the top.
    for (auto x : { screwInset, bounds.getWidth() - screwInset })
    {
        const auto screw = juce::Rectangle<float> (screwRadius * 2.0f, screwRadius * 2.0f).withCentre ({ x, screwInset });
        g.setColour (juce::Colours::black.withAlpha (0.45f));
        g.fillEllipse (screw);
        g.setColour (juce::Colours::white.withAlpha (0.25f));
        g.drawEllipse (screw.translated (0.0f, 0.6f), 0.6f);
    }

    // The LED between them: lit in the accent colour while the pedal is on.
    const auto ledCentre = juce::Point<float> (bounds.getCentreX(), ledY);
    const auto led = juce::Rectangle<float> (ledRadius * 2.0f, ledRadius * 2.0f).withCentre (ledCentre);

    if (slot.enabled)
    {
        g.setColour (theme().accent.withAlpha (0.3f));
        g.fillEllipse (led.expanded (3.5f));
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

    // The knobs' labels.
    g.setColour (text);
    g.setFont (withSpacing (fonts::uiSemiBold (textSize::xs), 0.06f));

    for (int i = 0; i < knobs.size(); ++i)
    {
        const auto area = knobArea (i);
        g.drawFittedText (style.knobs[(size_t) i].label.toUpperCase(),
                          juce::Rectangle<int> (area.getX() - 4, labelTop, area.getWidth() + 8, labelHeight),
                          juce::Justification::centred, 1, 0.8f);
    }

    // The name and, under it, what kind of pedal it is.
    g.setFont (fonts::uiExtraBold (textSize::xxl));
    g.drawFittedText (style.name, getLocalBounds().withY (nameTop).withHeight (nameHeight).reduced (6, 0),
                      juce::Justification::centred, 1, 0.85f);
    g.setColour (text.withAlpha (0.7f));
    g.setFont (withSpacing (fonts::uiSemiBold (textSize::xs), 0.14f));
    g.drawFittedText (style.typeLabel.toUpperCase(), getLocalBounds().withY (typeTop).withHeight (typeHeight).reduced (6, 0),
                      juce::Justification::centred, 1, 0.85f);
}

void PedalView::paintOverChildren (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();

    // Bypassed: drawn over everything (knobs and footswitch too), a grey as
    // bright as the enclosure but a little darker greys it out and dims it.
    if (! slot.enabled)
    {
        g.setColour (juce::Colour::greyLevel (style.colour.getPerceivedBrightness() * 0.72f).withAlpha (0.4f));
        g.fillRoundedRectangle (bounds, size::radiusCard);
    }

    if (hasKeyboardFocus (false))
        IkarosLookAndFeel::drawFocusRing (g, bounds, size::radiusCard);
}

juce::Rectangle<int> PedalView::knobArea (int index) const
{
    // Three knobs across the pedal, or two a little closer together.
    const int count = knobs.size();
    const int spacing = count >= 3 ? 48 : 56;
    const int centreX = width / 2 + (index * 2 - (count - 1)) * spacing / 2;
    return { centreX - knobSize / 2, knobTop, knobSize, knobSize };
}

void PedalView::resized()
{
    for (int i = 0; i < knobs.size(); ++i)
        knobs[i]->setBounds (knobArea (i));

    footswitch->setBounds (juce::Rectangle<int> (footswitchSize, footswitchSize).withCentre ({ width / 2, footswitchCentreY }));
}

//==============================================================================
void PedalView::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
    {
        if (onMenuRequested)
            onMenuRequested (*this);

        return;
    }

    if (onBodyMouseDown)
        onBodyMouseDown (*this, e);
}

void PedalView::mouseDrag (const juce::MouseEvent& e)
{
    if (! e.mods.isPopupMenu() && onBodyMouseDrag)
        onBodyMouseDrag (*this, e);
}

void PedalView::mouseUp (const juce::MouseEvent& e)
{
    if (! e.mods.isPopupMenu() && onBodyMouseUp)
        onBodyMouseUp (*this, e);
}

bool PedalView::keyPressed (const juce::KeyPress& key)
{
    // commandModifier is Cmd on a Mac and Ctrl elsewhere.
    const auto moveLeft  = juce::KeyPress (juce::KeyPress::leftKey, juce::ModifierKeys::commandModifier, 0);
    const auto moveRight = juce::KeyPress (juce::KeyPress::rightKey, juce::ModifierKeys::commandModifier, 0);

    if (key == juce::KeyPress (juce::KeyPress::F10Key, juce::ModifierKeys::shiftModifier, 0))
    {
        if (onMenuRequested)
            onMenuRequested (*this);

        return true;
    }

    if (key == moveLeft || key == moveRight)
    {
        if (onMoveRequested)
            onMoveRequested (*this, key == moveLeft ? -1 : 1);

        return true;
    }

    if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)
    {
        if (onRemoveRequested)
            onRemoveRequested (*this);

        return true;
    }

    return false;
}

} // namespace ikaros
