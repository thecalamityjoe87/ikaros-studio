// What the GxPlugins' Faust-generated DSP expects to be defined before it's
// included, the same as each pedal's LV2 wrapper (plugin/*.cpp) sets up, but
// portable: the wrappers use GCC-only attributes that MSVC rejects.
//
// Include this, then gx_pluginlv2.h, then the pedal's .cc, and nothing else
// after it (see GxDsp.h for why). Not a normal header: no include guard, as
// the macros are only ever wanted once, in one file per pedal.

// The standard headers the generated code uses go first, before min/max
// become macros below.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <math.h>        // fabs, pow, exp, tan... without std::, as the generated code calls them

// The wrappers put real-time code and data in their own linker sections
// (a GCC attribute, for locking them in memory). Not needed here.
#define __rt_func
#define __rt_data

#define FAUSTFLOAT float

// The generated code marks a DSP's name for translation; we don't show it.
#ifndef N_
 #define N_(text) (text)
#endif

#if defined (_MSC_VER)
 #define always_inline __forceinline
#else
 #define always_inline inline __attribute__((always_inline))
#endif

// Macros, not std::min/max: the older generated code mixes int and
// unsigned arguments, which the std versions don't accept. Newer Faust code
// calls std::min/std::max itself, and its file defines IKAROS_GX_STD_MINMAX
// so these don't break those calls.
#ifndef IKAROS_GX_STD_MINMAX
 #define max(x, y) (((x) > (y)) ? (x) : (y))
 #define min(x, y) (((x) < (y)) ? (x) : (y))
#endif

template <int N> inline float  faustpower (float x)    { return powf (x, (float) N); }
template <int N> inline double faustpower (double x)   { return pow (x, (double) N); }
template <int N> inline int    faustpower (int x)      { return faustpower<N / 2> (x) * faustpower<N - N / 2> (x); }
template <>      inline int    faustpower<0> (int)     { return 1; }
template <>      inline int    faustpower<1> (int x)   { return x; }
