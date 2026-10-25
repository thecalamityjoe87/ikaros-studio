#pragma once

#include "engine/PerfMonitor.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace ikaros
{

/*  The performance monitor's live numbers (IKAROS_PERF=1 only), in a small
    box in the window's top-right corner. Ctrl+Shift+P shows and hides it
    (see MainComponent). It ignores the mouse, and only repaints 4 times a
    second while it's showing.
*/
class PerfOverlay  : public juce::Component,
                     private juce::Timer
{
public:
    explicit PerfOverlay (const PerfMonitor&);

    void paint (juce::Graphics&) override;
    void visibilityChanged() override;

private:
    const PerfMonitor& monitor;
    juce::StringArray lines;

    void timerCallback() override;
};

} // namespace ikaros
