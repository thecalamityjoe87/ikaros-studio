#include "ui/bottompanel/PianoKeyboard.h"
#include "engine/AudioEngine.h"
#include "engine/TrackController.h"
#include "ui/theme/Fonts.h"
#include "ui/theme/Tokens.h"

namespace ikaros
{

using namespace tokens;

namespace
{
    constexpr int lowestNote = 48;    // C3
    constexpr int highestNote = 83;   // B5
    constexpr int numWhiteKeys = 21;
}

//==============================================================================
// MidiKeyboardComponent with our own key drawing. Overriding its virtual
// draw methods is how JUCE expects you to restyle it.
class PianoKeyboard::Keys  : public juce::MidiKeyboardComponent
{
public:
    explicit Keys (juce::MidiKeyboardState& s)
        : juce::MidiKeyboardComponent (s, juce::KeyboardComponentBase::horizontalKeyboard)
    {
        setAvailableRange (lowestNote, highestNote);
        setScrollButtonsVisible (false);
        setBlackNoteWidthProportion (0.55f);
        setBlackNoteLengthProportion (0.6f);
        setVelocity (0.8f, true);                 // velocity follows how low on the key you click
        setKeyPressBaseOctave (4);                // computer keyboard 'A' plays C4
        setOctaveForMiddleC (4);
        setTitle ("On-screen keyboard");
        setColour (juce::MidiKeyboardComponent::shadowColourId, juce::Colours::transparentBlack);
        setColour (juce::MidiKeyboardComponent::keySeparatorLineColourId, juce::Colours::transparentBlack);
    }

    // MidiKeyboardComponent maps letter keys to notes and would swallow
    // shortcuts like Ctrl/Cmd+S. Let anything with a modifier pass through to
    // the app's command manager instead.
    static bool hasShortcutModifier()
    {
        auto mods = juce::ModifierKeys::getCurrentModifiers();
        return mods.isCommandDown() || mods.isCtrlDown() || mods.isAltDown();
    }

    bool keyPressed (const juce::KeyPress& key) override
    {
        return ! hasShortcutModifier() && juce::MidiKeyboardComponent::keyPressed (key);
    }

    bool keyStateChanged (bool isKeyDown) override
    {
        return ! hasShortcutModifier() && juce::MidiKeyboardComponent::keyStateChanged (isKeyDown);
    }

    void drawWhiteNote (int note, juce::Graphics& g, juce::Rectangle<float> area,
                        bool isDown, bool isOver, juce::Colour, juce::Colour) override
    {
        auto key = area.reduced (1.0f, 0.0f);
        g.setColour (isDown ? colour::pianoWhiteLit : (isOver ? colour::pianoWhite.darker (0.05f) : colour::pianoWhite));
        g.fillRoundedRectangle (key.withTrimmedTop (-6.0f), 5.0f);   // round only the bottom corners

        if (note % 12 == 0)
        {
            g.setColour (colour::pianoLabel);
            g.setFont (fonts::ui (10.0f));
            g.drawText ("C" + juce::String (note / 12 - 1), key.removeFromBottom (18.0f), juce::Justification::centred);
        }
    }

    void drawBlackNote (int, juce::Graphics& g, juce::Rectangle<float> area,
                        bool isDown, bool isOver, juce::Colour) override
    {
        auto key = area.withTrimmedTop (-4.0f);
        g.setColour (isDown ? colour::accent : (isOver ? colour::pianoBlack.brighter (0.15f) : colour::pianoBlack));
        g.fillRoundedRectangle (key, 4.0f);

        g.setColour (colour::border);
        g.fillRoundedRectangle (key.removeFromBottom (3.0f), 1.5f);
    }
};

//==============================================================================
PianoKeyboard::PianoKeyboard (AudioEngine& e) : engine (e)
{
    keys = std::make_unique<Keys> (state);
    addAndMakeVisible (*keys);
    state.addListener (this);
}

PianoKeyboard::~PianoKeyboard()
{
    state.removeListener (this);
    releaseAll();
}

void PianoKeyboard::releaseAll()
{
    state.allNotesOff (1);
    engine.getTracks().allNotesOff();
}

void PianoKeyboard::handleNoteOn (juce::MidiKeyboardState*, int, int note, float velocity)
{
    engine.getTracks().noteOn (note, velocity);
}

void PianoKeyboard::handleNoteOff (juce::MidiKeyboardState*, int, int note, float)
{
    engine.getTracks().noteOff (note);
}

void PianoKeyboard::paint (juce::Graphics& g)
{
    g.setColour (colour::lcdBg);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), size::radiusLarge);
}

void PianoKeyboard::resized()
{
    auto r = getLocalBounds().reduced (4);
    keys->setBounds (r);
    keys->setKeyWidth ((float) r.getWidth() / (float) numWhiteKeys);
}

} // namespace ikaros
