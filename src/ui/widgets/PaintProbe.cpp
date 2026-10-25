#include "ui/widgets/PaintProbe.h"

#include <map>

namespace ikaros::paintprobe
{

namespace
{
    struct Totals
    {
        int calls = 0;
        double ms = 0;
    };

    // All message thread, so plain variables are fine.
    std::map<const char*, Totals> totals;
    juce::int64 frameStart = 0;
    int frames = 0;
    double maxFrameMs = 0;

    double ticksToMs (juce::int64 ticks)
    {
        return (double) ticks * 1000.0 / (double) juce::Time::getHighResolutionTicksPerSecond();
    }
}

Scope::Scope (const char* n) noexcept : name (n)
{
    if (PerfMonitor::isEnabled())
    {
        start = juce::Time::getHighResolutionTicks();

        if (frameStart == 0)
            frameStart = start;
    }
}

Scope::~Scope()
{
    if (start == 0)
        return;

    auto& t = totals[name];
    ++t.calls;
    t.ms += ticksToMs (juce::Time::getHighResolutionTicks() - start);
}

void frameEnd()
{
    if (! PerfMonitor::isEnabled() || frameStart == 0)
        return;

    ++frames;
    maxFrameMs = juce::jmax (maxFrameMs, ticksToMs (juce::Time::getHighResolutionTicks() - frameStart));
    frameStart = 0;
}

PerfMonitor::UiSample takeSample()
{
    PerfMonitor::UiSample sample;
    sample.frames = frames;
    sample.maxFrameMs = maxFrameMs;

    std::vector<std::pair<const char*, Totals>> sorted (totals.begin(), totals.end());
    std::sort (sorted.begin(), sorted.end(), [] (auto& a, auto& b) { return a.second.ms > b.second.ms; });

    juce::StringArray top;

    for (auto& [name, t] : sorted)
    {
        sample.paintCalls += t.calls;
        sample.paintMsPerSecond += t.ms;

        if (top.size() < 5)
            top.add (juce::String (name) + " " + juce::String (t.calls) + "/s " + juce::String (t.ms, 1) + "ms");
    }

    sample.topPainters = top.joinIntoString ("; ");

    totals.clear();
    frames = 0;
    maxFrameMs = 0;
    return sample;
}

} // namespace ikaros::paintprobe
