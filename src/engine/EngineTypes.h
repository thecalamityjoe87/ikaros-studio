#pragma once

// Plain data types the UI uses to talk to the engine layer.
//
// The UI never sees a tracktion::Edit, Track or Clip. Instead the engine layer
// hands out these small value types (snapshots of engine state), and the UI
// refers to tracks and regions by ID. That keeps src/ui/ free of Tracktion
// headers and makes it obvious where engine work happens.

#include <juce_graphics/juce_graphics.h>
#include <cstdint>
#include <limits>
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

// Notes have no IDs of their own. A note is identified by its index in
// RegionInfo::notes, which stays valid until the region is next edited, so
// the piano roll re-reads the region after every change.
struct NoteInfo
{
    int pitch = 60;            // MIDI note number
    double startBeat = 0.0;    // relative to the start of the region
    double lengthBeats = 1.0;
    int velocity = 100;        // 1..127
};

struct RegionInfo
{
    RegionId id = 0;
    TrackId trackId = 0;
    juce::String name;
    double startBeat = 0.0;    // position on the timeline, in beats (quarter notes)
    double lengthBeats = 0.0;
    bool isMidi = true;
    std::vector<NoteInfo> notes;   // MIDI regions only
    std::vector<float> peaks;      // audio regions only: waveform, 0..1 per column across the region

    // How far the edges can be dragged out. An audio region can't grow past
    // the start or end of its recording; a MIDI region can grow freely.
    double minStartBeat = 0.0;
    double maxEndBeat = std::numeric_limits<double>::max();

    double getEndBeat() const noexcept    { return startBeat + lengthBeats; }
};

// Where a region should end up after a move: a track and a start beat.
struct RegionPlacement
{
    RegionId id = 0;
    TrackId track = 0;
    double startBeat = 0.0;
};

// A changed note for TrackController::changeNotes.
struct NoteChange
{
    int index = 0;             // which note (its index in RegionInfo::notes)
    NoteInfo note;             // its new pitch, position, length and velocity
};

struct TrackInfo
{
    TrackId id = 0;
    int index = 0;
    TrackKind kind = TrackKind::instrument;
    juce::String name;
    juce::String soundName;        // e.g. "Soft Piano" (the sound shown in the library)
    juce::String soundId;          // the library sound (SoundInfo::id), empty for audio tracks
    juce::String category;         // library category, used to pick the icon
    juce::String icon;             // the icon the user chose ("guitar", "drums"...), or empty to follow the category
    juce::Colour colour;
    bool muted = false;
    bool solo = false;
    bool silencedBySolo = false;   // another track is soloed, so this one is quiet
    bool armed = false;            // record-enabled
    bool monitoring = false;       // audio tracks: the input is heard live
    float volume = 0.75f;          // fader position 0..1 (Tracktion's slider curve)
    float volumeDb = 0.0f;
    float pan = 0.0f;              // -1 (left) .. 0 (centre) .. 1 (right)
    float reverbSend = 0.0f;       // sends to the shared buses, 0..1 (see SendBuses)
    float delaySend = 0.0f;
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
    bool isThirdParty = false;     // a VST3/AU/LV2 plug-in: it has its own window
    bool missing = false;          // a third-party plug-in that isn't installed (or failed to load)
    juce::String manufacturer;     // third-party plug-ins only
};

// An effect the user can add from the "+ Add effect" menu.
struct EffectType
{
    juce::String type;             // Tracktion's plugin type, or "plugin:" + PluginInfo::id
    juce::String name;             // what the user sees
    juce::String category;         // menu group, e.g. "Space", or the maker of a third-party plug-in
    bool isThirdParty = false;
};

// A sound in the library: something an instrument track can play.
struct SoundInfo
{
    juce::String id;               // saved with the track, e.g. "synth:Warm Pad" or "sampler:Ikaros Basics/Soft Piano"
    juce::String name;             // what the user sees, e.g. "Warm Pad"
    juce::String category;         // library category, e.g. "Keyboards"
    juce::String detail;           // small text on the right of the row, e.g. "Sampled"
};

// A loop or audio file in the library, or a folder while browsing audio files.
struct LibraryFile
{
    juce::File file;
    juce::String name;
    juce::String detail;           // e.g. "120 · C" for a loop, or the file's length
    bool isFolder = false;
    double bpm = 0.0;              // loops: the tempo it was made at (0 = unknown)
};

// A third-party plug-in found by the plug-in scan.
struct PluginInfo
{
    juce::String id;               // stable identifier (format, name and IDs)
    juce::String name;
    juce::String manufacturer;
    juce::String format;           // "VST3", "LV2" or "AudioUnit"
    bool isInstrument = false;
};

// Progress of the background plug-in scan.
struct PluginScanStatus
{
    bool scanning = false;
    int done = 0, total = 0;       // plug-in files checked so far, out of total
    juce::String current;          // the file being checked now
    int numFound = 0;              // plug-ins added by this scan
    juce::StringArray failed;      // files that crashed or hung during this scan
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
    bool countIn = false;          // one bar of clicks before recording starts
    bool countingIn = false;       // recording has started but the count-in bar is still playing
    double positionBeats = 0.0;
    double positionSeconds = 0.0;
    int bar = 1;                   // 1-based
    int beat = 1;                  // 1-based, within the bar
    double bpm = 120.0;
    int timeSigNumerator = 4;
    int timeSigDenominator = 4;
    int keyRoot = 0;               // 0 = C, 1 = C#/Db ... 11 = B
    bool keyMinor = false;
    double loopStartBeats = 0.0;
    double loopEndBeats = 0.0;
};

/** A key's name as the app shows it, e.g. "Eb maj" or "F# min". */
inline juce::String getKeyName (int root, bool minor)
{
    static const char* const names[] { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
    return juce::String (names[((root % 12) + 12) % 12]) + (minor ? " min" : " maj");
}

struct SystemStats
{
    float cpuPercent = 0.0f;
    double sampleRate = 0.0;
    double latencyMs = 0.0;
};

} // namespace ikaros
