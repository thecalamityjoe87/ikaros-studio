#pragma once

// Plain data types the UI uses to talk to the engine layer.
//
// The UI never sees a tracktion::Edit, Track or Clip. Instead the engine layer
// hands out these small value types (snapshots of engine state), and the UI
// refers to tracks and regions by ID. That keeps src/ui/ free of Tracktion
// headers and makes it obvious where engine work happens.

#include <juce_graphics/juce_graphics.h>
#include <cstdint>
#include <vector>

namespace ikaros
{

// Tracktion identifies every track and clip with a 64-bit EditItemID.
// We pass the raw number around; 0 means "none".
using TrackId  = std::uint64_t;
using RegionId = std::uint64_t;
using PluginId = std::uint64_t;

// Instrument tracks play a synth from MIDI; audio tracks record a microphone,
// guitar or other audio input.
enum class TrackKind { instrument, audio };

struct NoteInfo
{
    int pitch = 60;            // MIDI note number
    double startBeat = 0.0;    // relative to the start of the region
    double lengthBeats = 1.0;
    int velocity = 100;
};

struct RegionInfo
{
    RegionId id = 0;
    juce::String name;
    double startBeat = 0.0;    // position on the timeline, in beats (quarter notes)
    double lengthBeats = 0.0;
    bool isMidi = true;
    std::vector<NoteInfo> notes;   // MIDI regions only
    std::vector<float> peaks;      // audio regions only: waveform, 0..1 per column across the region
};

struct TrackInfo
{
    TrackId id = 0;
    int index = 0;
    TrackKind kind = TrackKind::instrument;
    juce::String name;
    juce::String soundName;        // e.g. "Grand Piano" (the preset shown in the library)
    juce::String category;         // library category, used to pick the icon
    juce::Colour colour;
    bool muted = false;
    bool solo = false;
    bool silencedBySolo = false;   // another track is soloed, so this one is quiet
    bool armed = false;            // record-enabled
    float volume = 0.75f;          // fader position 0..1 (Tracktion's slider curve)
    float volumeDb = 0.0f;
};

// Stereo peak level in decibels.
struct StereoLevel
{
    float leftDb = -100.0f;
    float rightDb = -100.0f;
};

// One of the five big knobs on the "Sound" tab.
struct SoundControl
{
    juce::String name;
    float value = 0.0f;            // normalised 0..1
    juce::String valueText;        // human readable, e.g. "62%" or "0.8 s"
};

// One plug-in on a track: the instrument, or an effect in the chain.
struct PluginSlot
{
    PluginId id = 0;
    juce::String type;             // Tracktion's plugin type, e.g. "reverb"
    juce::String name;             // what the user sees, e.g. "Reverb"
    bool enabled = true;           // false = bypassed
};

// An effect the user can add from the "+ Add effect" menu.
struct EffectType
{
    juce::String type;             // Tracktion's plugin type
    juce::String name;             // what the user sees
    juce::String category;         // menu group, e.g. "Space"
};

// One adjustable setting of a plug-in (shown as a knob in the effect editor).
struct ParameterInfo
{
    juce::String name;
    float value = 0.0f;            // normalised 0..1
    juce::String valueText;        // human readable, with units
};

// An audio input the user can record from, e.g. "Input 1".
struct AudioInput
{
    juce::String id;               // stable ID, saved with the track
    juce::String name;
};

// An audio track's recording settings (the inspector's Recording section).
struct RecordingSettings
{
    juce::String inputId;
    bool monitoring = false;       // hear the input through the speakers
};

// The "Show details" box. Advanced info, hidden by default.
struct TrackDetails
{
    juce::String pluginName;
    juce::String outputName;
    juce::StringArray midiInputs;
};

struct TransportState
{
    bool playing = false;
    bool recording = false;
    double recordStartBeats = 0.0; // where the current take started
    bool looping = false;
    bool metronome = false;
    double positionBeats = 0.0;
    double positionSeconds = 0.0;
    int bar = 1;                   // 1-based
    int beat = 1;                  // 1-based, within the bar
    double bpm = 120.0;
    int timeSigNumerator = 4;
    int timeSigDenominator = 4;
    double loopStartBeats = 0.0;
    double loopEndBeats = 0.0;
};

struct SystemStats
{
    float cpuPercent = 0.0f;
    double sampleRate = 0.0;
    double latencyMs = 0.0;
};

} // namespace ikaros
