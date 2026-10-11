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
    scanner->onFinished = [this] { owner.notifyModelChanged(); };
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

    for (auto& d : owner.getTeEngine().getPluginManager().knownPluginList.getTypes())
        if (d.isInstrument == instruments)
            result.push_back (makeInfo (d));

    std::sort (result.begin(), result.end(), [] (const PluginInfo& a, const PluginInfo& b)
    {
        return a.name.compareNatural (b.name) < 0;
    });

    return result;
}

std::vector<PluginInfo> PluginLibrary::getInstruments() const   { return list (true); }
std::vector<PluginInfo> PluginLibrary::getEffects() const       { return list (false); }

std::optional<PluginInfo> PluginLibrary::findPlugin (const juce::String& id) const
{
    if (auto d = owner.getTeEngine().getPluginManager().knownPluginList.getTypeForIdentifierString (id))
        return makeInfo (*d);

    return std::nullopt;
}

void PluginLibrary::scan (bool rescanEverything)      { scanner->start ({}, rescanEverything); }
void PluginLibrary::scanFolders (const juce::StringArray& folders)   { scanner->start (folders, false); }
void PluginLibrary::cancelScan()                      { scanner->cancel(); }
PluginScanStatus PluginLibrary::getScanStatus() const { return scanner->getStatus(); }

juce::StringArray PluginLibrary::getSkippedPlugins() const
{
    juce::StringArray names;

    for (auto& file : owner.getTeEngine().getPluginManager().knownPluginList.getBlacklistedFiles())
        names.add (juce::File::isAbsolutePath (file) ? juce::File (file).getFileNameWithoutExtension() : file);

    return names;
}

void PluginLibrary::retrySkippedPlugins()
{
    owner.getTeEngine().getPluginManager().knownPluginList.clearBlacklistedFiles();
    scan();
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
