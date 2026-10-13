#pragma once

#include "engine/EngineTypes.h"
#include <optional>

// Plain structs about recording health and the audio device, for the UI.
// (Kept out of EngineTypes.h, which nearly every file includes.)

namespace ikaros
{

/** An input level at or above this (dB) counts as clipping: the converter
    ran out of room and the waveform got flattened. */
constexpr float clipLevelDb = -0.05f;

/** An input meter's reading: the level since the last reading, and whether
    the input has clipped since the clip light was last cleared. */
struct InputMeter
{
    StereoLevel level;
    bool clipped = false;
};

/** Why a take ended. */
enum class RecordingStopReason
{
    user,                   // Stop or Record was pressed (the normal case)
    deviceStopped,          // the audio device stopped responding or was closed (unplugged?)
    deviceChanged,          // a different audio device (or buffer size) was picked mid-take
    sampleRateChanged,      // the device switched sample rate mid-take
    diskFull,               // free disk space ran low; the take was stopped before it filled up
    diskWriteFailed,        // the take couldn't be written (Tracktion stopped it)
    notStarted,             // Record was pressed but there's too little disk space to start
    loopTooShort,           // Record was pressed with the loop on, but the loop is under 2 seconds
    noTrackArmed            // Record was pressed with no track armed, and "arm automatically" is off
};

/** How a take went, handed to the UI once after it ends. */
struct RecordingReport
{
    RecordingStopReason reason = RecordingStopReason::user;
    bool audio = false;     // an audio take (MIDI takes can't clip or drop out)
    bool clipped = false;   // the input clipped during the take
    int dropouts = 0;       // gaps in the audio (the computer didn't keep up)
    double seconds = 0.0;   // how long it recorded for
};

/** The audio device as the audio settings show it. Latencies are what the
    driver reports; on Linux they're an estimate (see RecordingGuard.h). */
struct AudioDeviceStatus
{
    bool running = false;
    juce::String deviceName;
    double sampleRate = 0.0;
    int bufferSize = 0;                 // samples per block
    double bufferMs = 0.0;
    double inputLatencyMs = 0.0;
    double outputLatencyMs = 0.0;

    /** Delay between a sound reaching the input and coming out of the
        speakers while monitoring: input + output latency, plus any delay
        added by plug-ins. */
    double monitoringLatencyMs = 0.0;

    /** How much earlier recordings are placed to line up with the music. */
    double recordingCompensationMs = 0.0;

    /** Dropouts since the device started. */
    int dropouts = 0;
};

} // namespace ikaros
