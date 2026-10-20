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

    PluginLV2* createInput()     { return vstb_p1::plugin(); }
    PluginLV2* createClip()      { return vstb_clip::plugin(); }
    PluginLV2* createOutput()    { return vstb_p2::plugin(); }
}
