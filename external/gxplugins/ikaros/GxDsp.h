#pragma once

#include <cstdint>

// The DSP of the GxPlugins (and Guitarix) effects we use, without their LV2
// wrappers.
//
// Each one's Faust-generated code (../<Pedal>/dsp/*.cc) is compiled in a
// file of its own here (e.g. GxSD1.cpp), which sets up the few macros it
// expects, the way its LV2 wrapper does. Those macros include function-like
// min() and max(), which would break any C++ header included after them
// (std::max, JUCE...). So the generated code never meets JUCE or Tracktion:
// all the rest of the app sees is this header and gx_pluginlv2.h, neither
// of which defines any such macros.
//
// A DSP is a PluginLV2 (see ../GxSD1/dsp/gx_pluginlv2.h): a struct of plain
// function pointers. Ikaros's GxPedalPlugin (src/engine/PortedPedals.h)
// drives one through them:
//
//   set_samplerate (rate, dsp)         sets up the filters for the rate (and clears)
//   activate_plugin (true/false, dsp)  if not null: allocates/frees its buffers
//   connect_ports (port, &value, dsp)  where to read a control from, by LV2 port number
//   clear_state (dsp)                  silences it (e.g. after a jump in playback)
//   mono_audio (count, in, out, dsp)   processes one channel (most of them)
//   stereo_audio (count, inL, inR, outL, outR, dsp)   or both (Spring Tank)
//   delete_instance (dsp)              frees it
//
// A control is read through its pointer at the start of each call, so the
// caller just writes the float before calling it. Every control of a pedal
// is connected to each of its DSPs; a DSP ignores the ports it doesn't use.
//
// The port numbers below are the original LV2 plugins' (their plugin/*.h),
// and the ranges in the comments are theirs too.
#include "../GxSD1/dsp/gx_pluginlv2.h"   // the same in every GxPlugins and Guitarix repository

namespace ikaros::gx
{

/** One of the DSPs below: makes new copies of it, and copies the whole
    state of one copy (filters, delay lines, LFO phase) into another.
    GxPedalPlugin runs a mono DSP's second copy only while the two channels
    differ, and copies the first one's state into it when they start to. */
struct DspType
{
    PluginLV2* (*create)();
    void (*copyState) (const PluginLV2& from, PluginLV2& to);   // null if it can't be copied
};

/** A DspType's copyState for a generated DSP class: plain assignment, which
    copies every member. Right for a DSP whose state is all in its own
    members: every one here without an activate_plugin (the ones with one
    allocate buffers that two copies would then share). */
template <typename GeneratedDsp>
void copyStateOf (const PluginLV2& from, PluginLV2& to)
{
    static_cast<GeneratedDsp&> (to) = static_cast<const GeneratedDsp&> (from);
}

/** GxSD1 (Yellow Drive). All linear:
      drive  0..1, default 0.5
      level  -20..+4 dB, default -16
      tone   100..1000 Hz, default 400 */
namespace sd1
{
    extern const DspType dsp;
    constexpr std::uint32_t drive = 3, level = 4, tone = 5;
}

/** GxGuvnor (Stack Drive). All 0..1, default 0.5. */
namespace guvnor
{
    extern const DspType dsp;
    constexpr std::uint32_t bass = 2, gain = 3, level = 4, mid = 5, treble = 6;
}

/** GxHotBox (Hot Plate). All 0..1, default 0.5, but volume 0.25. */
namespace hotbox
{
    extern const DspType dsp;
    constexpr std::uint32_t bass = 3, gain = 4, treble = 5, volume = 6;
}

/** GxBottleRocket (Tube Drive). All 0..1, default 0.5. */
namespace bottlerocket
{
    extern const DspType dsp;
    constexpr std::uint32_t bass = 3, gain = 4, treble = 5, volume = 6;
}

/** GxEternity (Mellow Drive). All 0..1, default 0.5. Runs at twice the
    rate it's given in set_samplerate: the caller oversamples it 2x. */
namespace eternity
{
    extern const DspType dsp;
    constexpr std::uint32_t drive = 3, glass = 4, level = 5;
}

/** GxSunFace (Sun Fuzz). All 0..1, default 0.5. */
namespace sunface
{
    extern const DspType dsp;
    constexpr std::uint32_t drive = 3, input = 4, volume = 5;
}

/** GxSuppaToneBender (Vintage Fuzz): three DSPs in a row, the middle one
    (the clipping) oversampled 4x by the caller. All 0..1, default 0.5. */
namespace tonebender
{
    extern const DspType inputDsp, clipDsp, outputDsp;
    constexpr std::uint32_t fuzz = 2, tone = 3, volume = 4;
}

/** GxSuperFuzz (Super Fuzz): three DSPs in a row, the middle one
    oversampled 4x by the caller.
      balance   0..1, default 0.5 (the output level)
      expander  0..1, default 0.5 (the fuzz)
      tone      a switch: 0 = on (default), 1 = off
      trim      0..0.49, default 0.25 */
namespace superfuzz
{
    extern const DspType inputDsp, clipDsp, outputDsp;
    constexpr std::uint32_t balance = 2, expander = 3, tone = 4, trim = 5;
}

/** GxQuack (Envelope Wah).
      depth  0..1, default 1
      drive  a switch: 0 = off (default), 1 = on
      gain   -12..+6 dB, default 0
      mode   0 = low, 1 = high, 2 = original (default)
      peak   -6..20, default 5
      range  0 = low, 1 = high (default)
      tone   0 = off (default), 1 = low, 2 = high */
namespace quack
{
    extern const DspType dsp;
    constexpr std::uint32_t depth = 3, drive = 4, gain = 5, mode = 6, peak = 7, range = 8, tone = 9;
}

/** GxSlowGear (Slow Swell).
      downtime   0..1000 ms, default 5 (the fade out once it's quieter than the threshold)
      threshold  0..10, default 1 (in hundredths of full scale)
      uptime     0..1000 ms, default 100 (the swell in) */
namespace slowgear
{
    extern const DspType dsp;
    constexpr std::uint32_t downtime = 2, threshold = 3, uptime = 4;
}

/** Guitarix's gx_flanger (Jet Flanger).
      depth     0..5 ms, default 0.5 (the sweep)
      width     0..10 ms, default 5 (the delay it sweeps around)
      freq      0.05..10 Hz, logarithmic, default 0.2 (the sweep's speed)
      feedback  -0.99..0.99, default -0.707
      wet       0..100 %, default 100
      mix       -1..1, default 0 */
namespace flanger
{
    extern const DspType dsp;
    constexpr std::uint32_t depth = 0, width = 1, freq = 2, feedback = 3, wet = 4, mix = 5;
}

/** Guitarix's gx_reverb, a stereo reverb (Spring Tank).
      wetDry    0..100 %, default 50
      lfo       0.2..5 Hz, default 0.2
      roomSize  0..1, default 0.5
      damp      0..1, default 0.2
      invert    0 = linear (default), 1 = ping-pong */
namespace reverb
{
    extern const DspType dsp;
    constexpr std::uint32_t wetDry = 0, lfo = 1, roomSize = 2, damp = 3, invert = 4;
}

} // namespace ikaros::gx
