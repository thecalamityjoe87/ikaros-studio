#pragma once

#include <cstdint>

// The DSP of Guitarix's amp (trunk/src/LV2/gx_amp.lv2), without its LV2
// wrapper: the tube preamp/power amp models, the tone stacks and the
// cabinet impulse responses. Used by Ikaros Studio's Amp effect
// (src/engine/AmpPlugin.h).
//
// The Faust-generated code is compiled in GuitarixAmp.cpp, in a namespace of
// its own, so its macros and globals (min/max, the tube tables) never meet
// JUCE or Tracktion. All the rest of the app sees is this header.
//
// A model or tone stack is a PluginLV2 (see ../src/LV2/DSP/gx_pluginlv2.h),
// driven the same way as the pedals' DSPs (see
// external/gxplugins/ikaros/GxDsp.h): set_samplerate, connect_ports with a
// pointer to each control's float, then mono_audio. None of them allocate
// buffers (activate_plugin is null), and mono_audio never allocates or locks.
#include "../src/LV2/DSP/gx_pluginlv2.h"   // the same as the pedals' copy (same include guard)

namespace ikaros::gx::amp
{

/** The amp models we use, named after their Guitarix files
    (faust-generated/gxampN.cc), with the tubes Guitarix's own amp lists
    them under. Every model has the same controls (the ports below). Which
    voice uses which is in src/engine/AmpPlugin.cpp. */
enum class Model
{
    gxamp,      // 12ax7
    gxamp5,     // pre 12ax7 / master 6V6
    gxamp7,     // pre 12ax7 / push-pull 6V6
    gxamp8,     // pre 12AU7 / push-pull 6V6
    gxamp9,     // 12ax7 feedback
    gxamp16     // pre 12AT7 / push-pull 6V6
};

PluginLV2* createModel (Model);

/** The tone stacks we use (faust-generated/tonestack_*.cc). */
enum class ToneStack { fenderDefault, bassman, ac30, jcm800, engl, ampeg };

PluginLV2* createToneStack (ToneStack);

// The LV2 port numbers (gx_amp.lv2/gxamp.h), with the ranges from its
// gx_amp.ttl:
//   gain1      the amp model's output level, -20..+20 dB (Guitarix's MasterGain)
//   pregain    into the first tube, -20..+20 dB
//   wetDry     0..100: how much of the signal goes through the tubes' clipping
//              path (Guitarix's "Distortion"); the rest stays cleaner
//   drive      0.01..1: the tube stages' drive (more gain and feedback)
//   middle, bass, treble   0..1: the tone stack's knobs
//   highGain   0 or 1: an extra, hotter output path in the model
constexpr std::uint32_t gain1 = 0, pregain = 1, wetDry = 2, drive = 3,
                        middle = 4, bass = 5, treble = 6, highGain = 18;

/** One of Guitarix's built-in cabinet impulse responses (DSP/cab_data.cc),
    mono, at `sampleRate` (48 kHz for all but one). */
struct CabinetIR
{
    const char* guitarixName;   // e.g. "4x12", "Twin": its name in Guitarix (never shown to users)
    int length;                 // samples
    std::uint32_t sampleRate;
    const float* samples;
};

/** The cabinets, in Guitarix's order (its c_model numbers). */
constexpr int numCabinets = 18;

/** Index 0..numCabinets-1. */
CabinetIR getCabinet (int index);

} // namespace ikaros::gx::amp
