// GxBottleRocket DSP, compiled without its LV2 wrapper (see GxDsp.h).

#include "GxDsp.h"
#include "GxFaustSupport.h"

// The generated code goes in a namespace of its own, so nothing in it (its
// PortIndex enum, its tables) can clash with another pedal's.
namespace ikaros::gx::bottlerocket
{
    // The LV2 ports, numbered as in the original's plugin/gx_bottlerocket.h. The DSP's
    // connect() switches on these names.
    enum PortIndex
    {
        EFFECTS_OUTPUT,
        EFFECTS_INPUT,
        BYPASS,
        BASS,
        GAIN,
        TREBLE,
        VOLUME,
    };

    #include "../GxBottleRocket/dsp/bottlerocket.cc"

    static_assert (BASS == bass && GAIN == gain && TREBLE == treble && VOLUME == volume,
                   "GxDsp.h's port numbers are out of date");

    PluginLV2* create()    { return bottlerocket::plugin(); }
}
