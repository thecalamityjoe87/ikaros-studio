#pragma once

#include <tracktion_engine/tracktion_engine.h>
#include <array>
#include <atomic>

namespace ikaros
{

//==============================================================================
/**
    The hidden effect behind a sampled instrument's Sound tab knobs (see
    EffectsController::setSoundControl). Tracktion's sampler has no settings
    of its own, so Tone, Drive, Width and Room are done here, right after the
    instrument:

      tone   0..1, 0.5 = unchanged. Lower closes a low-pass filter (darker),
             higher lifts the treble with a shelf (brighter).
      drive  0..1, 0 = clean. Soft saturation, louder parts first.
      width  0..1, 0.5 = unchanged, 0 = mono, 1 = twice as wide.
      room   0..1, 0 = dry. How much reverb, and how big the room is.

    Each is an AutomatableParameter backed by a property on the plugin's
    state, so the settings are saved with the session and undo restores them.
    The user never sees this plugin in the effects list.
*/
class SoundShaperPlugin  : public tracktion::Plugin
{
public:
    explicit SoundShaperPlugin (tracktion::PluginCreationInfo);
    ~SoundShaperPlugin() override;

    static const char* xmlTypeName;    // "ikarosSoundShaper"

    // The parameter IDs, which are also the names of the state properties.
    static const juce::Identifier toneID, driveID, widthID, roomID;

    /** The parameter for "tone", "drive", "width" or "room" (nullptr for anything else). */
    tracktion::AutomatableParameter* getParameterFor (const juce::String& control) const;

    /** A setting's neutral value: where it leaves the sound unchanged. */
    static float neutralValueOf (const juce::String& control);

    //==============================================================================
    // tracktion::Plugin
    juce::String getName() const override                   { return "Sound Shaper"; }
    juce::String getPluginType() override                   { return xmlTypeName; }
    juce::String getShortName (int) override                { return "Shaper"; }
    juce::String getSelectableDescription() override        { return getName(); }
    int getNumOutputChannelsGivenInputs (int) override      { return 2; }
    BusLayout getBusses() const override                    { return BusLayout::singleStereoInOut(); }
    bool canBeAddedToClip() override                        { return false; }

    void initialise (const tracktion::PluginInitialisationInfo&) override;
    void deinitialise() override                            {}
    void reset() override;
    void applyToBuffer (const tracktion::PluginRenderContext&) override;
    void restorePluginStateFromValueTree (const juce::ValueTree&) override;

private:
    // CachedValue keeps a copy of a state property, and writes changes
    // through the undo manager. The parameters read and write these.
    juce::CachedValue<float> toneValue, driveValue, widthValue, roomValue;
    tracktion::AutomatableParameter::Ptr tone, drive, width, room;

    // Audio-thread state.
    std::array<juce::IIRFilter, 2> lowPass, highShelf;
    float filterTone = -1.0f;              // the tone the filters were last set up for
    juce::Reverb reverb;
    float reverbRoom = -1.0f;              // the room the reverb was last set up for
    juce::SmoothedValue<float> driveGain { 1.0f };
    double rate = 44100.0;

    void updateFilters (float toneSetting);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SoundShaperPlugin)
};

//==============================================================================
/**
    The hidden plugin behind a "tune" knob: it moves every note up or down by
    whole semitones before the instrument plays it.

    Tracktion's own MIDI Modifier does the same, but it shifts each note-off
    by whatever the setting is when the key comes up, so turning the knob
    while a note is held would leave that note hanging. This remembers how far
    each held note was moved and moves its note-off the same amount.
*/
class TransposePlugin  : public tracktion::Plugin
{
public:
    explicit TransposePlugin (tracktion::PluginCreationInfo);
    ~TransposePlugin() override;

    static const char* xmlTypeName;            // "ikarosTranspose"
    static const juce::Identifier semitonesID;
    static constexpr float maxSemitones = 12.0f;

    tracktion::AutomatableParameter* getSemitonesParameter() const    { return semitones.get(); }

    //==============================================================================
    // tracktion::Plugin
    juce::String getName() const override                   { return "Transpose"; }
    juce::String getPluginType() override                   { return xmlTypeName; }
    juce::String getShortName (int) override                { return "Transpose"; }
    juce::String getSelectableDescription() override        { return getName(); }
    bool takesMidiInput() override                          { return true; }
    bool takesAudioInput() override                         { return false; }
    BusLayout getBusses() const override                    { return {}; }
    int getNumOutputChannelsGivenInputs (int) override      { return 0; }
    bool canBeAddedToClip() override                        { return false; }

    void initialise (const tracktion::PluginInitialisationInfo&) override;
    void deinitialise() override                            {}
    void applyToBuffer (const tracktion::PluginRenderContext&) override;
    void restorePluginStateFromValueTree (const juce::ValueTree&) override;

private:
    juce::CachedValue<float> semitonesValue;
    tracktion::AutomatableParameter::Ptr semitones;

    // How far each held note (by MIDI channel and note number) was moved.
    // Only the audio thread uses it.
    std::array<std::array<juce::int8, 128>, 16> heldShift {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TransposePlugin)
};

} // namespace ikaros
