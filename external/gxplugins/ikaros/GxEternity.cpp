// GxEternity DSP, compiled without its LV2 wrapper (see GxDsp.h).

#include "GxDsp.h"
#include "GxFaustSupport.h"

// The generated code goes in a namespace of its own, so nothing in it (its
// PortIndex enum, its tables) can clash with another pedal's.
namespace ikaros::gx::eternity
{
    // The LV2 ports, numbered as in the original's plugin/gx_eternity.h. The DSP's
    // connect() switches on these names.
    enum PortIndex
    {
        EFFECTS_OUTPUT,
        EFFECTS_INPUT,
        BYPASS,
        DRIVE,
        GLASS,
        LEVEL,
    };

    #include "../GxEternity/dsp/eternity.cc"

    static_assert (DRIVE == drive && GLASS == glass && LEVEL == level,
                   "GxDsp.h's port numbers are out of date");

    const DspType dsp { eternity::plugin, copyStateOf<eternity::Dsp> };
}
