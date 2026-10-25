#include "ui/widgets/PerfOverlay.h"
#include "ui/widgets/PaintProbe.h"

namespace ikaros
{

PerfOverlay::PerfOverlay (const PerfMonitor& m) : monitor (m)
{
    setInterceptsMouseClicks (false, false);
    setSize (430, 190);
}

void PerfOverlay::visibilityChanged()
{
    if (isVisible())
        startTimerHz (4);
    else
        stopTimer();
}

void PerfOverlay::timerCallback()
{
    const auto s = monitor.getSnapshot();
    auto f = [] (double v, int places = 1) { return juce::String (v, places); };

    juce::StringArray next;
    next.add (f (s.sampleRate / 1000.0) + " kHz, " + juce::String (s.bufferSize) + " samples"
              + (s.audioThreadRealtime ? ", RT" : ", not RT") + (s.playing ? ", playing" : ", stopped")
              + (s.burnPercent > 0 ? ", burn " + f (s.burnPercent, 0) + "%" : juce::String()));
    next.add ("Callback %  avg " + f (s.callback.avgPercent) + "  p99 " + f (s.callback.p99Percent)
              + "  max " + f (s.callback.maxPercent) + "  (meter " + f (s.juceMeterPercent, 0) + ")");
    next.add ("Run        avg " + f (s.run.avgPercent) + "  p99 " + f (s.run.p99Percent)
              + "  max " + f (s.run.maxPercent));
    next.add ("Over budget " + juce::String (s.callback.overBudget) + "/s (run " + juce::String (s.run.overBudget)
              + ")  late " + juce::String (s.callback.lateStarts) + "  xruns " + juce::String (s.deviceXruns)
              + "  slow cores " + juce::String (s.callback.onSlowCores));
    next.add ("Tracktion " + f (s.tracktionAvgMs, 2) + " / " + f (s.tracktionMaxMs, 2) + " ms, plug-ins "
              + f (s.pluginsPercent) + "%, threads " + juce::String (s.audioThreadsConfigured));
    next.add ("CPU audio " + f (s.audioThreadCpu, 0) + "%  UI " + f (s.messageThreadCpu, 0) + "%  other "
              + f (s.otherThreadsCpu, 0) + "%  all " + f (s.processCpu, 0) + "%");
    next.add ("Paint " + f (s.ui.paintMsPerSecond) + " ms/s, " + juce::String (s.ui.paintCalls) + " calls, "
              + juce::String (s.ui.frames) + " frames (max " + f (s.ui.maxFrameMs) + " ms)");
    next.add ("Memory " + f (s.rssMb, 0) + " MB, " + juce::String (s.openFiles) + " files, "
              + juce::String (s.threads) + " threads");

    for (int i = 0; i < juce::jmin (3, (int) s.plugins.size()); ++i)
        next.add ("  " + s.plugins[(size_t) i].track + " / " + s.plugins[(size_t) i].plugin + "  "
                  + f (s.plugins[(size_t) i].avgPercent) + "%");

    if (next != lines)
    {
        lines = next;
        repaint();
    }
}

void PerfOverlay::paint (juce::Graphics& g)
{
    IKAROS_PERF_PAINT ("PerfOverlay");

    // Fixed colours: a debugging aid, so it doesn't follow the theme.
    g.setColour (juce::Colours::black.withAlpha (0.78f));
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);

    g.setColour (juce::Colours::white);
    g.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 12.0f, juce::Font::plain));

    auto area = getLocalBounds().reduced (8, 6);

    for (auto& line : lines)
        g.drawText (line, area.removeFromTop (16), juce::Justification::centredLeft, false);
}

} // namespace ikaros
