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
    constexpr int numOctaves = 3;
    constexpr int numWhiteKeys = 7 * numOctaves;
    constexpr int defaultLowestNote = 48;   // C3
    constexpr int minLowestNote = 12;       // C0
    constexpr int maxLowestNote = 84;       // C6 (so the top key is B8)
    constexpr int buttonWidth = 26;
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
        setAvailableRange (defaultLowestNote, defaultLowestNote + numOctaves * 12 - 1);
        setScrollButtonsVisible (false);
        setBlackNoteWidthProportion (0.55f);
        setBlackNoteLengthProportion (0.6f);
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
        if (hasShortcutModifier())
            return false;

        // Z and X move down and up an octave, as in GarageBand.
        if (onOctaveShift && (key.getTextCharacter() == 'z' || key.getTextCharacter() == 'x'))
        {
            onOctaveShift (key.getTextCharacter() == 'z' ? -1 : 1);
            return true;
        }

        return juce::MidiKeyboardComponent::keyPressed (key);
    }

    // The wheel moves the keyboard an octave per notch (MidiKeyboardComponent
    // would otherwise scroll within the visible range, which is all of it).
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel) override
    {
        const auto amount = std::abs (wheel.deltaX) > std::abs (wheel.deltaY) ? -wheel.deltaX : wheel.deltaY;
        wheelAccumulator += wheel.isReversed ? -amount : amount;

        // Trackpads send many small deltas: move once per notch's worth.
        if (std::abs (wheelAccumulator) >= 0.12f && onOctaveShift)
        {
            onOctaveShift (wheelAccumulator > 0.0f ? 1 : -1);
            wheelAccumulator = 0.0f;
        }
    }

    std::function<void (int direction)> onOctaveShift;
    float wheelAccumulator = 0.0f;

    bool keyStateChanged (bool isKeyDown) override
    {
        return ! hasShortcutModifier() && juce::MidiKeyboardComponent::keyStateChanged (isKeyDown);
    }

    // Keys held on a hardware keyboard look pressed too. (Only drawn: the
    // keyboard's own state would play them a second time.)
    std::bitset<128> heldOnController;

    void drawWhiteNote (int note, juce::Graphics& g, juce::Rectangle<float> area,
                        bool isDown, bool isOver, juce::Colour, juce::Colour) override
    {
        isDown = isDown || heldOnController[(size_t) note];

        // The 1px gaps either side of the key are its dividers.
        g.setColour (theme().pianoKeyDivider);
        g.fillRect (area);

        auto key = area.reduced (1.0f, 0.0f);
        g.setColour (isDown ? theme().pianoWhiteDown : (isOver ? theme().pianoWhite.darker (0.05f) : theme().pianoWhite));
        g.fillRoundedRectangle (key.withTrimmedTop (-6.0f), size::radiusSmall);   // round only the bottom corners

        if (note % 12 == 0)
        {
            g.setColour (theme().pianoLabel);
            g.setFont (fonts::ui (textSize::tiny));
            g.drawText ("C" + juce::String (note / 12 - 1), key.removeFromBottom (18.0f), juce::Justification::centred);
        }
    }

    void drawBlackNote (int note, juce::Graphics& g, juce::Rectangle<float> area,
                        bool isDown, bool isOver, juce::Colour) override
    {
        isDown = isDown || heldOnController[(size_t) note];

        auto key = area.withTrimmedTop (-4.0f);
        g.setColour (isDown ? theme().pianoBlackDown : (isOver ? theme().pianoBlack.brighter (0.15f) : theme().pianoBlack));
        g.fillRoundedRectangle (key, size::radiusXs);

        g.setColour (theme().pianoBlackLip);
        g.fillRoundedRectangle (key.removeFromBottom (3.0f), 1.5f);
    }
};

//==============================================================================
void PianoKeyboard::setHeldNotes (const std::bitset<128>& notes)
{
    if (notes == keys->heldOnController)
        return;

    // A key just pressed outside the octaves shown (the controller's own
    // octave buttons, say): move the keyboard so it's in view, by octaves.
    const auto highest = lowestNote + numOctaves * 12 - 1;
    auto start = lowestNote;

    for (int note = 0; note < 128; ++note)
    {
        const bool justPressed = notes[(size_t) note] && ! keys->heldOnController[(size_t) note];

        if (justPressed && note < lowestNote)
            start = note / 12 * 12;                                // it's in the bottom octave
        else if (justPressed && note > highest)
            start = note / 12 * 12 - (numOctaves - 1) * 12;        // ...or the top one
    }

    keys->heldOnController = notes;
    start = juce::jlimit (minLowestNote, maxLowestNote, start);

    if (start != lowestNote)
    {
        releaseAll();
        lowestNote = start;
        updateRange();
    }

    keys->repaint();
}

