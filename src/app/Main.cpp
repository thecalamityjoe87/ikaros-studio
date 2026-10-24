// Ikaros Studio — application entry point.

#include "app/MainWindow.h"
#include "app/PerfHarness.h"
#include "platform/ExternalDrops.h"
#include "ui/dialogs/StartupSplash.h"
#include "ui/theme/IkarosLookAndFeel.h"
#include "ui/theme/ThemeManager.h"

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
    // IKAROS_SECOND_INSTANCE=1 also allows one, for testing a build while
    // another copy is open.
    bool moreThanOneInstanceAllowed() override
    {
        return isScanWorker() || juce::SystemStats::getEnvironmentVariable ("IKAROS_SECOND_INSTANCE", {}).isNotEmpty();
    }

    void initialise (const juce::String& commandLine) override
    {
        // A scan worker only loads plug-ins for the main app; no engine, no window.
        if (AudioEngine::runPluginScanWorkerIfRequested (commandLine))
            return;

        // Install the LookAndFeel before creating any components so they all pick it up.
        lookAndFeel = std::make_unique<IkarosLookAndFeel>();
        juce::LookAndFeel::setDefaultLookAndFeel (lookAndFeel.get());

        // Starting the engine takes a moment and blocks the message loop, so
        // put the splash on screen first and do the rest once it has drawn.
        splash = std::make_unique<StartupSplash> ([this, commandLine] { finishStarting (commandLine); });
    }

    void finishStarting (const juce::String& commandLine)
    {
        // Doesn't start yet: the engine's stages run below, one at a time.
        engine = std::make_unique<AudioEngine> ("IkarosStudio", true, false);
        runNextStartupStep (commandLine);
    }

    void shutdown() override
    {
        // Destroy in reverse order of creation: UI first (it references the
        // engine), then the engine, then the LookAndFeel.
        splash.reset();
        perfHarness.reset();
        mainWindow.reset();
        themeManager.reset();   // it listens to the engine's preferences
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

    // Only one copy of the app runs. When another is started, for example by
    // double-clicking a session while the app is open, its command line comes
    // here instead (on macOS, so do sessions opened from the Finder).
    void anotherInstanceStarted (const juce::String& commandLine) override
    {
        if (mainWindow == nullptr)
            return;

        mainWindow->toFront (true);
        openSessionsIn (commandLine);
    }

private:
    // The engine's stages fill this much of the splash's progress bar, and
    // opening the window fills the rest.
    static constexpr float engineShare = 0.88f;

    // Each step shows what it's about to do on the splash, then does it a
    // moment later: the splash can only draw (and the window system confirm
    // it) while the message loop is running, and a step blocks it.
    void runNextStartupStep (const juce::String& commandLine)
    {
        juce::String text;
        float fraction;
        std::function<void()> work;

        if (auto stage = engine->getNextStartupStage())
        {
            text = stage->description;
            fraction = stage->fractionDone * engineShare;
            work = [this, commandLine] { engine->runNextStartupStage(); runNextStartupStep (commandLine); };
        }
        else
        {
            text = "Opening main window...";
            fraction = engineShare;
            work = [this, commandLine] { openMainWindow (commandLine); };
        }

        splash->setStage (text, fraction);

        // The app object outlives the message loop, so `this` is safe here.
        juce::Timer::callAfterDelay (20, [work = std::move (work)]
        {
            // Not if the app was asked to quit in the meantime.
            if (! juce::MessageManager::getInstance()->hasStopMessageBeenSent())
                work();
        });
    }

    void openMainWindow (const juce::String& commandLine)
    {
        // The saved theme (Preferences > Appearance), before the window
        // exists so it opens in the right colours.
        themeManager = std::make_unique<ThemeManager> (engine->getPreferences(), *lookAndFeel);

        // The saved interface size, before the window exists (Preferences > Appearance
        // changes it later without a restart; see MainComponent).
        MainComponent::setInterfaceScale (engine->getPreferences().getInterfaceScale());

        mainWindow = std::make_unique<MainWindow> (getApplicationName(), *engine);
        platform::enableDropsFromWaylandApps();

        splash->setStage ("Ready", 1.0f);

        // Started by double-clicking a session (or "ikaros-studio My Song.iksession").
        openSessionsIn (commandLine);

        // Debug only: a stress run (--perf-scenario=..., with IKAROS_PERF=1).
        perfHarness = PerfHarness::createIfRequested (commandLine, *engine, *mainWindow->getMainComponent(),
                                                      [] { juce::JUCEApplication::quit(); });

        // The window is up behind the splash; close the splash once it has
        // been seen.
        juce::Timer::callAfterDelay (splash->getRemainingMilliseconds(), [this] { splash.reset(); });
    }

    // Opens the first session named in a command line, once the window is up
    // (after the session chooser has appeared, so it can replace it).
    void openSessionsIn (const juce::String& commandLine)
    {
        // fromTokens with quote handling keeps "My Song.iksession" in one piece.
        for (auto arg : juce::StringArray::fromTokens (commandLine, true))
        {
            const juce::File file (juce::File::getCurrentWorkingDirectory().getChildFile (arg.unquoted()));

            if (file.existsAsFile() && (file.hasFileExtension (AudioEngine::sessionFileExtension)
                                         || file.hasFileExtension (AudioEngine::legacySessionFileExtension)))
            {
                juce::MessageManager::callAsync ([window = juce::Component::SafePointer<MainWindow> (mainWindow.get()), file]
                {
                    if (window != nullptr)
                        window->getMainComponent()->openFileFromSystem (file);
                });
                return;
            }
        }
    }

    static bool isScanWorker()
    {
        return getCommandLineParameters().contains ("--IkarosPluginScan:");
    }

    std::unique_ptr<IkarosLookAndFeel> lookAndFeel;
    std::unique_ptr<StartupSplash> splash;   // only while starting
    std::unique_ptr<AudioEngine> engine;
    std::unique_ptr<ThemeManager> themeManager;   // after engine: members are destroyed in reverse order
    std::unique_ptr<MainWindow> mainWindow;
    std::unique_ptr<PerfHarness> perfHarness;   // only for a stress run
};

} // namespace ikaros

// Generates main() (or WinMain) and starts the app's message loop.
START_JUCE_APPLICATION (ikaros::IkarosApplication)
