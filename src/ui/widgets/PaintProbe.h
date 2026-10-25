#pragma once

#include "engine/PerfMonitor.h"
#include <juce_gui_basics/juce_gui_basics.h>

/*  Debug-only paint timing for the performance monitor (IKAROS_PERF=1, see
    engine/PerfMonitor.h).

    Put IKAROS_PERF_PAINT ("Name"); first thing in a paint() method. It times
    the method (not the children JUCE paints after it) and counts the calls.
    With IKAROS_PERF unset it costs one bool test; with CMake's
    IKAROS_PERF_TOOLS=OFF it compiles to nothing.

    Paint only ever runs on the message thread, so nothing here needs locks.
*/

#ifndef IKAROS_PERF_TOOLS
 #define IKAROS_PERF_TOOLS 1
#endif

namespace ikaros::paintprobe
{

/** Times one paint() call (use the macro below). */
class Scope
{
public:
    explicit Scope (const char* name) noexcept;
    ~Scope();

private:
    const char* name;          // a string literal, so the pointer itself is a fine map key
    juce::int64 start = 0;     // 0 = not timing (monitor off)
};

/** Call at the end of the top-level component's paintOverChildren: everything
    painted since the first timed paint after the previous call is one frame. */
void frameEnd();

/** The figures since the last call (the performance monitor asks once a second). */
PerfMonitor::UiSample takeSample();

} // namespace ikaros::paintprobe

#if IKAROS_PERF_TOOLS
 // JUCE_JOIN_MACRO pastes the line number on, so the variable's name is unique.
 #define IKAROS_PERF_PAINT(name) const ::ikaros::paintprobe::Scope JUCE_JOIN_MACRO (ikarosPaintProbe_, __LINE__) (name)
#else
 #define IKAROS_PERF_PAINT(name)
#endif
