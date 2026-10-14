#include "ui/dialogs/DeviceChangePrompt.h"

namespace ikaros
{

namespace
{
    // The tick box's left edge lines up with the message text, which AlertWindow draws
    // after its icon while placing custom components at a tenth of its width.
    class RememberRow  : public juce::Component
    {
    public:
        RememberRow()
        {
            setSize (360, 24);
            addAndMakeVisible (remember);
        }

        void moved() override       { layoutBox(); }
        void resized() override     { layoutBox(); }

        juce::ToggleButton remember { "Remember my choice (change it in Preferences)" };

    private:
        void layoutBox()
        {
            constexpr int messageLeft = 81;   // AlertWindow's icon width, plus its border
            constexpr int tickInset = 4;      // where the look and feel starts drawing the tick
            const int offset = messageLeft - tickInset - getX();
            remember.setBounds (offset, 0, getWidth() - offset, getHeight());
        }
    };

    // An alert with a tick box under its message. The row is a member, so it
    // goes away with the window (AlertWindow doesn't own custom components).
    class DeviceChangeWindow  : public juce::AlertWindow
    {
    public:
        DeviceChangeWindow (const juce::String& title, const juce::String& message, juce::Component* associated)
            : juce::AlertWindow (title, message, juce::MessageBoxIconType::QuestionIcon, associated)
        {
            addCustomComponent (&row);
        }

        bool rememberChoice() const     { return row.remember.getToggleState(); }

    private:
        RememberRow row;
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

            if (safeWindow != nullptr && safeWindow->rememberChoice())
                engine.getPreferences().set (settings::deviceChange,
                                             doSwitch ? settings::DeviceChange::alwaysSwitch
                                                      : settings::DeviceChange::neverAsk);

            if (! doSwitch)
            {
                onClosed();
                return;
            }

            engine.switchAudioDevice (prompt, [prompt, onClosed] (const juce::String& error)
            {
                if (error.isNotEmpty())
                    juce::AlertWindow::showAsync (juce::MessageBoxOptions()
                                                      .withIconType (juce::MessageBoxIconType::WarningIcon)
                                                      .withTitle ("Couldn't switch")
                                                      .withMessage ("\"" + prompt.useName + "\" couldn't be used: " + error
                                                                    + "\n\nAnother program may be using it. You can pick a device in Preferences > Audio & MIDI.")
                                                      .withButton ("OK"),
                                                  nullptr);

                onClosed();
            });
        }),
        true);
}

} // namespace ikaros
