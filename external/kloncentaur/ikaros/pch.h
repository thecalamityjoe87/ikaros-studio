#pragma once

// Ikaros Studio's stand-in for ChowCentaur's precompiled header (its
// ChowCentaur/pch.h), which every file of the circuit model includes as
// <pch.h>. The original pulls in the whole plugin's JUCE modules, RTNeural
// and toms917; the circuit model only needs JUCE's DSP module and four parts
// of chowdsp_utils, all in ../chowdsp_utils.

#include <juce_dsp/juce_dsp.h>

// With JUCE included, chowdsp's WDF headers say `using namespace SIMDUtils`
// (a part of chowdsp_utils we don't need). An empty one is enough for them.
namespace chowdsp { namespace SIMDUtils {} }

#include "../chowdsp_utils/DSP/WDF/wdf.h"
#include "../chowdsp_utils/DSP/WDF/wdf_t.h"
#include "../chowdsp_utils/DSP/Filters/chowdsp_IIRFilter.h"
#include "../chowdsp_utils/DSP/Filters/chowdsp_BilinearUtils.h"

// The model's code uses JUCE's names without juce::, as JuceHeader.h allows.
// Only KlonCentaurDsp.cpp includes this.
using namespace juce;