PianoKeyboard::PianoKeyboard (AudioEngine& e) : engine (e)
{
    keys = std::make_unique<Keys> (state);
    keys->onOctaveShift = [this] (int direction) { shiftOctave (direction); };
    addAndMakeVisible (*keys);
    state.addListener (this);
    engine.getPreferences().addListener (this);
    applyVelocityPreference();

    for (auto* b : { &octaveDown, &octaveUp })
    {
        b->setColours (IconButton::ghostColours);
        b->setCornerRadius (6.0f);
        b->setIconSize (18.0f);
        b->setMouseClickGrabsKeyboardFocus (false);
        addAndMakeVisible (b);
    }

    octaveDown.onClick = [this] { shiftOctave (-1); };
    octaveUp.onClick = [this] { shiftOctave (1); };
    updateRange();
}

void PianoKeyboard::shiftOctave (int direction)
{
    const auto newLowest = juce::jlimit (minLowestNote, maxLowestNote, lowestNote + direction * 12);

    if (newLowest == lowestNote)
        return;

    releaseAll();   // a held key would otherwise never get its note-off
    lowestNote = newLowest;
    updateRange();
}

void PianoKeyboard::showRange (int lowestPlayed, int highestPlayed)
{
    const auto floorC = lowestPlayed / 12 * 12;
    auto start = floorC;

    // Wider than the keyboard (a piano): the middle, as near C3 as the range allows.
    if (highestPlayed - start >= numOctaves * 12)
        start = juce::jlimit (floorC, juce::jmax (floorC, highestPlayed / 12 * 12 - (numOctaves - 1) * 12), defaultLowestNote);

    start = juce::jlimit (minLowestNote, maxLowestNote, start);

    if (start == lowestNote)
        return;

    releaseAll();
    lowestNote = start;
    updateRange();
}

void PianoKeyboard::updateRange()
{
    const auto highest = lowestNote + numOctaves * 12 - 1;
    keys->setAvailableRange (lowestNote, highest);

    // The computer keyboard's 'A' plays the lowest C shown.
    keys->setKeyPressBaseOctave (lowestNote / 12);

    auto noteName = [] (int note) { return juce::MidiMessage::getMidiNoteName (note, true, true, 4); };
    const auto range = noteName (lowestNote) + juce::String::fromUTF8 ("\xe2\x80\x93") + noteName (highest);   // en dash
    octaveDown.setEnabled (lowestNote > minLowestNote);
    octaveUp.setEnabled (lowestNote < maxLowestNote);
    octaveDown.setTooltip ("Octave down (Z). Showing " + range);
    octaveUp.setTooltip ("Octave up (X). Showing " + range);
    resized();
}

PianoKeyboard::~PianoKeyboard()
{
    engine.getPreferences().removeListener (this);
    state.removeListener (this);
    releaseAll();
}

void PianoKeyboard::preferenceChanged (const juce::String& key)
{
    if (key.isEmpty() || key == settings::keyboardVelocity.key)
        applyVelocityPreference();
}

// The top velocity the keys play (a click lower on a key plays harder, up to this).
void PianoKeyboard::applyVelocityPreference()
{
    const float velocities[] { 0.55f, 0.8f, 1.0f };   // soft, normal, hard
    const auto choice = juce::jlimit (0, 2, engine.getPreferences().get (settings::keyboardVelocity));
    keys->setVelocity (velocities[choice], true);
}

void PianoKeyboard::releaseAll()
{
    state.allNotesOff (1);
    engine.getTracks().inputs().allNotesOff();
}

void PianoKeyboard::handleNoteOn (juce::MidiKeyboardState*, int, int note, float velocity)
{
    engine.getTracks().inputs().noteOn (note, velocity);
}

void PianoKeyboard::handleNoteOff (juce::MidiKeyboardState*, int, int note, float)
{
    engine.getTracks().inputs().noteOff (note);
}

void PianoKeyboard::paint (juce::Graphics& g)
{
    const auto tray = getLocalBounds().toFloat();
    g.setColour (theme().keyboardTray);
    g.fillRoundedRectangle (tray, size::radiusLarge);
    g.setColour (theme().keyboardTrayBorder);
    g.drawRoundedRectangle (tray.reduced (0.5f), size::radiusLarge, 1.0f);
}

void PianoKeyboard::resized()
{
    auto r = getLocalBounds().reduced (4);
    octaveDown.setBounds (r.removeFromLeft (buttonWidth));
    octaveUp.setBounds (r.removeFromRight (buttonWidth));
    keys->setBounds (r);

    // JUCE asserts on a zero key width; we are resized before first layout.
    if (r.getWidth() > 0)
        keys->setKeyWidth ((float) r.getWidth() / (float) numWhiteKeys);
}

} // namespace ikaros
