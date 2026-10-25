// GxSuppaToneBender DSP, compiled without its LV2 wrapper (see GxDsp.h).

#include "GxDsp.h"
#include "GxFaustSupport.h"

// The generated code goes in a namespace of its own, so nothing in it (its
// PortIndex enum, its tables) can clash with another pedal's.
namespace ikaros::gx::tonebender
{
    // The LV2 ports, numbered as in the original's plugin/gx_vstb.h. The DSP's
    // connect() switches on these names.
    enum PortIndex
    {
        EFFECTS_OUTPUT,
        EFFECTS_INPUT,
        FUZZ,
        TONE,
        VOLUME,
    };

    #include "../GxSuppaToneBender/dsp/vstb_p1.cc"
    #include "../GxSuppaToneBender/dsp/vstb_clip.cc"
    #include "../GxSuppaToneBender/dsp/vstb_p2.cc"

    static_assert (FUZZ == fuzz && TONE == tone && VOLUME == volume,
                   "GxDsp.h's port numbers are out of date");

    const DspType inputDsp { vstb_p1::plugin, copyStateOf<vstb_p1::Dsp> };
    const DspType clipDsp { vstb_clip::plugin, copyStateOf<vstb_clip::Dsp> };
    const DspType outputDsp { vstb_p2::plugin, copyStateOf<vstb_p2::Dsp> };
}
