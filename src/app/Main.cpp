// Ikaros Studio — application entry point.

#include "app/MainWindow.h"
#include "ui/theme/IkarosLookAndFeel.h"

namespace ikaros
{

//==============================================================================
/** The JUCEApplication: owns the LookAndFeel, the audio engine and the window. */
class IkarosApplication  : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override       { return JUCE_APPLICATION_NAME_STRING; }
    const juce::String getApplicationVersion() override    { return JUCE_APPLICATION_VERSION_STRING; }
    // One window at a time, except for the plug-in scan's worker processes:
    // they're copies of this app started with a special command line.
    bool moreThanOneInstanceAllowed() override             { return isScanWorker(); }

    void initialise (const juce::String& commandLine) override
    {
        // A scan worker only loads plug-ins for the main app; no engine, no window.
        if (AudioEngine::runPluginScanWorkerIfRequested (commandLine))
            return;

        // Install the LookAndFeel before creating any components so they all pick it up.
        lookAndFeel = std::make_unique<IkarosLookAndFeel>();
        juce::LookAndFeel::setDefaultLookAndFeel (lookAndFeel.get());

        engine = std::make_unique<AudioEngine>();
        mainWindow = std::make_unique<MainWindow> (getApplicationName(), *engine);
    }

    void shutdown() override
    {
        // Destroy in reverse order of creation: UI first (it references the
        // engine), then the engine, then the LookAndFeel.
        mainWindow.reset();
        engine.reset();
        juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
        lookAndFeel.reset();
    }

    void systemRequestedQuit() override
    {
        if (mainWindow != nullptr)
            mainWindow->getMainComponent()->requestQuit();
        else
            quit();
    }

    void anotherInstanceStarted (const juce::String&) override {}

private:
    static bool isScanWorker()
    {
        return getCommandLineParameters().contains ("--IkarosPluginScan:");
    }

    std::unique_ptr<IkarosLookAndFeel> lookAndFeel;
    std::unique_ptr<AudioEngine> engine;
    std::unique_ptr<MainWindow> mainWindow;
};

} // namespace ikaros

// Generates main() (or WinMain) and starts the app's message loop.
START_JUCE_APPLICATION (ikaros::IkarosApplication)
