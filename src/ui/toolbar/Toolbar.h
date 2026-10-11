#pragma once

#include "ui/widgets/IconButton.h"
#include "ui/widgets/LevelMeter.h"
#include "ui/toolbar/TransportBar.h"

namespace ikaros
{

class AudioEngine;

//==============================================================================
/**
    The 64px toolbar: library toggle, "+ New Track", Undo and Redo on the left, the
    TransportBar in the centre, master meters and CPU / sample-rate / latency on
    the right.

    Left-side buttons fire application commands, so they behave exactly like
    the matching menu items and keyboard shortcuts.
*/
class Toolbar  : public juce::Component
{
public:
    Toolbar (AudioEngine&, juce::ApplicationCommandManager&);

    /** Called by the owner's ~30 Hz timer. */
    void update (bool libraryVisible);

    /** "+ New Track" was clicked: the owner shows the track-type menu. */
    std::function<void (juce::Component*)> onNewTrackRequested;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    AudioEngine& engine;
    juce::ApplicationCommandManager& commands;

    IconButton libraryButton  { "Show or hide the sound library", Icon::library };
    IconButton newTrackButton { "New Track", Icon::plus, "New Track" };
    IconButton undoButton     { "Undo", Icon::undo };
    IconButton redoButton     { "Redo", Icon::redo };
    TransportBar transport;
    LevelMeter masterMeter;

    juce::String statsLine1, statsLine2;
    juce::Rectangle<int> masterArea, statsArea;
    int statsCountdown = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Toolbar)
};

} // namespace ikaros
