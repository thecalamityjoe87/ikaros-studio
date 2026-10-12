#pragma once

#include "engine/AudioEngine.h"

namespace ikaros
{

//==============================================================================
/**
    The window a third-party plug-in's own editor appears in ("Open plug-in
    window"). The plug-in draws everything inside it, so unlike the rest of
    the app it uses the system's title bar.

    The engine asks for one of these (see PluginWindowRequest) and owns it:
    it's deleted when the user closes it, when the plug-in is removed, or when
    the session closes.
*/
class PluginWindow  : public juce::DocumentWindow
{
public:
    explicit PluginWindow (PluginWindowRequest);
    ~PluginWindow() override;

    /** For AudioEngine::setPluginWindowFactory. */
    static std::unique_ptr<juce::Component> create (PluginWindowRequest);

private:
    std::function<void()> close;
    std::function<void (juce::Rectangle<int>)> onMoved;
    bool reportMoves = false;

    void closeButtonPressed() override;
    void moved() override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginWindow)
};

} // namespace ikaros
