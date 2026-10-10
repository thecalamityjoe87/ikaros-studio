#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace ikaros::platform
{

/*  Maximise/restore for a window with a custom (app-drawn) title bar.

    Windows / macOS: JUCE's own full-screen state matches the OS, so these just
    use DocumentWindow::setFullScreen / isFullScreen.

    Linux: the window manager (e.g. GNOME's Mutter) has its own idea of
    "maximised", and JUCE doesn't track it for windows without a native title
    bar. Worse, if an undecorated window resizes itself to cover the whole
    monitor (which is what JUCE's setFullScreen does), Mutter treats it as a
    full-screen app and locks it in place: no moving or resizing, not even
    with Alt+drag. So on Linux we ask the window manager to maximise and
    restore, using the standard EWMH _NET_WM_STATE protocol, and read its
    state back. App and desktop then always agree.
*/

/** True if the window is maximised (or full screen, on Linux). */
bool isMaximised (juce::DocumentWindow&);

/** Maximises or restores the window. On Linux this is asynchronous: the new
    size arrives a moment later through the normal resize path. */
void setMaximised (juce::DocumentWindow&, bool shouldBeMaximised);

/** Lets the window manager take over a title-bar drag that's in progress
    (mouse button held). Linux only; returns false elsewhere. */
bool startWindowManagerDrag (juce::DocumentWindow&);

} // namespace ikaros::platform
