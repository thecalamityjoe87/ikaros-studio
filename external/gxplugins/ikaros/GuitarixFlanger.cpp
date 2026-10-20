// Guitarix's gx_flanger DSP, compiled without its LV2 wrapper (see GxDsp.h).

#include "GxDsp.h"

// This newer Faust code calls std::min/std::max, so it mustn't get the
// old code's min/max macros.
#define IKAROS_GX_STD_MINMAX
#include "GxFaustSupport.h"

// The generated code goes in a namespace of its own, so nothing in it (its
// PortIndex enum, its tables) can clash with another pedal's.
namespace ikaros::gx::flanger
{
    // The LV2 ports, numbered as in the original's trunk/src/LV2/gx_flanger.lv2/gx_flanger.h. The DSP's
    // connect() switches on these names.
    enum PortIndex
    {
        DEPTH,
        WIDTH,
        FREQ,
        FEEDBACK,
        WET,
        MIX,
        EFFECTS_OUTPUT,
        EFFECTS_INPUT,
    };

    #include "../guitarix/dsp/gx_flanger.cc"

    static_assert (DEPTH == depth && WIDTH == width && FREQ == freq && FEEDBACK == feedback && WET == wet && MIX == mix,
                   "GxDsp.h's port numbers are out of date");

    PluginLV2* create()    { return gx_flanger::plugin(); }
}
