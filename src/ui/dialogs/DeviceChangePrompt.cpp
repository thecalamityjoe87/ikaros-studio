#include "ui/dialogs/DeviceChangePrompt.h"
#include "ui/dialogs/AlertDialog.h"

namespace ikaros
{

void showDeviceChangePrompt (AudioEngine& engine, const AudioDevicePrompt& prompt, juce::Component* associatedComponent,
                             std::function<void()> onClosed)
{
    const bool appeared = prompt.kind == AudioDevicePrompt::Kind::appeared;
    const bool input = prompt.inputName.isNotEmpty();
    const bool output = prompt.outputName.isNotEmpty();
    const juce::String use = input && output ? "recording and playback" : input ? "recording" : "playback";

    const auto message = appeared
        ? "\"" + prompt.deviceName + "\" is now available.\n\nSwitch to it for " + use + "?"
        : "\"" + prompt.deviceName + "\" was disconnected.\n\nSwitch to \"" + prompt.useName + "\" for " + use + "?";

    auto options = juce::MessageBoxOptions()
                       .withTitle (appeared ? "New audio device" : "Audio device disconnected")
                       .withMessage (message)
                       .withButton ("Switch to " + prompt.useName, 1)
                       .withButton (appeared ? "Keep Current" : "Not Now", 0)
                       .withAssociatedComponent (associatedComponent);

    // 1 = Switch, 0 = Keep Current / Not Now (or Escape).
    AlertDialog::show (options, "Remember my choice (change it in Preferences)",
        [&engine, prompt, onClosed = std::move (onClosed),
         safeAssociated = juce::Component::SafePointer<juce::Component> (associatedComponent)] (int result, bool remember)
        {
            const bool doSwitch = result == 1;

            if (remember)
                engine.getPreferences().set (settings::deviceChange,
                                             doSwitch ? settings::DeviceChange::alwaysSwitch
                                                      : settings::DeviceChange::neverAsk);

            if (! doSwitch)
            {
                onClosed();
                return;
            }

            engine.switchAudioDevice (prompt, [prompt, onClosed, safeAssociated] (const juce::String& error)
            {
                if (error.isNotEmpty())
                    AlertDialog::show (juce::MessageBoxOptions()
                                           .withTitle ("Couldn't switch")
                                           .withMessage ("\"" + prompt.useName + "\" couldn't be used: " + error
                                                         + "\n\nAnother program may be using it. You can pick a device in Preferences > Audio & MIDI.")
                                           .withButton ("OK", 0)
                                           .withAssociatedComponent (safeAssociated.getComponent()));

                onClosed();
            });
        });
}

} // namespace ikaros
