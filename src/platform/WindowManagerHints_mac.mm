#include "platform/WindowManagerHints.h"

// The macOS half of WindowManagerHints: the inline title bar needs AppKit,
// which only Objective-C++ can talk to. Only built on macOS (see CMakeLists).

#if JUCE_MAC

#import <Cocoa/Cocoa.h>

namespace ikaros::platform
{

namespace
{
    NSWindow* getNSWindow (const juce::Component& window)
    {
        if (auto* peer = window.getPeer())
            return [(__bridge NSView*) peer->getNativeHandle() window];

        return nil;
    }
}

void useInlineTitleBar (juce::DocumentWindow& window)
{
    auto* nsWindow = getNSWindow (window);

    if (nsWindow == nil || ([nsWindow styleMask] & NSWindowStyleMaskTitled) == 0)
        return;

    if (([nsWindow styleMask] & NSWindowStyleMaskFullSizeContentView) != 0)
        return;

    // Our content runs up under the title bar, which is see-through and has no
    // text, so only the traffic lights show, over the left of the MenuBar strip.
    [nsWindow setStyleMask: [nsWindow styleMask] | NSWindowStyleMaskFullSizeContentView];
    [nsWindow setTitlebarAppearsTransparent: YES];
    [nsWindow setTitleVisibility: NSWindowTitleHidden];
}

int getInlineTitleBarButtonsWidth (const juce::DocumentWindow& window)
{
    auto* nsWindow = getNSWindow (window);

    // In full screen the title bar (buttons and all) slides in over the
    // content only when the mouse is at the top, so it needs no space.
    if (nsWindow == nil || ([nsWindow styleMask] & NSWindowStyleMaskFullScreen) != 0)
        return 0;

    auto* close = [nsWindow standardWindowButton: NSWindowCloseButton];
    auto* zoom  = [nsWindow standardWindowButton: NSWindowZoomButton];

    if (close == nil || zoom == nil || [zoom isHidden])
        return 0;

    // Ask AppKit where the buttons are rather than hard-coding it: their size
    // and spacing differ between macOS versions. Leave the same gap after them
    // as before them. Points, converted to the app's (scaled) units.
    const auto closeFrame = [close convertRect: [close bounds] toView: nil];
    const auto zoomFrame  = [zoom convertRect: [zoom bounds] toView: nil];
    const auto width = NSMaxX (zoomFrame) + NSMinX (closeFrame);

    return (int) std::ceil (width / (double) window.getDesktopScaleFactor());
}

} // namespace ikaros::platform

#endif
