#pragma once

#include "ui/titlebar/WindowControls.h"

namespace ikaros
{

//==============================================================================
/**
    The 30px strip at the top. It doubles as the window's title bar, because
    the app draws its own (no OS title bar or window buttons).

      [File Edit Track ...]      ● Session name · Ikaros Studio      [– □ ×]

    The centred title shows the session name, with an amber dot when it has
    unsaved changes, followed by the app name.

    Drag the empty parts to move the window; double-click to maximise/restore.
    On macOS the menus move to the system menu bar and the window buttons sit
    on the left, following platform convention.
*/
class MenuBar  : public juce::Component,
                 public juce::SettableTooltipClient   // adds setTooltip()
{
public:
    explicit MenuBar (juce::MenuBarModel*);

    void setSessionStatus (const juce::String& sessionName, bool isUntitled, bool hasUnsavedChanges);

    /** Keeps the window buttons in sync with the window state. Called by the owner's timer. */
    void updateWindowState();

    /** What's under a point (in this component's coordinates): a window
        button, the draggable caption, or an interactive control. */
    juce::Component::WindowControlKind getWindowControlAt (juce::Point<float>) const;

    void paint (juce::Graphics&) override;
    void resized() override;

    // Moving the window by dragging the strip (Linux/macOS; on Windows the OS
    // does this itself, using getWindowControlAt).
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

private:
    juce::MenuBarModel* model;
    juce::MenuBarComponent menus;
    WindowControls windowControls;

    // Window dragging: where the mouse is relative to the window's top-left.
    // A maximised window is restored once the mouse moves a few pixels
    // ("drag to restore"), like on every desktop.
    juce::Point<int> dragOffset;
    bool draggingWindow = false, restoreOnDrag = false;

    juce::String sessionName;
    bool unsavedChanges = false;
    juce::Rectangle<int> freeArea;   // between the menus and the window buttons

    int getMenusWidth();
    juce::Component* getWindow() const   { return getTopLevelComponent(); }

};

} // namespace ikaros
