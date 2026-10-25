#include "ui/dialogs/MonitoringWarning.h"
#include "ui/dialogs/AlertDialog.h"
#include "engine/TrackController.h"

namespace ikaros
{

void confirmMonitoring (AudioEngine& engine, juce::Component* associatedComponent, std::function<void (bool)> then)
{
    if (engine.getPreferences().get (settings::monitoringWarningShown))
    {
        then (true);
        return;
    }

    auto options = juce::MessageBoxOptions()
                       .withTitle ("Turn on monitoring?")
                       .withMessage ("Monitoring lets you hear your microphone or instrument as you play.\n\n"
                                     "With a microphone and speakers, the speakers' sound goes back into the "
                                     "microphone and can build into a loud howl (feedback). Use headphones, "
                                     "or keep the volume down.")
                       .withButton ("Turn On Monitoring", 1)
                       .withButton ("Cancel", 0)
                       .withAssociatedComponent (associatedComponent);

    // The callback gets 1 for the first button and 0 for the last.
    // The engine outlives every window, so capturing it by reference is safe.
    AlertDialog::show (options, [&engine, then = std::move (then)] (int result)
    {
        if (result == 1)
            engine.getPreferences().set (settings::monitoringWarningShown, true);

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
