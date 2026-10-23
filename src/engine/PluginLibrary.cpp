#include "engine/PluginLibrary.h"
#include "engine/AudioEngine.h"
#include "engine/PluginScanner.h"
#include "engine/TrackHelpers.h"

namespace ikaros
{

namespace
{
    PluginInfo makeInfo (const juce::PluginDescription& d)
    {
        return { d.createIdentifierString(), d.name, d.manufacturerName, d.pluginFormatName, d.isInstrument };
    }
}

PluginLibrary::PluginLibrary (AudioEngine& o) : owner (o)
{
    auto& pm = owner.getTeEngine().getPluginManager();

    // Tracktion's scanner would run some formats (LV2) inside the app; ours
    // runs every plug-in in a separate process.
    scanner = std::make_unique<PluginScanner> (pm.pluginFormatManager, pm.knownPluginList);
    scanner->onFinished = [this]
    {
        owner.notifyModelChanged();

        if (std::exchange (rescanWhenDone, false))
            scan();
    };
    scanner->setRecordFile (owner.getTeEngine().getPropertyStorage().getAppPrefsFolder()
                                .getChildFile ("IkarosScannedPlugins.xml"));
    pm.knownPluginList.addChangeListener (this);
}

PluginLibrary::~PluginLibrary()
{
    owner.getTeEngine().getPluginManager().knownPluginList.removeChangeListener (this);
    scanner.reset();
}

std::vector<PluginInfo> PluginLibrary::list (bool instruments) const
{
    std::vector<PluginInfo> result;
    for (auto& info : getInstalledPlugins())
        if (info.isInstrument == instruments && ! info.disabled)
            result.push_back (info);

    std::sort (result.begin(), result.end(), [] (const PluginInfo& a, const PluginInfo& b)
    {
        return a.name.compareNatural (b.name) < 0;
    });

    return result;
}

std::vector<PluginInfo> PluginLibrary::getInstruments() const   { return list (true); }
std::vector<PluginInfo> PluginLibrary::getEffects() const       { return list (false); }

std::vector<PluginInfo> PluginLibrary::getInstalledPlugins() const
{
    std::vector<PluginInfo> result;
    auto& prefs = owner.getPreferences();
    const auto disabledFormats = prefs.getList (settings::disabledPluginFormats);
    const auto disabledPlugins = prefs.getList (settings::disabledPlugins);

    for (auto& d : owner.getTeEngine().getPluginManager().knownPluginList.getTypes())
    {
        if (disabledFormats.contains (d.pluginFormatName))
            continue;

        auto info = makeInfo (d);
        info.disabled = disabledPlugins.contains (info.id);
        result.push_back (std::move (info));
    }

    std::sort (result.begin(), result.end(), [] (const PluginInfo& a, const PluginInfo& b)
    {
        return a.name.compareNatural (b.name) < 0;
    });

    return result;
}

std::optional<PluginInfo> PluginLibrary::findPlugin (const juce::String& id) const
{
    if (auto d = owner.getTeEngine().getPluginManager().knownPluginList.getTypeForIdentifierString (id))
    {
        auto info = makeInfo (*d);

        if (! owner.getPreferences().getList (settings::disabledPlugins).contains (info.id))
            return info;
    }

    return std::nullopt;
}

namespace
{
    // Calls fn (plug-in, track name) for every third-party plug-in in the session.
    template <typename Fn>
    void forEachExternalPlugin (te::Edit* edit, Fn fn)
    {
        if (edit == nullptr)
            return;

        auto visit = [&] (te::PluginList& list, const juce::String& trackName)
        {
            for (auto* p : list)
                if (auto* external = dynamic_cast<te::ExternalPlugin*> (p))
                    fn (*external, trackName);
        };

        visit (edit->getMasterPluginList(), "Master");

        for (auto* t : te::getAudioTracks (*edit))
            visit (t->pluginList, t->getName());
    }
}

juce::StringArray PluginLibrary::getTracksUsing (const juce::String& id) const
{
    juce::StringArray names;

    const auto wanted = owner.getTeEngine().getPluginManager().knownPluginList.getTypeForIdentifierString (id);

    // A loaded plug-in's description can differ from the scan's in its file
    // and IDs, so it's matched by what the user sees.
    forEachExternalPlugin (owner.getEdit(), [&] (te::ExternalPlugin& p, const juce::String& track)
    {
        if (wanted != nullptr && p.desc.name == wanted->name && p.desc.pluginFormatName == wanted->pluginFormatName
              && p.desc.manufacturerName == wanted->manufacturerName)
            names.addIfNotAlreadyThere (track);
    });

    return names;
}

void PluginLibrary::applyDisabledPlugins()
{
    blocked = owner.getPreferences().getList (settings::disabledPlugins);
}

void PluginLibrary::disabledPluginsChanged()
{
    const auto disabled = owner.getPreferences().getList (settings::disabledPlugins);
    bool turnedBackOn = false;

    for (int i = blocked.size(); --i >= 0;)
    {
        if (! disabled.contains (blocked[i]))
        {
            blocked.remove (i);
            turnedBackOn = true;
        }
    }

    if (turnedBackOn)
    {
        // Plug-ins that weren't loaded because of the block load now.
        forEachExternalPlugin (owner.getEdit(), [&] (te::ExternalPlugin& p, const juce::String&)
        {
            if (p.getAudioPluginInstance() == nullptr && ! isBlocked (p.desc))
                p.forceFullReinitialise();
        });

    }

    owner.notifyModelChanged();   // the lists changed
}

bool PluginLibrary::isBlocked (const juce::PluginDescription& description) const
{
    return ! blocked.isEmpty() && blocked.contains (description.createIdentifierString());
}

void PluginLibrary::setPluginEnabled (const juce::String& id, bool enabled)
{
    auto& prefs = owner.getPreferences();
    auto disabled = prefs.getList (settings::disabledPlugins);

    if (enabled)
        disabled.removeString (id);
    else
        disabled.addIfNotAlreadyThere (id);

    prefs.setList (settings::disabledPlugins, disabled);
}

void PluginLibrary::scan (bool rescanEverything)
{
    const auto [folders, disabled] = getSearchSettings();
    scanner->setSearchOptions (folders, disabled);

    if (! scanner->isScanning())
        appliedSearch = folders.joinIntoString ("\n") + "|" + disabled.joinIntoString ("\n");

    scanner->start ({}, rescanEverything);
}

void PluginLibrary::scanFolders (const juce::StringArray& folders)
{
    scanner->setSearchOptions ({}, owner.getPreferences().getList (settings::disabledPluginFormats));
    scanner->start (folders, false);
}

void PluginLibrary::cancelScan()                      { scanner->cancel(); }
PluginScanStatus PluginLibrary::getScanStatus() const { return scanner->getStatus(); }

juce::StringArray PluginLibrary::getSkippedPlugins() const
{
    juce::StringArray names;

    for (auto& file : owner.getTeEngine().getPluginManager().knownPluginList.getBlacklistedFiles())
        names.add (juce::File::isAbsolutePath (file) ? juce::File (file).getFileNameWithoutExtension() : file);

    return names;
}

void PluginLibrary::clearSkippedPlugins()
{
    owner.getTeEngine().getPluginManager().knownPluginList.clearBlacklistedFiles();
}

void PluginLibrary::retrySkippedPlugins()
{
    clearSkippedPlugins();
    scan();
}

juce::StringArray PluginLibrary::getFormatNames() const
{
    juce::StringArray names;

    for (auto* format : owner.getTeEngine().getPluginManager().pluginFormatManager.getFormats())
        if (format->canScanForPlugins())
            names.add (format->getName());

    return names;
}

void PluginLibrary::preferencesChanged()
{
    const auto [folders, disabled] = getSearchSettings();

    // A reset tells everyone everything changed: only a real change needs a scan.
    if (folders.joinIntoString ("\n") + "|" + disabled.joinIntoString ("\n") == appliedSearch)
        return;

    if (scanner->isScanning())
        rescanWhenDone = true;   // onFinished starts it again
    else
        scan();

    owner.notifyModelChanged();   // the lists may have lost (or regained) a format
}

std::pair<juce::StringArray, juce::StringArray> PluginLibrary::getSearchSettings() const
{
    auto& prefs = owner.getPreferences();
    return { prefs.getList (settings::pluginFolders), prefs.getList (settings::disabledPluginFormats) };
}

bool PluginLibrary::runScanWorkerIfRequested (const juce::String& commandLine)
{
    return PluginScanner::runWorkerIfRequested (commandLine);
}

void PluginLibrary::changeListenerCallback (juce::ChangeBroadcaster*)
{
    // New plug-ins found (this arrives on the message thread).
    owner.notifyModelChanged();
}

} // namespace ikaros
