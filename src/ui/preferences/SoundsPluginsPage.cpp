#include "ui/preferences/PreferencesPages.h"
#include "engine/PluginLibrary.h"
#include "engine/SoundLibrary.h"
#include "ui/theme/Fonts.h"
#include "ui/theme/Tokens.h"

namespace ikaros
{

using namespace tokens;

//==============================================================================
// One switch per plug-in format the app can host (VST3 and LV2, or VST3 and Audio Units on macOS).
class SoundsPluginsPage::FormatToggles  : public juce::Component
{
public:
    explicit FormatToggles (AudioEngine& e) : engine (e)
    {
        const auto disabled = engine.getPreferences().getList (settings::disabledPluginFormats);

        for (auto& name : engine.getPlugins().getFormatNames())
        {
            // JUCE calls Audio Units "AudioUnit".
            auto* toggle = toggles.add (new juce::ToggleButton ((name == "AudioUnit" ? juce::String ("Audio Units") : name) + " plug-ins"));
            toggle->setToggleState (! disabled.contains (name), juce::dontSendNotification);
            toggle->onClick = [this] { save(); };
            addAndMakeVisible (toggle);
            names.add (name);
        }

        setSize (100, juce::jmax (1, toggles.size()) * PreferencesPage::rowHeight);
    }

    void resized() override
    {
        auto r = getLocalBounds();

        for (auto* toggle : toggles)
            toggle->setBounds (r.removeFromTop (PreferencesPage::rowHeight));
    }

private:
    AudioEngine& engine;
    juce::OwnedArray<juce::ToggleButton> toggles;
    juce::StringArray names;

    void save()
    {
        juce::StringArray off;

        for (int i = 0; i < toggles.size(); ++i)
            if (! toggles[i]->getToggleState())
                off.add (names[i]);

        engine.getPreferences().setList (settings::disabledPluginFormats, off);
    }
};

//==============================================================================
// The extra plug-in folders: a list with Add and Remove.
class SoundsPluginsPage::FolderList  : public juce::Component,
                                       private juce::ListBoxModel
{
public:
    explicit FolderList (AudioEngine& e) : engine (e)
    {
        folders = engine.getPreferences().getList (settings::pluginFolders);

        listBox.setModel (this);
        listBox.setRowHeight (28);
        listBox.setColour (juce::ListBox::backgroundColourId, colour::wellBg);
        listBox.setColour (juce::ListBox::outlineColourId, colour::borderSoft);
        listBox.setOutlineThickness (1);
        listBox.setTitle ("Extra plug-in folders");
        addAndMakeVisible (listBox);

        for (auto* b : { &addButton, &removeButton })
        {
            PreferencesPage::styleButton (*b);
            addAndMakeVisible (b);
        }

        removeButton.setEnabled (false);
        addButton.onClick = [this] { chooseFolder(); };
        removeButton.onClick = [this] { removeSelected(); };

        setSize (100, 156);
    }

    void paintOverChildren (juce::Graphics& g) override
    {
        if (folders.isEmpty())
        {
            g.setColour (colour::textSecondary);
            g.setFont (fonts::ui (13.0f));
            g.drawText ("None added", listBox.getBounds().reduced (10, 0).withHeight (28), juce::Justification::centredLeft);
        }
    }

    void resized() override
    {
        auto r = getLocalBounds();
        auto buttons = r.removeFromBottom (30);
        r.removeFromBottom (6);
        listBox.setBounds (r);
        addButton.setBounds (buttons.removeFromLeft (110));
        buttons.removeFromLeft (8);
        removeButton.setBounds (buttons.removeFromLeft (90));
    }

private:
    AudioEngine& engine;
    juce::StringArray folders;
    juce::ListBox listBox;
    IconButton addButton { "Add a plug-in folder", std::nullopt, "Add Folder..." };
    IconButton removeButton { "Remove the selected folder", std::nullopt, "Remove" };
    std::unique_ptr<juce::FileChooser> chooser;

    int getNumRows() override   { return folders.size(); }

    void paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected) override
    {
        if (! juce::isPositiveAndBelow (row, folders.size()))
            return;

        if (selected)
        {
            g.setColour (colour::selectedRow);
            g.fillRect (0, 0, width, height);
        }

        g.setColour (colour::text);
        g.setFont (fonts::ui (13.0f));
        g.drawText (folders[row], 10, 0, width - 20, height, juce::Justification::centredLeft, true);
    }

