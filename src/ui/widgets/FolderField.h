#pragma once

#include "ui/widgets/IconButton.h"

namespace ikaros
{

//==============================================================================
/**
    Shows a folder setting: the folder (or the default one, in grey), with
    Choose... to pick another and Default to go back.

    It only reports the choice through onChanged; whoever owns the setting
    stores it, then calls setFolder() again if it wants it shown differently.
*/
class FolderField  : public juce::Component,
                     public juce::SettableTooltipClient   // the full path
{
public:
    /** `name` titles the folder chooser and names the field for screen readers. */
    explicit FolderField (const juce::String& name);

    /** `chosen` is empty while the default is used. */
    void setFolder (const juce::File& chosen, const juce::File& defaultFolder);

    /** The new folder, or an empty File for the default. */
    std::function<void (const juce::File&)> onChanged;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    juce::String name;
    juce::File chosenFolder, defaultFolder;
    IconButton chooseButton { "Choose folder", std::nullopt, "Choose..." };
    IconButton defaultButton { "Use the default folder", std::nullopt, "Default" };
    juce::Rectangle<int> pathArea;
    std::unique_ptr<juce::FileChooser> chooser;

    void chooseFolder();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FolderField)
};

} // namespace ikaros
