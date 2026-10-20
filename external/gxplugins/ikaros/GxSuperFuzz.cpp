// GxSuperFuzz DSP, compiled without its LV2 wrapper (see GxDsp.h).

#include "GxDsp.h"
#include "GxFaustSupport.h"

// The generated code goes in a namespace of its own, so nothing in it (its
// PortIndex enum, its tables) can clash with another pedal's.
namespace ikaros::gx::superfuzz
{
    // The LV2 ports, numbered as in the original's plugin/gx_sfp.h. The DSP's
    // connect() switches on these names.
    enum PortIndex
    {
        EFFECTS_OUTPUT,
        EFFECTS_INPUT,
        BALANCE,
        EXPANDER,
        TONE,
        TRIM,
    };

    #include "../GxSuperFuzz/dsp/sfp_p1.cc"
    #include "../GxSuperFuzz/dsp/sfp_clip.cc"
    #include "../GxSuperFuzz/dsp/sfp_p2.cc"

    static_assert (BALANCE == balance && EXPANDER == expander && TONE == tone && TRIM == trim,
                   "GxDsp.h's port numbers are out of date");

    PluginLV2* createInput()     { return sfp_p1::plugin(); }
    PluginLV2* createClip()      { return sfp_clip::plugin(); }
    PluginLV2* createOutput()    { return sfp_p2::plugin(); }
}
