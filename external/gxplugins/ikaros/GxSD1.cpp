// GxSD1's DSP, compiled without its LV2 wrapper (see GxDsp.h).

#include "GxDsp.h"
#include "GxFaustSupport.h"
#include "../GxSD1/dsp/gx_pluginlv2.h"

// The generated code goes in a namespace of its own, so nothing in it (its
// PortIndex enum, its tables) can clash with another pedal's.
namespace ikaros::gx::sd1
{
    // The LV2 ports, numbered as in the original's plugin/gx_sd1sim.h. The
    // DSP's connect() switches on these names.
    enum PortIndex
    {
        EFFECTS_OUTPUT,
        EFFECTS_INPUT,
        BYPASS,
        DRIVE,
        LEVEL,
        TONE,
    };

    #include "../GxSD1/dsp/sd1sim.cc"

    static_assert (DRIVE == drive && LEVEL == level && TONE == tone, "GxDsp.h's port numbers are out of date");

    PluginLV2* create()    { return sd1sim::plugin(); }
}
