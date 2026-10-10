#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

namespace ikaros
{

class AudioEngine;

//==============================================================================
/**
    A 3-octave on-screen keyboard (C3–B5) that plays the selected track.

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

private:
    class Keys;

    AudioEngine& engine;
    juce::MidiKeyboardState state;
    std::unique_ptr<Keys> keys;

    // MidiKeyboardState::Listener. Called on the message thread for mouse and
    // computer-keyboard input.
    void handleNoteOn (juce::MidiKeyboardState*, int channel, int note, float velocity) override;
    void handleNoteOff (juce::MidiKeyboardState*, int channel, int note, float velocity) override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PianoKeyboard)
};

} // namespace ikaros
