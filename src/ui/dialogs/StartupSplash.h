#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace ikaros
{

//==============================================================================
/**
    The small window with the app's icon, name and version that shows while
    the app starts.

    Starting the engine (audio devices, plug-in list, sound library) blocks
    the message thread for a moment, so nothing could draw during it. The
    splash therefore goes on screen first, and calls onShown once it has
    drawn; the app does the slow part there.

    It always uses the icon's dark and amber colours, not the light or dark
    theme: the saved theme isn't known until the engine has loaded the
    preferences.
*/
class StartupSplash  : public juce::Component,
                       private juce::Timer
{
public:
    explicit StartupSplash (std::function<void()> onShown);

    /** How much longer the splash should stay up, so a fast start doesn't
        just flash it. Zero once it has been up long enough. */
    int getRemainingMilliseconds() const;

    void paint (juce::Graphics&) override;

private:
    void timerCallback() override;

    std::function<void()> onShown;
    juce::Image icon;
    juce::uint32 createdAt = 0, paintedAt = 0;   // juce::Time::getMillisecondCounter() values

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StartupSplash)
};

} // namespace ikaros
