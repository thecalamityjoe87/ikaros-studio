#include "platform/WindowManagerHints.h"

// This is the only file in the app that talks to the windowing system
// directly, because JUCE has no API for window-manager state. Keep platform
// code here, out of src/ui/. (The macOS inline title bar, which needs
// Objective-C++, is in WindowManagerHints_mac.mm.)

#if JUCE_LINUX || JUCE_BSD

// Xlib defines macros with very common names (None, Status, Bool...), so it's
// included after JUCE and nothing JUCE-related follows it.
#include <X11/Xlib.h>
#include <X11/Xatom.h>

namespace ikaros::platform
{

namespace
{
    // Our own connection to the X server (JUCE's is private). Opened on first
    // use and kept for the app's lifetime; the OS closes it on exit.
    struct X11Connection
    {
        Display* display  = XOpenDisplay (nullptr);
        Atom wmState      = intern ("_NET_WM_STATE");
        Atom maximisedH   = intern ("_NET_WM_STATE_MAXIMIZED_HORZ");
        Atom maximisedV   = intern ("_NET_WM_STATE_MAXIMIZED_VERT");
        Atom fullScreen   = intern ("_NET_WM_STATE_FULLSCREEN");

        Atom intern (const char* name) const   { return display != nullptr ? XInternAtom (display, name, False) : 0; }
    };

    X11Connection& x11()
    {
        static X11Connection connection;
        return connection;
    }

    ::Window getXWindow (juce::Component& window)
    {
        // On Linux, JUCE's native handle for a desktop window is the X11 Window id.
        if (auto* peer = window.getPeer())
            return (::Window) (juce::pointer_sized_uint) peer->getNativeHandle();

        return 0;
    }

    // Sends an EWMH _NET_WM_STATE request (action 1 = add, 0 = remove) for up
    // to two state atoms.
    void requestState (::Window xWindow, bool add, Atom first, Atom second)
    {
        auto& x = x11();

        XEvent ev {};
        ev.xclient.type = ClientMessage;
        ev.xclient.window = xWindow;
        ev.xclient.message_type = x.wmState;
        ev.xclient.format = 32;
        ev.xclient.data.l[0] = add ? 1 : 0;
        ev.xclient.data.l[1] = (long) first;
        ev.xclient.data.l[2] = (long) second;
        ev.xclient.data.l[3] = 1;   // source: normal application

        XSendEvent (x.display, DefaultRootWindow (x.display), False,
                    SubstructureRedirectMask | SubstructureNotifyMask, &ev);
    }
}

bool isMaximised (juce::DocumentWindow& window)
{
    auto& x = x11();
    auto xWindow = getXWindow (window);

    if (x.display == nullptr || xWindow == 0)
        return window.isFullScreen();

    // _NET_WM_STATE is a list of atoms the window manager keeps up to date.
    Atom actualType = 0;
    int actualFormat = 0;
    unsigned long numItems = 0, bytesAfter = 0;
    unsigned char* data = nullptr;

    const auto result = XGetWindowProperty (x.display, xWindow, x.wmState, 0, 64, False, XA_ATOM,
                                            &actualType, &actualFormat, &numItems, &bytesAfter, &data);

    bool maximised = false;

    if (result == Success && data != nullptr)
    {
        auto* atoms = reinterpret_cast<Atom*> (data);

        for (unsigned long i = 0; i < numItems; ++i)
            if (atoms[i] == x.maximisedH || atoms[i] == x.maximisedV || atoms[i] == x.fullScreen)
                maximised = true;
    }

    if (data != nullptr)
        XFree (data);

    return maximised;
}

void setMaximised (juce::DocumentWindow& window, bool shouldBeMaximised)
{
    auto& x = x11();
    auto xWindow = getXWindow (window);

    if (x.display == nullptr || xWindow == 0)
    {
        window.setFullScreen (shouldBeMaximised);
        return;
    }

    // Never full screen: that's the state Mutter locks in place.
    requestState (xWindow, false, x.fullScreen, 0);
    requestState (xWindow, shouldBeMaximised, x.maximisedH, x.maximisedV);
    XFlush (x.display);
}

bool startWindowManagerDrag (juce::DocumentWindow& window)
{
    auto* peer = window.getPeer();

    if (peer == nullptr)
        return false;

    // JUCE's own hook for window-manager-driven resizing (_NET_WM_MOVERESIZE).
    // A Zone with no edges set means "move" rather than "resize".
    peer->startHostManagedResize (peer->globalToLocal (juce::Desktop::getMousePosition()),
                                  juce::ResizableBorderComponent::Zone());
    return true;
}

} // namespace ikaros::platform

#else

namespace ikaros::platform
{
    bool isMaximised (juce::DocumentWindow& window)                     { return window.isFullScreen(); }
    void setMaximised (juce::DocumentWindow& window, bool maximise)     { window.setFullScreen (maximise); }
    bool startWindowManagerDrag (juce::DocumentWindow&)                 { return false; }
}

#endif
