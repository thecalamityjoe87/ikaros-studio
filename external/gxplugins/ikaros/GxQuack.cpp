// GxQuack DSP, compiled without its LV2 wrapper (see GxDsp.h).

#include "GxDsp.h"
#include "GxFaustSupport.h"

// The generated code goes in a namespace of its own, so nothing in it (its
// PortIndex enum, its tables) can clash with another pedal's.
namespace ikaros::gx::quack
{
    // The LV2 ports, numbered as in the original's plugin/gx_quack.h. The DSP's
    // connect() switches on these names.
    enum PortIndex
    {
        EFFECTS_OUTPUT,
        EFFECTS_INPUT,
        BYPASS,
        DEPTH,
        DRIVE,
        GAIN,
        MODE,
        PEAK,
        RANGE,
        TONE,
    };

    #include "../GxQuack/dsp/quack.cc"

    static_assert (DEPTH == depth && DRIVE == drive && GAIN == gain && MODE == mode && PEAK == peak && RANGE == range && TONE == tone,
                   "GxDsp.h's port numbers are out of date");

    const DspType dsp { quack::plugin, copyStateOf<quack::Dsp> };
}
