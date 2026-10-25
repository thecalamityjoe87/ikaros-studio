// GxGuvnor DSP, compiled without its LV2 wrapper (see GxDsp.h).

#include "GxDsp.h"
#include "GxFaustSupport.h"

// The generated code goes in a namespace of its own, so nothing in it (its
// PortIndex enum, its tables) can clash with another pedal's.
namespace ikaros::gx::guvnor
{
    // The LV2 ports, numbered as in the original's plugin/gx_guvnor.h. The DSP's
    // connect() switches on these names.
    enum PortIndex
    {
        EFFECTS_OUTPUT,
        EFFECTS_INPUT,
        BASS,
        GAIN,
        LEVEL,
        MID,
        TREBLE,
    };

    #include "../GxGuvnor/dsp/guvnor.cc"

    static_assert (BASS == bass && GAIN == gain && LEVEL == level && MID == mid && TREBLE == treble,
                   "GxDsp.h's port numbers are out of date");

    const DspType dsp { guvnor::plugin, copyStateOf<guvnor::Dsp> };
}
