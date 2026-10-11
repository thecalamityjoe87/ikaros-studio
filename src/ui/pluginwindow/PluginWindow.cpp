#include "ui/pluginwindow/PluginWindow.h"
#include "ui/theme/Tokens.h"

namespace ikaros
{

using namespace tokens;

PluginWindow::PluginWindow (PluginWindowRequest request)
    : juce::DocumentWindow (request.title, colour::windowBg, juce::DocumentWindow::closeButton,
                            false),   // added to the desktop below, once it has its size
      close (std::move (request.close)),
      onMoved (std::move (request.moved))
{
    setUsingNativeTitleBar (true);
    setResizable (false, false);

    // true = the window sizes itself to the editor, and follows it if the
    // plug-in resizes its editor later.
    setContentOwned (request.content.release(), true);
    setTopLeftPosition (request.position);

    // Keep it in front of the main window, like a palette. (On Linux the
    // window manager otherwise tends to drop it behind.)
    setAlwaysOnTop (true);
    addToDesktop();
    setVisible (true);
    toFront (true);
    reportMoves = true;
}

PluginWindow::~PluginWindow()
{
    reportMoves = false;
    clearContentComponent();   // the plug-in's editor goes first
}

std::unique_ptr<juce::Component> PluginWindow::create (PluginWindowRequest request)
{
    return std::make_unique<PluginWindow> (std::move (request));
}

void PluginWindow::closeButtonPressed()
{
    // The engine owns this window, and deletes it (soon after this returns).
    if (close)
        close();
}

void PluginWindow::moved()
{
    juce::DocumentWindow::moved();

    if (reportMoves && onMoved)
        onMoved (getBounds());
}

} // namespace ikaros
