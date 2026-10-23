#pragma once

#include "engine/EngineTypes.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_events/juce_events.h>
#include <memory>
#include <optional>

namespace ikaros
{

class AudioEngine;
class PluginScanner;

//==============================================================================
/**
    The third-party plug-ins (VST3, LV2, Audio Units) this computer has.

    Scanning runs in the background (see PluginScanner); the results are kept
    in Tracktion's settings, so they're there straight away next time. Plug-ins
    that crashed or hung during a scan are skipped from then on, until the
    user asks to try them again.

    Every method runs on the message thread. When the list changes, the
    AudioEngine sends a change message like for any other model change.
*/
class PluginLibrary  : private juce::ChangeListener
{
public:
    explicit PluginLibrary (AudioEngine&);
    ~PluginLibrary() override;

    std::vector<PluginInfo> getInstruments() const;
    std::vector<PluginInfo> getEffects() const;

    /** The plug-in with this ID, unless the user turned it off. */
    std::optional<PluginInfo> findPlugin (const juce::String& id) const;

    /** Every plug-in of the formats that are turned on, including the ones
        the user turned off, for the Preferences list. */
    std::vector<PluginInfo> getInstalledPlugins() const;

    /** Turns a plug-in off or on. A turned-off plug-in is left out of the
        lists and can't be picked; sessions that already use it keep it. */
    void setPluginEnabled (const juce::String& id, bool enabled);

    /** Looks for new or changed plug-ins in the standard folders. With
        rescanEverything, every plug-in is checked again, including skipped ones. */
    void scan (bool rescanEverything = false);

    /** Scans just these folders (for tests). */
    void scanFolders (const juce::StringArray& folders);

    void cancelScan();
    PluginScanStatus getScanStatus() const;

    /** Plug-ins skipped because they crashed or hung a scan. */
    juce::StringArray getSkippedPlugins() const;

    /** Forgets the skipped plug-ins and scans again. */
    void retrySkippedPlugins();

    /** Forgets the skipped plug-ins, so the next scan tries them again. */
    void clearSkippedPlugins();

    /** The plug-in formats this app hosts, as the lists name them ("VST3", "LV2", "AudioUnit"). */
    juce::StringArray getFormatNames() const;

    /** Names of the tracks (and "Master") in the open session that use this plug-in. */
    juce::StringArray getTracksUsing (const juce::String& id) const;

    /** Called when a session is about to load: from now on, plug-ins turned
        off in the Preferences aren't loaded. (Turning one off while a session
        is open waits for the next time one is loaded, so a plug-in is never
        taken out from under it.) */
    void applyDisabledPlugins();

    /** The list of turned-off plug-ins changed: plug-ins that were turned
        back on load again at once. */
    void disabledPluginsChanged();

    /** Whether applyDisabledPlugins() has stopped this plug-in from loading. */
    bool isBlocked (const juce::PluginDescription&) const;

    /** The plug-in folders or formats in the Preferences changed: lists
        leave out disabled formats, and a scan picks up the new folders. */
    void preferencesChanged();

    /** For main(): see PluginScanner::runWorkerIfRequested. */
    static bool runScanWorkerIfRequested (const juce::String& commandLine);

private:
    AudioEngine& owner;
    std::unique_ptr<PluginScanner> scanner;
    bool rescanWhenDone = false;   // preferences changed during a scan
    juce::StringArray blocked;          // IDs of the plug-ins that don't load (see applyDisabledPlugins)
    juce::String appliedSearch = "|";   // the folders and formats the last scan used (none, at first)

    /** The extra folders and the disabled formats, from the Preferences. */
    std::pair<juce::StringArray, juce::StringArray> getSearchSettings() const;

    std::vector<PluginInfo> list (bool instruments) const;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginLibrary)
};

} // namespace ikaros