    void selectedRowsChanged (int) override   { removeButton.setEnabled (listBox.getSelectedRow() >= 0); }

    juce::String getNameForRow (int row) override   { return juce::isPositiveAndBelow (row, folders.size()) ? folders[row] : juce::String(); }

    void save()
    {
        engine.getPreferences().setList (settings::pluginFolders, folders);
        listBox.updateContent();
        listBox.repaint();
        repaint();
    }

    void chooseFolder()
    {
        chooser = std::make_unique<juce::FileChooser> ("Add a plug-in folder");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                              [safeThis = SafePointer<FolderList> (this)] (const juce::FileChooser& fc)
        {
            if (safeThis == nullptr || fc.getResult() == juce::File())
                return;

            safeThis->folders.addIfNotAlreadyThere (fc.getResult().getFullPathName());
            safeThis->save();
        });
    }

    void removeSelected()
    {
        const auto row = listBox.getSelectedRow();

        if (juce::isPositiveAndBelow (row, folders.size()))
        {
            folders.remove (row);
            listBox.deselectAllRows();
            save();
        }
    }
};

//==============================================================================
SoundsPluginsPage::SoundsPluginsPage (AudioEngine& e, std::function<void()> openSoundLibrary)
    : engine (e),
      formatToggles (std::make_unique<FormatToggles> (e)),
      folderList (std::make_unique<FolderList> (e))
{
    soundFolderField.onChanged = [this] (const juce::File& folder)
    {
        engine.getPreferences().set (settings::userSoundsFolder, folder.getFullPathName());
        showSoundFolder();
    };
    showSoundFolder();

    styleButton (soundLibraryButton);
    soundLibraryButton.onClick = [open = std::move (openSoundLibrary)] { if (open) open(); };

    styleButton (rescanButton);
    rescanButton.onClick = [this]
    {
        engine.getPlugins().scan (true);
        updateScanState();
    };

    styleButton (clearSkippedButton);
    clearSkippedButton.onClick = [this]
    {
        engine.getPlugins().clearSkippedPlugins();
        updateScanState();
    };

    addHeading ("Sounds");
    addRow ("Sound pack folder", soundFolderField, 0,
            "Where your own and downloaded sound packs are kept. Packs in the old folder aren't moved.");
    addRow ("Sound Library", soundLibraryButton, 220, "Download more instruments and loops.");

    addHeading ("Plug-ins");
    addRow ("Plug-in formats", *formatToggles, 240, "A format that's turned off is left out of scans, and its plug-ins out of the lists.");
    addRow ("Extra folders", *folderList, 0, "Searched as well as the standard plug-in folders.");
    addRow ("Scan", rescanButton, 220, {});
    addRow ("Skipped plug-ins", clearSkippedButton, 220, {});

    updateScanState();
    startTimerHz (2);
}

SoundsPluginsPage::~SoundsPluginsPage() = default;

void SoundsPluginsPage::showSoundFolder()
{
    soundFolderField.setFolder (engine.getPreferences().getFolder (settings::userSoundsFolder, {}),
                                SoundLibrary::getUserSoundsFolder());
}

void SoundsPluginsPage::updateScanState()
{
    const auto status = engine.getPlugins().getScanStatus();
    const auto skipped = engine.getPlugins().getSkippedPlugins();

    const auto scanNote = status.scanning
        ? "Scanning " + status.current + " (" + juce::String (status.done) + " of " + juce::String (status.total) + ")..."
        : juce::String ("Checks every plug-in again, including any that were skipped.");

    const auto skippedNote = skipped.isEmpty()
        ? juce::String ("No plug-ins have been skipped.")
        : juce::String (skipped.size()) + (skipped.size() == 1 ? " plug-in was" : " plug-ins were")
              + " skipped because they crashed or hung while being scanned: " + skipped.joinIntoString (", ")
              + ". Clearing the list lets the next scan try again.";

    rescanButton.setEnabled (! status.scanning);
    clearSkippedButton.setEnabled (! skipped.isEmpty());

    // Only when the text changes: setting a note lays the page out again.
    if (scanNote != shownScanNote)
    {
        shownScanNote = scanNote;
        setNote (rescanButton, scanNote);
    }

    if (skippedNote != shownSkippedNote)
    {
        shownSkippedNote = skippedNote;
        setNote (clearSkippedButton, skippedNote);
    }
}

void SoundsPluginsPage::timerCallback()
{
    updateScanState();
}

} // namespace ikaros
