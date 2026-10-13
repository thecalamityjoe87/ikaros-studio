#include "ui/dialogs/MonitoringWarning.h"
#include "engine/TrackController.h"

namespace ikaros
{

static const char* const warningShownSetting = "monitoringWarningShown";

void confirmMonitoring (AudioEngine& engine, juce::Component* associatedComponent, std::function<void (bool)> then)
{
    if ((bool) engine.getAppSetting (warningShownSetting, false))
    {
        then (true);
        return;
    }

    auto options = juce::MessageBoxOptions()
                       .withIconType (juce::MessageBoxIconType::WarningIcon)
                       .withTitle ("Turn on monitoring?")
                       .withMessage ("Monitoring lets you hear your microphone or instrument as you play.\n\n"
                                     "With a microphone and speakers, the speakers' sound goes back into the "
                                     "microphone and can build into a loud howl (feedback). Use headphones, "
                                     "or keep the volume down.")
                       .withButton ("Turn On Monitoring")
                       .withButton ("Cancel")
                       .withAssociatedComponent (associatedComponent);

    // showAsync's callback gets 1 for the first button and 0 for the last.
    // The engine outlives every window, so capturing it by reference is safe.
    juce::AlertWindow::showAsync (options, [&engine, then = std::move (then)] (int result)
    {
        if (result == 1)
            engine.setAppSetting (warningShownSetting, true);

        then (result == 1);
    });
}

void setMonitoringWithWarning (AudioEngine& engine, TrackId track, bool shouldMonitor, juce::Component* associatedComponent)
{
    if (! shouldMonitor)
    {
        engine.getTracks().inputs().setMonitoring (track, false);
        return;
    }

    confirmMonitoring (engine, associatedComponent, [&engine, track] (bool goAhead)
    {
        if (goAhead)
            engine.getTracks().inputs().setMonitoring (track, true);
    });
}

} // namespace ikaros
