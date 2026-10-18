#include "ui/preferences/PreferencesPages.h"
#include "engine/SongExporter.h"
#include "ui/theme/Fonts.h"
#include "ui/theme/Tokens.h"

namespace ikaros
{

using namespace tokens;

//==============================================================================
GeneralPage::GeneralPage (AudioEngine& e) : engine (e)
{
    auto& prefs = engine.getPreferences();

    fillChoices (startupBox, { "Show the session chooser", "Reopen the last session" }, prefs.get (settings::onStartup));
    startupBox.setTitle ("On startup");
    startupBox.onChange = [this] { engine.getPreferences().set (settings::onStartup, startupBox.getSelectedItemIndex()); };

    sessionFolderField.onChanged = [this] (const juce::File& folder)
    {
        engine.getPreferences().set (settings::sessionFolder, folder.getFullPathName());
        showSessionFolder();
    };
    showSessionFolder();

    styleButton (clearRecentButton);
    clearRecentButton.setEnabled (! engine.getRecentSessions().empty());
    clearRecentButton.onClick = [this]
    {
        engine.clearRecentSessions();
        clearRecentButton.setEnabled (false);
    };

    addRow ("On startup", startupBox, 260);
    addRow ("New sessions go in", sessionFolderField, 0, "Where the Save and Open windows start.");
    addRow ("Recent sessions", clearRecentButton, 220,
            "Empties the list in the session chooser. The sessions themselves stay where they are.");
}

void GeneralPage::showSessionFolder()
{
    sessionFolderField.setFolder (engine.getPreferences().getFolder (settings::sessionFolder, {}),
                                  AudioEngine::getStandardSessionFolder());
}

//==============================================================================
AudioMidiPage::AudioMidiPage (AudioEngine& e) : engine (e), devices (e)
{
    auto& prefs = engine.getPreferences();

    fillChoices (velocityBox, { "Soft", "Normal", "Hard" }, prefs.get (settings::keyboardVelocity));
    velocityBox.setTitle ("Keyboard velocity");
    velocityBox.onChange = [this] { engine.getPreferences().set (settings::keyboardVelocity, velocityBox.getSelectedItemIndex()); };

    styleButton (warningButton);
    warningButton.onClick = [this]
    {
        engine.getPreferences().set (settings::monitoringWarningShown, false);
        updateWarningButton();
    };

    fillChoices (deviceChangeBox, { "Ask me", "Switch automatically", "Never ask" }, prefs.get (settings::deviceChange));
    deviceChangeBox.setTitle ("Audio device changes");
    deviceChangeBox.onChange = [this] { engine.getPreferences().set (settings::deviceChange, deviceChangeBox.getSelectedItemIndex()); };

    addWide (devices);
    addRow ("Audio device changes", deviceChangeBox, 200,
            "What to do when you plug in an audio device, or unplug the one in use.");
    addRow ("Keyboard velocity", velocityBox, 160,
            "How hard the on-screen keyboard and your computer's keys play. A MIDI keyboard sends its own.");
    addRow ("Monitoring", warningButton, 240,
            "Turning on monitoring warns about feedback the first time. This brings that warning back.");
    updateWarningButton();
}

void AudioMidiPage::updateWarningButton()
{
    // Nothing to do while the warning hasn't been shown yet.
    warningButton.setEnabled (engine.getPreferences().get (settings::monitoringWarningShown));
}

//==============================================================================
RecordingPage::RecordingPage (AudioEngine& e) : engine (e)
{
    auto& prefs = engine.getPreferences();

    fillChoices (countInBox, { "Off", "1 bar", "2 bars" }, engine.getCountInBars());
    countInBox.setTitle ("Count-in");
    countInBox.onChange = [this] { engine.setCountInBars (countInBox.getSelectedItemIndex()); };

    fillChoices (cycleBox, { "Merge every pass into one region", "Keep each pass as a take" },
                 prefs.get (settings::midiCycleTakes) ? 1 : 0);
    cycleBox.setTitle ("MIDI loop recording");
    cycleBox.onChange = [this] { engine.setMidiCycleTakes (cycleBox.getSelectedItemIndex() == 1); };

    autoArmToggle.setToggleState (prefs.get (settings::autoArmTrack), juce::dontSendNotification);
    autoArmToggle.onClick = [this] { engine.getPreferences().set (settings::autoArmTrack, autoArmToggle.getToggleState()); };

    addRow ("Count-in", countInBox, 160,
            "Clicks before recording starts, so you can get ready. The Count-in button on the toolbar turns it on and off.");
    addRow ("MIDI loop recording", cycleBox, 300, "With the loop on, recording MIDI makes a pass each time round.");
    addRow ("Arming", autoArmToggle, 0,
            "On: selecting a track, or pressing Record, arms it. Off: Record only records onto tracks you armed "
            "yourself with their record button, and does nothing if none is armed.");
}

//==============================================================================
EditingPage::EditingPage (AudioEngine& e) : engine (e)
{
    auto& prefs = engine.getPreferences();

    snapToggle.setToggleState (prefs.get (settings::snapEnabled), juce::dontSendNotification);
    snapToggle.onClick = [this]
    {
        engine.getPreferences().set (settings::snapEnabled, snapToggle.getToggleState());
        gridBox.setEnabled (snapToggle.getToggleState());
    };

    fillChoices (gridBox, { "Bar", "Beat" }, prefs.get (settings::snapGrid));
    gridBox.setTitle ("Grid");
    gridBox.setEnabled (snapToggle.getToggleState());
    gridBox.onChange = [this] { engine.getPreferences().set (settings::snapGrid, gridBox.getSelectedItemIndex()); };

    followToggle.setToggleState (prefs.get (settings::followPlayhead), juce::dontSendNotification);
    followToggle.onClick = [this] { engine.getPreferences().set (settings::followPlayhead, followToggle.getToggleState()); };

   #if JUCE_MAC
    fillChoices (wheelBox, { "Scroll only", "Zoom while Cmd is held" }, prefs.get (settings::wheelMode));
   #else
    fillChoices (wheelBox, { "Scroll only", "Zoom while Ctrl is held" }, prefs.get (settings::wheelMode));
   #endif
    wheelBox.setTitle ("Mouse wheel");
    wheelBox.onChange = [this] { engine.getPreferences().set (settings::wheelMode, wheelBox.getSelectedItemIndex()); };

    addRow ("Snap", snapToggle, 0, "Where moved regions and edges land. You can also change it with the Snap button above the tracks.");
    addRow ("Grid", gridBox, 160);
    addRow ("Playhead", followToggle, 0, "Turns the page when the playhead reaches the edge of the window.");
    addRow ("Mouse wheel", wheelBox, 260, "Shift and the wheel always scrolls sideways.");
}

//==============================================================================
namespace
{
    const ExportFormat exportFormats[] { ExportFormat::wav, ExportFormat::mp3, ExportFormat::flac, ExportFormat::ogg };
    const char* const exportFormatNames[] { "WAV", "MP3 (small, plays everywhere)", "FLAC (lossless, smaller)", "OGG Vorbis (small)" };
}

ExportPage::ExportPage (AudioEngine& e) : engine (e)
{
    auto& prefs = engine.getPreferences();

    int selected = 0;

    for (int i = 0; i < juce::numElementsInArray (exportFormats); ++i)
    {
        formatBox.addItem (exportFormatNames[i], i + 1);

        if (SongExporter::getFileExtension (exportFormats[i]) == prefs.get (settings::exportFormat))
            selected = i;
    }

    formatBox.setSelectedItemIndex (selected, juce::dontSendNotification);
    formatBox.setTitle ("Format");
    formatBox.onChange = [this]
    {
        const auto format = exportFormats[juce::jlimit (0, juce::numElementsInArray (exportFormats) - 1, formatBox.getSelectedItemIndex())];
        engine.getPreferences().set (settings::exportFormat, SongExporter::getFileExtension (format));
        fillQuality();
    };

    qualityBox.setTitle ("Quality");
    fillQuality();

    normalizeToggle.setToggleState (prefs.get (settings::exportNormalize), juce::dontSendNotification);
    normalizeToggle.onClick = [this] { engine.getPreferences().set (settings::exportNormalize, normalizeToggle.getToggleState()); };

    folderField.onChanged = [this] (const juce::File& folder)
    {
        engine.getPreferences().set (settings::exportFolder, folder.getFullPathName());
        showFolder();
    };
    showFolder();

    addRow ("Format", formatBox, 260);
    addRow ("Quality", qualityBox, 260);
    addRow ("Loudness", normalizeToggle, 0);
    addRow ("Exports go in", folderField, 0);
    addNote ("These are what the Export window starts with. It remembers your last choices here each time you export.");
}

void ExportPage::fillQuality()
{
    const auto format = exportFormats[juce::jlimit (0, juce::numElementsInArray (exportFormats) - 1, formatBox.getSelectedItemIndex())];
    const auto extension = SongExporter::getFileExtension (format);
    const auto choices = SongExporter::getQualityChoices (format);
    const bool bitRate = format == ExportFormat::ogg || format == ExportFormat::mp3;
    const auto preferred = engine.getPreferences().get (settings::exportQuality (extension.toRawUTF8()));

    qualityBox.onChange = nullptr;
    qualityBox.clear (juce::dontSendNotification);

    for (auto quality : choices)
    {
        auto text = SongExporter::describeQuality (format, quality);

        if (! bitRate)
            text << (quality == 16 ? " (CD quality)" : " (studio quality)");

        qualityBox.addItem (text, quality);   // the item ID is the setting itself
    }

    const bool known = std::find (choices.begin(), choices.end(), preferred) != choices.end();
    qualityBox.setSelectedId (known ? preferred : choices.back(), juce::dontSendNotification);

    qualityBox.onChange = [this, extension]
    {
        engine.getPreferences().set (settings::exportQuality (extension.toRawUTF8()), qualityBox.getSelectedId());
    };
}

void ExportPage::showFolder()
{
    folderField.setFolder (engine.getPreferences().getFolder (settings::exportFolder, {}),
                           juce::File::getSpecialLocation (juce::File::userMusicDirectory));
}

//==============================================================================
namespace
{
    constexpr int interfaceSizes[] { 100, 125, 150, 175, 200 };   // percent
}

AppearancePage::AppearancePage (AudioEngine& e) : engine (e)
{
    // The nearest size on offer, in case the file holds something else.
    const int current = engine.getPreferences().get (settings::interfaceScale);
    int nearest = 0;

    for (int i = 0; i < juce::numElementsInArray (interfaceSizes); ++i)
    {
        sizeBox.addItem (juce::String (interfaceSizes[i]) + "%", i + 1);

        if (std::abs (interfaceSizes[i] - current) < std::abs (interfaceSizes[nearest] - current))
            nearest = i;
    }

    sizeBox.setSelectedItemIndex (nearest, juce::dontSendNotification);
    sizeBox.setTitle ("Interface size");
    sizeBox.onChange = [this]
    {
        engine.getPreferences().set (settings::interfaceScale, interfaceSizes[juce::jlimit (0, juce::numElementsInArray (interfaceSizes) - 1,
                                                                                      sizeBox.getSelectedItemIndex())]);
    };

    addRow ("Interface size", sizeBox, 160, "Takes effect straight away, with no restart. A large size needs a large screen.");
}

//==============================================================================
// The shortcuts the command manager knows, read-only: one line each, with the keys on the right.
class AdvancedPage::ShortcutList  : public juce::Component
{
public:
    explicit ShortcutList (juce::ApplicationCommandManager& commands)
    {
        auto* mappings = commands.getKeyMappings();

        for (auto& category : commands.getCommandCategories())
        {
            std::vector<Line> items;

            for (auto id : commands.getCommandsInCategory (category))
            {
                juce::StringArray keys;

                for (auto& key : mappings->getKeyPressesAssignedToCommand (id))
                   #if JUCE_MAC
                    keys.add (key.getTextDescriptionWithIcons());
                   #else
                    keys.add (key.getTextDescription());
                   #endif

                if (! keys.isEmpty())
                    items.push_back ({ commands.getNameOfCommand (id), keys.joinIntoString ("   "), false });
            }

            if (items.empty())
                continue;

            lines.push_back ({ category, {}, true });
            lines.insert (lines.end(), items.begin(), items.end());
        }

        int height = 0;

        for (auto& line : lines)
            height += line.heading ? headingHeight : lineHeight;

        setSize (100, height);
        setTitle ("Keyboard shortcuts");
    }

