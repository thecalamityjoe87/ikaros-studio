// GxSunFace DSP, compiled without its LV2 wrapper (see GxDsp.h).

#include "GxDsp.h"
#include "GxFaustSupport.h"

// The generated code goes in a namespace of its own, so nothing in it (its
// PortIndex enum, its tables) can clash with another pedal's.
namespace ikaros::gx::sunface
{
    // The LV2 ports, numbered as in the original's plugin/gx_SunFace.h. The DSP's
    // connect() switches on these names.
    enum PortIndex
    {
        EFFECTS_OUTPUT,
        EFFECTS_INPUT,
        BYPASS,
        DRIVE,
        INPUT_,
        VOLUME,
    };

    #include "../GxSunFace/dsp/SunFace.cc"

    static_assert (DRIVE == drive && INPUT_ == input && VOLUME == volume,
                   "GxDsp.h's port numbers are out of date");

    PluginLV2* create()    { return SunFace::plugin(); }
}
