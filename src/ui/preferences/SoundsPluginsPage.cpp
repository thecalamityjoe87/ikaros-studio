#include "ui/preferences/PreferencesPages.h"
#include "engine/PluginLibrary.h"
#include "engine/SoundLibrary.h"
#include "ui/dialogs/AlertDialog.h"
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
            g.setColour (theme().textSecondary);
            g.setFont (fonts::ui (textSize::md));
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
            g.setColour (theme().selectedRow);
            g.fillRect (0, 0, width, height);
        }

        g.setColour (theme().text);
        g.setFont (fonts::ui (textSize::md));
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
// The plug-ins found on this computer, one row each, with a tick to turn each on or off.
// Turned-off ones stay in the list (dimmed) so they can be turned on again.
class SoundsPluginsPage::InstalledList  : public juce::Component,
                                          private juce::ListBoxModel
{
public:
    explicit InstalledList (AudioEngine& e) : engine (e)
    {
        listBox.setModel (this);
        listBox.setRowHeight (28);
        listBox.setOutlineThickness (1);
        listBox.setTitle ("Installed plug-ins");
        addAndMakeVisible (listBox);

        refresh();
        setSize (100, 240);
    }

    /** Reads the list again; does nothing if it hasn't changed. */
    void refresh()
    {
        auto found = engine.getPlugins().getInstalledPlugins();

        const auto same = std::equal (found.begin(), found.end(), plugins.begin(), plugins.end(),
                                      [] (const PluginInfo& a, const PluginInfo& b)
                                      { return a.id == b.id && a.disabled == b.disabled; });
        const auto scanning = engine.getPlugins().getScanStatus().scanning;

        if (same && scanning == wasScanning)
            return;

        plugins = std::move (found);
        wasScanning = scanning;
        listBox.updateContent();
        listBox.repaint();
        repaint();
    }

    void paintOverChildren (juce::Graphics& g) override
    {
        if (plugins.empty())
        {
            g.setColour (theme().textSecondary);
            g.setFont (fonts::ui (textSize::md));
            g.drawText (wasScanning ? "Scanning..." : "No plug-ins found",
                        listBox.getBounds().reduced (10, 0).withHeight (28), juce::Justification::centredLeft);
        }
    }

    void resized() override   { listBox.setBounds (getLocalBounds()); }

private:
    AudioEngine& engine;
    std::vector<PluginInfo> plugins;
    bool wasScanning = false;
    juce::ListBox listBox;

    int getNumRows() override   { return (int) plugins.size(); }

    void paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected) override
    {
        if (! juce::isPositiveAndBelow (row, (int) plugins.size()))
            return;

        const auto& p = plugins[(size_t) row];
        const auto format = p.format == "AudioUnit" ? juce::String ("Audio Unit") : p.format;
        auto r = juce::Rectangle<int> (0, 0, width, height).reduced (10, 0);

        if (selected)
        {
            g.setColour (theme().selectedRow);
            g.fillRect (0, 0, width, height);
        }

        // The tick
        const auto box = r.removeFromLeft (16).withSizeKeepingCentre (16, 16).toFloat();
        r.removeFromLeft (10);
        g.setColour (p.disabled ? theme().textSecondary : theme().text);
        g.drawRoundedRectangle (box.reduced (0.5f), 3.0f, 1.0f);

        if (! p.disabled)
        {
            juce::Path tick;
            tick.startNewSubPath (box.getX() + 3.5f, box.getCentreY());
            tick.lineTo (box.getCentreX() - 0.5f, box.getBottom() - 4.0f);
            tick.lineTo (box.getRight() - 3.0f, box.getY() + 4.0f);
            g.strokePath (tick, juce::PathStrokeType (1.6f));
        }

        g.setFont (fonts::ui (textSize::md));

        auto kind = r.removeFromRight (80);
        auto fmt = r.removeFromRight (90);
        auto maker = r.removeFromRight (r.getWidth() / 3);

        g.setColour (p.disabled ? theme().textSecondary : theme().text);
        g.drawText (p.name, r, juce::Justification::centredLeft, true);

        g.setColour (theme().textSecondary);
        g.drawText (p.manufacturer, maker, juce::Justification::centredLeft, true);
        g.drawText (format, fmt, juce::Justification::centredLeft, true);
        g.drawText (p.disabled ? "Off" : p.isInstrument ? "Instrument" : "Effect", kind, juce::Justification::centredLeft, true);
    }

    void toggle (int row)
    {
        if (! juce::isPositiveAndBelow (row, (int) plugins.size()))
            return;

        const auto plugin = plugins[(size_t) row];

        if (plugin.disabled)
        {
            engine.getPlugins().setPluginEnabled (plugin.id, true);
            refresh();
            return;
        }

        // Tracks that use it keep it until the session opens next: say so first.
        const auto users = engine.getPlugins().getTracksUsing (plugin.id);

        if (users.isEmpty())
        {
            engine.getPlugins().setPluginEnabled (plugin.id, false);
            refresh();
            return;
        }

        auto trackList = users.size() <= 3 ? users.joinIntoString (", ")
                                           : users.joinIntoString (", ", 0, 3) + " and " + juce::String (users.size() - 3) + " more";

        auto options = juce::MessageBoxOptions()
                           .withTitle ("Turn off " + plugin.name + "?")
                           .withMessage (plugin.name + " is used in this session (" + trackList + "). It stays loaded until the session "
                                         "is opened again; then those tracks show it as disabled and it won't make a sound. "
                                         "Its settings are kept, and turning it back on brings it back.")
                           .withButton ("Turn Off", 1)
                           .withButton ("Cancel", 0)
                           .withAssociatedComponent (this);

        AlertDialog::show (options, [safeThis = SafePointer<InstalledList> (this), &lib = engine.getPlugins(), id = plugin.id] (int result)
        {
            if (result != 1)
                return;

            lib.setPluginEnabled (id, false);

            if (safeThis != nullptr)
                safeThis->refresh();
        });
    }

    void listBoxItemClicked (int row, const juce::MouseEvent& e) override
    {
        if (e.x < 36)   // the tick; the rest of the row just selects it
            toggle (row);
    }

    void returnKeyPressed (int row) override   { toggle (row); }

    juce::String getNameForRow (int row) override
    {
        if (! juce::isPositiveAndBelow (row, (int) plugins.size()))
            return {};

        const auto& p = plugins[(size_t) row];
        return p.name + ", " + p.manufacturer + ", " + p.format + (p.isInstrument ? ", instrument" : ", effect")
                 + (p.disabled ? ", off" : ", on");
    }
};

//==============================================================================
SoundsPluginsPage::SoundsPluginsPage (AudioEngine& e, std::function<void()> openSoundLibrary)
    : engine (e),
      formatToggles (std::make_unique<FormatToggles> (e)),
      folderList (std::make_unique<FolderList> (e)),
      installedList (std::make_unique<InstalledList> (e))
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
    addRow ("Installed", *installedList, 0, "The plug-ins found on this computer. Untick one to turn it off: it's left out of the lists and can't be picked, and "
            "sessions that use it stop loading it the next time they open. Tick it again to turn it back on.");
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
    installedList->refresh();
    updateScanState();
}

} // namespace ikaros
