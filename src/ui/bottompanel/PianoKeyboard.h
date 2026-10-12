#pragma once

#include "ui/widgets/IconButton.h"
#include <juce_audio_utils/juce_audio_utils.h>

namespace ikaros
{

class AudioEngine;

//==============================================================================
/**
    A 3-octave on-screen keyboard that plays the selected track. It starts
    at C3-B5; the arrow buttons at either end, the mouse wheel over the keys,
    or Z and X (as in GarageBand's Musical Typing) move it an octave down or up.

    Built on juce::MidiKeyboardComponent, which handles mouse and computer-
    keyboard input (A W S E D F T G Y H U J K ... play notes). We only restyle
    the keys and forward note events to the engine.
*/
class PianoKeyboard  : public juce::Component,
                       private juce::MidiKeyboardState::Listener
{
public:
    explicit PianoKeyboard (AudioEngine&);
    ~PianoKeyboard() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    /** Releases any held keys (e.g. when the selected track changes). */
    void releaseAll();

    /** Moves the keyboard up (+1) or down (-1) an octave. */
    void shiftOctave (int direction);

private:
    class Keys;

    AudioEngine& engine;
    juce::MidiKeyboardState state;
    std::unique_ptr<Keys> keys;
    IconButton octaveDown { "Octave down", Icon::chevronLeft };
    IconButton octaveUp { "Octave up", Icon::chevronRight };
    int lowestNote = 48;   // C3; always a C

    void updateRange();

    // MidiKeyboardState::Listener. Called on the message thread for mouse and
    // computer-keyboard input.
    void handleNoteOn (juce::MidiKeyboardState*, int channel, int note, float velocity) override;
    void handleNoteOff (juce::MidiKeyboardState*, int channel, int note, float velocity) override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PianoKeyboard)
};

} // namespace ikaros
