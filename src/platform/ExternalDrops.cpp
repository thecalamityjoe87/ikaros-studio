// JUCE's X11 classes (XWindowSystem, for its connection to the X server) are
// only declared when this is set before JUCE's header is included.
#define JUCE_GUI_BASICS_INCLUDE_XHEADERS 1

#include <juce_gui_basics/juce_gui_basics.h>
#include "platform/ExternalDrops.h"

#if JUCE_LINUX || JUCE_BSD

// Xlib's internal header, for XESetWireToEvent. Included after JUCE because it
// defines macros with common names.
#include <X11/Xlibint.h>

namespace ikaros::platform
{

namespace
{
    // Xlib converts each event arriving from the X server ("wire" format) into
    // an XEvent with a function per event type, and lets a program replace
    // that function. We wrap the one for ClientMessage, which is how XDND
    // messages arrive, and fix up XdndEnter before JUCE sees it.
    using WireToEventProc = Bool (*) (Display*, XEvent*, xEvent*);

    WireToEventProc originalClientMessageProc = nullptr;
    Atom xdndEnter = 0;

    Bool convertClientMessage (Display* display, XEvent* event, xEvent* wire)
    {
        const auto result = originalClientMessageProc (display, event, wire);

        if (result && event->type == ClientMessage && event->xclient.message_type == xdndEnter)
        {
            // XdndEnter's second word: protocol version in the top 8 bits,
            // flags in the rest. JUCE accepts version 3 only.
            auto& word = event->xclient.data.l[1];
            const auto version = ((unsigned long) word >> 24) & 0xff;

            if (version > 3)
                word = (long) (((unsigned long) word & 0x00ffffffUL) | (3UL << 24));
        }

        return result;
    }
}

void enableDropsFromWaylandApps()
{
    if (originalClientMessageProc != nullptr)
        return;

    auto* display = juce::XWindowSystem::getInstance()->getDisplay();

    if (display == nullptr)
        return;

    juce::XWindowSystemUtilities::ScopedXLock lock;
    xdndEnter = XInternAtom (display, "XdndEnter", False);
    originalClientMessageProc = XESetWireToEvent (display, ClientMessage, convertClientMessage);
}

} // namespace ikaros::platform

#else

namespace ikaros::platform
{
void enableDropsFromWaylandApps() {}
}

#endif
