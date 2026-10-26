#include "platform/WindowManagerHints.h"

// The macOS half of WindowManagerHints: the inline title bar needs AppKit,
// which only Objective-C++ can talk to. Only built on macOS (see CMakeLists).

#if JUCE_MAC

#import <Cocoa/Cocoa.h>
#import <objc/runtime.h>

namespace ikaros::platform
{

namespace
{
    char watchingFullScreenKey;   // marks windows that already restore the title bar after full screen

    NSWindow* getNSWindow (const juce::Component& window)
    {
        if (auto* peer = window.getPeer())
            return [(__bridge NSView*) peer->getNativeHandle() window];

        return nil;
    }

    void applyInlineTitleBar (NSWindow* nsWindow)
    {
        if (([nsWindow styleMask] & NSWindowStyleMaskTitled) == 0
             || ([nsWindow styleMask] & NSWindowStyleMaskFullSizeContentView) != 0)
            return;

        // Our content runs up under the title bar, which is see-through and has no
        // text, so only the traffic lights show, over the left of the MenuBar strip.
        [nsWindow setStyleMask: [nsWindow styleMask] | NSWindowStyleMaskFullSizeContentView];
        [nsWindow setTitlebarAppearsTransparent: YES];
        [nsWindow setTitleVisibility: NSWindowTitleHidden];
    }
}

void useInlineTitleBar (juce::DocumentWindow& window)
{
    auto* nsWindow = getNSWindow (window);

    if (nsWindow == nil)
        return;

    applyInlineTitleBar (nsWindow);

    // Leaving full screen, JUCE resets the window's style in its own
    // windowDidExitFullScreen, which drops the inline title bar: the traffic
    // lights end up in a title bar of their own above the app. Put it back
    // once JUCE is done (hence the dispatch_async: the order observers are
    // told in isn't defined). Setting it again from MainWindow::resized()
    // didn't bring it back.
    if (objc_getAssociatedObject (nsWindow, &watchingFullScreenKey) == nil)
    {
        objc_setAssociatedObject (nsWindow, &watchingFullScreenKey, @YES, OBJC_ASSOCIATION_RETAIN);

        [[NSNotificationCenter defaultCenter] addObserverForName: NSWindowDidExitFullScreenNotification
                                                          object: nsWindow
                                                           queue: nil
                                                      usingBlock: ^(NSNotification* note)
        {
            auto* exited = (NSWindow*) [note object];
            dispatch_async (dispatch_get_main_queue(), ^{ applyInlineTitleBar (exited); });
        }];
    }
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