    void paint (juce::Graphics& g) override
    {
        int y = 0;

        for (auto& line : lines)
        {
            if (line.heading)
            {
                auto r = juce::Rectangle<int> (0, y, getWidth(), headingHeight).withTrimmedTop (10);
                g.setColour (colour::textSecondary);
                g.setFont (fonts::uiSemiBold (textSize::smPlus));
                g.drawText (line.name.toUpperCase(), r, juce::Justification::centredLeft);
                y += headingHeight;
                continue;
            }

            auto r = juce::Rectangle<int> (0, y, getWidth(), lineHeight);
            g.setColour (colour::text);
            g.setFont (fonts::ui (textSize::mdPlus));
            g.drawText (line.name, r, juce::Justification::centredLeft);
            g.setColour (colour::textSecondary);
            g.setFont (fonts::mono (textSize::smPlus));
            g.drawText (line.keys, r, juce::Justification::centredRight);
            y += lineHeight;
        }
    }

private:
    struct Line { juce::String name, keys; bool heading; };

    static constexpr int lineHeight = 24, headingHeight = 32;
    std::vector<Line> lines;
};

AdvancedPage::AdvancedPage (AudioEngine& e, juce::ApplicationCommandManager& commands, std::function<void()> reset)
    : engine (e), onReset (std::move (reset)), shortcuts (std::make_unique<ShortcutList> (commands))
{
    styleButton (resetButton);
    resetButton.onClick = [this]
    {
        auto options = juce::MessageBoxOptions()
                           .withIconType (juce::MessageBoxIconType::WarningIcon)
                           .withTitle ("Reset all preferences?")
                           .withMessage ("Every preference goes back to how it was when you first started the app. "
                                         "Your sessions, sound packs and plug-ins aren't touched.")
                           .withButton ("Reset")
                           .withButton ("Cancel")
                           .withAssociatedComponent (this);

        // The callback gets 1 for the first button. The page may be gone by
        // then (the window closed), and is replaced by onReset, so it copies what it needs first.
        juce::AlertWindow::showAsync (options, [safeThis = SafePointer<AdvancedPage> (this)] (int result)
        {
            if (result != 1 || safeThis == nullptr)
                return;

            safeThis->engine.getPreferences().resetAll();
            auto done = safeThis->onReset;

            if (done)
                done();
        });
    };

    addRow ("Reset", resetButton, 240,
            "Puts every preference back to its default. Your sessions, sound packs and plug-ins aren't affected.");
    addHeading ("Keyboard shortcuts");
    addWide (*shortcuts);
}

AdvancedPage::~AdvancedPage() = default;

} // namespace ikaros
