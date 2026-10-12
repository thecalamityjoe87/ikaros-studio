#pragma once

namespace ikaros::platform
{

/*  Lets files be dragged in from apps running natively under Wayland (e.g.
    GNOME's Files) on Linux.

    JUCE windows are X11 windows, so on a Wayland desktop they run under
    XWayland, and a drag from a Wayland app reaches them through the
    compositor's bridge using the X11 drag-and-drop protocol (XDND). GNOME's
    Mutter always announces protocol version 5, but JUCE only accepts version
    3 and silently ignores anything newer, so every such drop was lost.
    The messages JUCE relies on are the same in versions 3 to 5, so we tell
    JUCE the drag is version 3.

    Call once after the main window is on screen. Does nothing on other
    platforms.
*/
void enableDropsFromWaylandApps();

} // namespace ikaros::platform
