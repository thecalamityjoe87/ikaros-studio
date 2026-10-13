#include "ui/dialogs/DeviceChangePrompt.h"

namespace ikaros
{

namespace
{
    // An alert with a tick box under its message. The tick box is a member, so
    // it goes away with the window (AlertWindow doesn't own custom components).
    class DeviceChangeWindow  : public juce::AlertWindow
    {
    public:
        DeviceChangeWindow (const juce::String& title, const juce::String& message, juce::Component* associated)
            : juce::AlertWindow (title, message, juce::MessageBoxIconType::QuestionIcon, associated)
        {
            remember.setSize (360, 24);
            addCustomComponent (&remember);
        }

        juce::ToggleButton remember { "Remember my choice (change it in Preferences)" };
    };
}

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

    auto* window = new DeviceChangeWindow (appeared ? "New audio device" : "Audio device disconnected",
                                           message, associatedComponent);

    window->addButton ("Switch to " + prompt.useName, 1, juce::KeyPress (juce::KeyPress::returnKey));
    window->addButton (appeared ? "Keep Current" : "Not Now", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    // The window is still alive when the callback runs, and is deleted after it.
    window->enterModalState (true,
        juce::ModalCallbackFunction::create ([&engine, prompt, onClosed = std::move (onClosed),
                                              safeWindow = juce::Component::SafePointer<DeviceChangeWindow> (window)] (int result)
        {
            const bool doSwitch = result == 1;

            if (safeWindow != nullptr && safeWindow->remember.getToggleState())
                engine.getPreferences().set (settings::deviceChange,
                                             doSwitch ? settings::DeviceChange::alwaysSwitch
                                                      : settings::DeviceChange::neverAsk);

            if (doSwitch)
            {
                const auto error = engine.switchAudioDevice (prompt);

                if (error.isNotEmpty())
                    juce::AlertWindow::showAsync (juce::MessageBoxOptions()
                                                      .withIconType (juce::MessageBoxIconType::WarningIcon)
                                                      .withTitle ("Couldn't switch")
                                                      .withMessage ("\"" + prompt.useName + "\" couldn't be used: " + error
                                                                    + "\n\nAnother program may be using it. You can pick a device in Preferences > Audio & MIDI.")
                                                      .withButton ("OK"),
                                                  nullptr);
            }

            onClosed();
        }),
        true);
}

} // namespace ikaros
