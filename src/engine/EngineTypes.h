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
#include <set>
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

    // C++20: "= default" makes == compare every member in turn.
    bool operator== (const NoteInfo&) const = default;
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
    // the start or end of its recording (unless it's looped); a MIDI region
    // can grow freely.
    double minStartBeat = 0.0;
    double maxEndBeat = std::numeric_limits<double>::max();

    // Looping: dragging the top-right corner repeats the region's content.
    // A looped region's notes and peaks are one loop's worth, starting at the
    // loop's first beat, and the region starts loopOffsetBeats into the loop.
    double loopLengthBeats = 0.0;  // 0 = not looped
    double loopOffsetBeats = 0.0;

    double getEndBeat() const noexcept      { return startBeat + lengthBeats; }
    bool isLooped() const noexcept          { return loopLengthBeats > 0.0; }

    /** The length of what the piano roll edits: one loop, or the whole region. */
    double getContentLengthBeats() const noexcept   { return isLooped() ? loopLengthBeats : lengthBeats; }

    bool operator== (const RegionInfo&) const = default;   // compares every member, notes and peaks included
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

// What changed in the session since the UI last heard, so it only re-reads
// what it needs: turning a knob shouldn't rebuild every region's waveform.
// Several changes can arrive together (the engine collects them for a moment
// before telling the UI), so these are flags rather than a single kind.
struct ModelChanges
{
    bool tracks = false;       // tracks added, removed, reordered, renamed, armed; a new sound or effect
    bool regions = false;      // regions added to or removed from tracks: re-read every region
    bool notes = false;        // notes inside MIDI regions (see changedRegions for which)
    bool mixer = false;        // volume, pan, sends and plug-in settings (knobs)
    bool transport = false;    // tempo, time signature or key, which move regions on the beat grid
    bool session = false;      // the session was replaced or saved, or something unknown: re-read everything

    // Regions whose own settings (position, length, name) or notes changed.
    // Only these need re-reading, unless `regions` is set too.
    std::set<RegionId> changedRegions;

    /** A change set that makes the UI re-read everything. */
    static ModelChanges everything()   { ModelChanges c; c.session = true; return c; }

    /** True if anything at all changed. */
    bool any() const
    {
        return tracks || regions || notes || mixer || transport || session || ! changedRegions.empty();
    }

    /** True if anything about this region (or the whole region list) changed. */
    bool affectsRegion (RegionId id) const
    {
        return session || regions || transport || changedRegions.count (id) > 0;
    }

    /** Adds another change set's flags and regions to this one. */
    void merge (const ModelChanges& other)
    {
        tracks |= other.tracks;
        regions |= other.regions;
        notes |= other.notes;
        mixer |= other.mixer;
        transport |= other.transport;
        session |= other.session;
        changedRegions.insert (other.changedRegions.begin(), other.changedRegions.end());
    }
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

/** How a new session starts: the templates on the session chooser's New page. */
enum class SessionTemplate
{
    empty,        // one instrument track
    keyboards,    // piano, electric piano and organ
    voice,        // a vocal track with a piano to sing along to
    beats,        // drum kit, synth bass and pad
    songwriter    // drums, bass, guitar, piano and a vocal track
};

struct NewSessionSettings
{
    SessionTemplate kind = SessionTemplate::empty;
    double bpm = 120.0;
    int numerator = 4, denominator = 4;
    int keyRoot = 0;       // 0 = C ... 11 = B
    bool minor = false;
};

/** A session in the recently opened list, newest first. */
struct RecentSession
{
    juce::File file;
    juce::String name;
    juce::Time modified;
    juce::File preview;    // a picture of its tracks; missing for sessions saved before previews existed
};

struct SystemStats
{
    float cpuPercent = 0.0f;
    double sampleRate = 0.0;
    double latencyMs = 0.0;
};

} // namespace ikaros
