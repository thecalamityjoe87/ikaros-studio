#pragma once

// Engine-internal (the UI never includes this): the Sound tab's knobs as
// Tracktion parameters, so they can be automated. Implemented in
// EffectsController.cpp, next to the rest of the knob code.

#include "engine/TrackHelpers.h"
#include <functional>

namespace ikaros
{

class SoundLibrary;

/** One Sound tab knob as a Tracktion parameter. Knobs go 0..1; the
    parameter behind one may not (the synth's attack is in seconds, "Pitch"
    in semitones), so this also says how to map between the two. */
struct SoundControlParameter
{
    juce::String name;                                  // the knob's label, e.g. "Brightness"
    te::AutomatableParameter* parameter = nullptr;      // nullptr if the hidden plugin it needs isn't there yet
    juce::Identifier stateProperty;                     // where its plugin stores the value (see AutomationEditor)
    std::function<float (float)> toParameter;           // knob 0..1 -> the parameter's value
    std::function<float (float)> fromParameter;         // and back
    std::function<juce::String (float)> describe;       // knob 0..1 -> what the knob shows, e.g. "62%"
    float valueWithoutParameter = 0.0f;                 // what the knob shows while parameter is nullptr
};

/** The knobs a track's Sound tab shows, in order. With createMissing, the
    hidden plugins a knob needs are added (in the current undo step), so
    every knob has its parameter. */
std::vector<SoundControlParameter> getSoundControlParameters (te::AudioTrack&, const SoundLibrary&, bool createMissing);

} // namespace ikaros
