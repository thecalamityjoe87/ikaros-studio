#pragma once

#include "app/MainComponent.h"
#include "ui/titlebar/WindowControls.h"

namespace ikaros
{

//==============================================================================
/**
    The top-level window. The app draws everything, including the title bar
    and window buttons (see ui/titlebar), except on macOS, where the system's
    traffic lights sit in our title bar; maximise/restore goes through
    platform::setMaximised so it agrees with the OS / window manager.
*/
class MainWindow  : public juce::DocumentWindow,
                    public MaximisableWindow,
                    private juce::Timer
{
public:
    MainWindow (const juce::String& name, AudioEngine&);

    void closeButtonPressed() override;

    MainComponent* getMainComponent() const noexcept   { return content; }

    // MaximisableWindow: used by the title bar (MenuBar / WindowControls).
    bool isMaximised() const override                  { return maximised; }
    void setMaximised (bool shouldBeMaximised) override;
    bool startWindowManagerDrag() override;
    int getNativeButtonsWidth() const override;

    // On Windows, clicks on the maximise area are routed here by the OS.
    void maximiseButtonPressed() override              { toggleMaximised(); }

    int getDesktopWindowStyleFlags() const override;
    juce::BorderSize<int> getBorderThickness() const override;
    void moved() override;
    void resized() override;
    WindowControlKind findControlAtPoint (juce::Point<float>) const override;

private:
    MainComponent* content = nullptr;   // non-owning; the window owns it
    bool maximised = false;
    juce::Rectangle<int> restoredBounds;

    void setMaximisedFlag (bool);
    void timerCallback() override;
    void rememberRestoredBounds();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainWindow)
};

} // namespace ikaros
