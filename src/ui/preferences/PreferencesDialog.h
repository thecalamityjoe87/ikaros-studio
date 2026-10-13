#pragma once

#include "engine/AudioEngine.h"
#include "ui/preferences/PreferencesPage.h"
#include "ui/widgets/IconButton.h"

namespace ikaros
{

//==============================================================================
/**
    The Preferences window (Settings on macOS): app-wide choices, one page
    per section, with the list of sections on the left. A change applies the
    moment it's made; Done only closes the window. Tempo, key and time
    signature stay with the session.

    Drawn inside the main window over a dimmed background, like the other
    dialogs. Deletes itself when closed.
*/
class PreferencesDialog  : public juce::Component,
                           private juce::ListBoxModel
{
public:
    enum class Page { general, audioMidi, recording, editing, soundsPlugins, exportSong, appearance, advanced };

    /** Opens the window over `parent` on a page. `openSoundLibrary` is called
        (after the window has closed) by the Sound Library button. If a
        Preferences window is already open, it just shows that page. */
    static void show (juce::Component& parent, AudioEngine&, juce::ApplicationCommandManager&, Page,
                      std::function<void()> openSoundLibrary);

    /** What the window is called: Preferences, or Settings on macOS. */
    static const char* getName();

    ~PreferencesDialog() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    PreferencesDialog (AudioEngine&, juce::ApplicationCommandManager&, std::function<void()> openSoundLibrary);

    AudioEngine& engine;
    juce::ApplicationCommandManager& commands;
    std::function<void()> openSoundLibrary;

    juce::ListBox pageList;
    std::unique_ptr<PreferencesPage> page;
    int currentPage = -1;
    IconButton doneButton { "Done", std::nullopt, "Done" };
    juce::Rectangle<int> panel, sidebar, pageArea, titleArea;

    void showPage (int index);   // always builds the page again
    void close();

    // ListBoxModel: the list of sections
    int getNumRows() override;
    void paintListBoxItem (int row, juce::Graphics&, int width, int height, bool selected) override;
    void selectedRowsChanged (int lastRowSelected) override;
    juce::String getNameForRow (int row) override;

    static constexpr int panelWidth = 820, panelHeight = 600, sidebarWidth = 200, padding = 28;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PreferencesDialog)
};

} // namespace ikaros
