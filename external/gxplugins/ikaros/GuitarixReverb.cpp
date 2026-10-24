// Guitarix's gx_reverb DSP, compiled without its LV2 wrapper (see GxDsp.h).

#include "GxDsp.h"

// This newer Faust code calls std::min/std::max, so it mustn't get the
// old code's min/max macros.
#define IKAROS_GX_STD_MINMAX
#include "GxFaustSupport.h"

// The generated code goes in a namespace of its own, so nothing in it (its
// PortIndex enum, its tables) can clash with another pedal's.
namespace ikaros::gx::reverb
{
    // The LV2 ports, numbered as in the original's trunk/src/LV2/gx_reverb.lv2/gx_reverb.h. The DSP's
    // connect() switches on these names.
    enum PortIndex
    {
        WET_DRY,
        LFOFREQ,
        ROOMSIZE,
        DAMP,
        INVERT,
        EFFECTS_OUTPUT,
        EFFECTS_OUTPUT1,
        EFFECTS_INPUT,
        EFFECTS_INPUT1,
    };

    #include "../guitarix/dsp/stereoverb.cc"

    static_assert (WET_DRY == wetDry && LFOFREQ == lfo && ROOMSIZE == roomSize && DAMP == damp && INVERT == invert,
                   "GxDsp.h's port numbers are out of date");

    // Stereo, and its buffers are its own (activate_plugin), so never copied.
    const DspType dsp { stereoverb::plugin, nullptr };
}
