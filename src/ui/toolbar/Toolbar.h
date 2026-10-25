#pragma once

#include "ui/widgets/IconButton.h"
#include "ui/widgets/MeterSlider.h"
#include "ui/toolbar/TransportBar.h"

namespace ikaros
{

class AudioEngine;

//==============================================================================
/**
    The 64px toolbar: library toggle, Undo and Redo on the left, the
    TransportBar in the centre, and on the right the master volume (a
    speaker icon, "Master" with a live dB readout, and one slider whose
    track is the level meter), then CPU / sample-rate / latency and the RT
    badge, right-aligned. The CPU readout warns about load the average hides:
    amber when a single block came close to running out of time, and an xrun
    count, red for a moment after each one.

    New tracks are added from the "Add track" row under the track list, the
    Track menu or its shortcuts, so there's no button for it here.

    Left-side buttons fire application commands, so they behave exactly like
    the matching menu items and keyboard shortcuts.
*/
class Toolbar  : public juce::Component,
                 public juce::TooltipClient   // the CPU readout's tooltip (see getTooltip)
{
public:
    Toolbar (AudioEngine&, juce::ApplicationCommandManager&);

    /** Called by the owner's ~30 Hz timer. */
    void update (bool libraryVisible);

    /** For hints about recording to point at. */
    juce::Component& getRecordButton() noexcept     { return transport.getRecordButton(); }
    LcdDisplay& getLcd() noexcept                   { return transport.getLcd(); }

    void paint (juce::Graphics&) override;
    void resized() override;
    juce::String getTooltip() override;

private:
    AudioEngine& engine;
    juce::ApplicationCommandManager& commands;

    IconButton libraryButton  { "Show or hide the sound library", Icon::library };
    IconButton undoButton     { "Undo", Icon::undo };
    IconButton redoButton     { "Redo", Icon::redo };
    TransportBar transport;
    MeterSlider masterVolume;   // the master level and volume in one

    juce::String masterDbText;  // e.g. "−3.0 dB", beside "Master"
    juce::String cpuText, xrunText, statsLine2;     // "CPU 34%", "2 xruns" (once there's been one), "48 kHz · 5 ms"
    int xruns = 0;
    double lastBusyMs = -1.0e9, lastXrunMs = -1.0e9;   // juce::Time::getMillisecondCounterHiRes
    bool busy = false;          // the CPU text is amber: a block lately took 90% of its time or more
    bool xrunShowing = false;   // the whole first line is red, for a while after an xrun
    bool realtimeBadge = false;     // "RT" beside the CPU readout
    bool realtimeKnown = false;     // false where the OS looks after it (no tooltip line then)
    juce::Rectangle<int> speakerArea, masterArea, statsArea;
    int statsCountdown = 0;

    juce::Rectangle<int> getRealtimeBadgeArea() const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Toolbar)
};

} // namespace ikaros
