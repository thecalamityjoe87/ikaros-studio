#pragma once

#include "ui/widgets/IconButton.h"

namespace ikaros
{

//==============================================================================
/**
    Implemented by the app's main window, so title-bar code in src/ui can
    maximise/restore it without knowing how each OS does that.
*/
class MaximisableWindow
{
public:
    virtual ~MaximisableWindow() = default;

    virtual bool isMaximised() const = 0;
    virtual void setMaximised (bool) = 0;

    /** Hands an in-progress title-bar drag over to the window manager, which
        then moves the window (and un-maximises it) itself, with the desktop's
        usual snapping. Returns false where that isn't supported; the caller
        then moves the window itself. */
    virtual bool startWindowManagerDrag() = 0;

    /** How much of the title bar's left edge the OS's own window buttons use
        (macOS), or 0 where the app draws them (WindowControls). */
    virtual int getNativeButtonsWidth() const = 0;

    void toggleMaximised()      { setMaximised (! isMaximised()); }
};

//==============================================================================
/**
    Minimise / maximise-restore / close buttons, drawn by the app so they look
    the same on Windows and every Linux desktop. (macOS uses the system's
    own traffic lights instead; see MainWindow.)

    They act on whatever DocumentWindow this component lives in, so they can be
    placed anywhere in the UI (we put them in the MenuBar strip).
*/
class WindowControls  : public juce::Component
{
public:
    WindowControls();

    /** Keeps the maximise/restore icon in sync with the window state. */
    void update();

    /** Which control is at a point (in this component's coordinates), for the
        OS hit-testing done in MainWindow::findControlAtPoint. */
    juce::Component::WindowControlKind getControlAt (juce::Point<float>) const;

    void resized() override;

    static constexpr int buttonWidth = 46;
    static constexpr int preferredWidth = buttonWidth * 3;

private:
    IconButton minimiseButton { "Minimize", Icon::windowMinimise };
    IconButton maximiseButton { "Maximize", Icon::windowMaximise };
    IconButton closeButton    { "Close", Icon::windowClose };

    bool showingRestore = false;

    juce::DocumentWindow* getWindow() const;
    MaximisableWindow* getMaximisableWindow() const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WindowControls)
};

} // namespace ikaros
