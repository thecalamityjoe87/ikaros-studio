#pragma once

#include "engine/AudioEngine.h"
#include "ui/preferences/AudioSettingsPanel.h"
#include "ui/preferences/PreferencesPage.h"
#include "ui/widgets/FolderField.h"

namespace ikaros
{

// The pages of the Preferences window, one per section. Each reads its
// settings when it's made and writes them as its controls change.

//==============================================================================
class GeneralPage  : public PreferencesPage
{
public:
    explicit GeneralPage (AudioEngine&);

private:
    AudioEngine& engine;
    juce::ComboBox startupBox;
    FolderField sessionFolderField { "Default folder for new sessions" };
    IconButton clearRecentButton { "Clear recent sessions", std::nullopt, "Clear Recent Sessions" };

    void showSessionFolder();
};

//==============================================================================
/** The device picker (moved here from the old Audio & MIDI Settings window), then the rest. */
class AudioMidiPage  : public PreferencesPage
{
public:
    explicit AudioMidiPage (AudioEngine&);

private:
    AudioEngine& engine;
    AudioSettingsPanel devices;
    juce::ComboBox velocityBox;
    juce::ComboBox deviceChangeBox;    IconButton warningButton { "Show the feedback warning again", std::nullopt, "Show Feedback Warning Again" };

    void updateWarningButton();
};

//==============================================================================
class RecordingPage  : public PreferencesPage
{
public:
    explicit RecordingPage (AudioEngine&);

private:
    AudioEngine& engine;
    juce::ComboBox countInBox, cycleBox;
    juce::ToggleButton autoArmToggle { "Arm the selected track automatically" };
};

//==============================================================================
class EditingPage  : public PreferencesPage
{
public:
    explicit EditingPage (AudioEngine&);

private:
    AudioEngine& engine;
    juce::ToggleButton snapToggle { "Snap to the grid" };
    juce::ComboBox gridBox, wheelBox;
    juce::ToggleButton followToggle { "Follow the playhead while playing" };
};

//==============================================================================
class SoundsPluginsPage  : public PreferencesPage,
                           private juce::Timer
{
public:
    SoundsPluginsPage (AudioEngine&, std::function<void()> openSoundLibrary);
    ~SoundsPluginsPage() override;

private:
    class FormatToggles;
    class FolderList;

    AudioEngine& engine;
    FolderField soundFolderField { "Folder for sound packs" };
    IconButton soundLibraryButton { "Open the Sound Library", std::nullopt, "Open Sound Library..." };
    std::unique_ptr<FormatToggles> formatToggles;
    std::unique_ptr<FolderList> folderList;
    IconButton rescanButton { "Rescan plug-ins", std::nullopt, "Rescan Plug-ins" };
    IconButton clearSkippedButton { "Clear the skipped plug-ins", std::nullopt, "Clear Skipped Plug-ins" };
    juce::String shownScanNote, shownSkippedNote;

    void showSoundFolder();
    void updateScanState();
    void timerCallback() override;
};

//==============================================================================
class ExportPage  : public PreferencesPage
{
public:
    explicit ExportPage (AudioEngine&);

private:
    AudioEngine& engine;
    juce::ComboBox formatBox, qualityBox;
    juce::ToggleButton normalizeToggle { "Normalize (as loud as it can be without clipping)" };
    FolderField folderField { "Default folder for exported songs" };

    void fillQuality();
    void showFolder();
};

//==============================================================================
class AppearancePage  : public PreferencesPage
{
public:
    explicit AppearancePage (AudioEngine&);

private:
    AudioEngine& engine;
    juce::ComboBox sizeBox;
};

//==============================================================================
class AdvancedPage  : public PreferencesPage
{
public:
    /** `onReset` runs after every preference was put back to its default
        (the window shows the page again, so its controls follow). */
    AdvancedPage (AudioEngine&, juce::ApplicationCommandManager&, std::function<void()> onReset);
    ~AdvancedPage() override;

private:
    class ShortcutList;

    AudioEngine& engine;
    std::function<void()> onReset;
    IconButton resetButton { "Reset all preferences", std::nullopt, "Reset All Preferences..." };
    std::unique_ptr<ShortcutList> shortcuts;
};

} // namespace ikaros
