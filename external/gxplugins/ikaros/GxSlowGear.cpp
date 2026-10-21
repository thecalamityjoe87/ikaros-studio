// GxSlowGear DSP, compiled without its LV2 wrapper (see GxDsp.h).

#include "GxDsp.h"
#include "GxFaustSupport.h"

// The generated code goes in a namespace of its own, so nothing in it (its
// PortIndex enum, its tables) can clash with another pedal's.
namespace ikaros::gx::slowgear
{
    // The LV2 ports, numbered as in the original's plugin/gx_slowgear.h. The DSP's
    // connect() switches on these names.
    enum PortIndex
    {
        EFFECTS_OUTPUT,
        EFFECTS_INPUT,
        DOWNTIME,
        TRESHOLD,
        UPTIME,
    };

    #include "../GxSlowGear/dsp/slowgear.cc"

    static_assert (DOWNTIME == downtime && TRESHOLD == threshold && UPTIME == uptime,
                   "GxDsp.h's port numbers are out of date");

    PluginLV2* create()    { return slowgear::plugin(); }
}
