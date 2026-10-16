#include "engine/Preferences.h"

namespace ikaros
{

namespace
{
    // Kept in Tracktion's settings file before Preferences existed, under the same names.
    const char* const legacyKeys[] { "midiCycleTakes", "monitoringWarningShown",
                                     "exportFormat", "exportNormalize", "exportFolder",
                                     "exportQuality.wav", "exportQuality.flac", "exportQuality.ogg", "exportQuality.mp3" };
}

Preferences::Preferences (const juce::File& settingsFile, juce::PropertiesFile* legacy)
{
    const bool firstRun = ! settingsFile.existsAsFile();

    juce::PropertiesFile::Options options;
    options.storageFormat = juce::PropertiesFile::storeAsXML;
    options.millisecondsBeforeSaving = 0;   // every change is written at once

    settingsFile.getParentDirectory().createDirectory();
    file = std::make_unique<juce::PropertiesFile> (settingsFile, options);

    if (firstRun)
    {
        if (legacy != nullptr)
        {
            for (auto* key : legacyKeys)
                if (legacy->containsKey (key))
                    file->setValue (key, legacy->getValue (key));

            // Tracktion's name for the count-in setting. It also holds one
            // or two *beats*, which the app never offered: they count as off.
            if (legacy->containsKey ("countInMode"))
                file->setValue (settings::countIn.key, juce::jlimit (0, 2, legacy->getIntValue ("countInMode")));
        }

        file->save();   // so the file exists, and a later reset doesn't import again
    }
}

Preferences::~Preferences() = default;

bool Preferences::get (BoolSetting s) const          { return file->getBoolValue (s.key, s.defaultValue); }
int Preferences::get (IntSetting s) const            { return file->getIntValue (s.key, s.defaultValue); }
juce::String Preferences::get (TextSetting s) const  { return file->getValue (s.key, s.defaultValue); }

void Preferences::set (BoolSetting s, bool value)
{
    file->setValue (s.key, value);
    changed (s.key);
}

void Preferences::set (IntSetting s, int value)
{
    file->setValue (s.key, value);
    changed (s.key);
}

void Preferences::set (TextSetting s, const juce::String& value)
{
    file->setValue (s.key, value);
    changed (s.key);
}

juce::StringArray Preferences::getList (TextSetting s) const
{
    juce::StringArray list;
    list.addLines (get (s));
    list.removeEmptyStrings();
    return list;
}

void Preferences::setList (TextSetting s, const juce::StringArray& list)
{
    set (s, list.joinIntoString ("\n"));
}

juce::File Preferences::getFolder (TextSetting s, const juce::File& fallback) const
{
    const auto path = get (s);

    if (path.isNotEmpty() && juce::File::isAbsolutePath (path))
        if (juce::File folder (path); folder.isDirectory())
            return folder;

    return fallback;
}

float Preferences::getInterfaceScale() const
{
    return juce::jlimit (1.0f, 2.0f, (float) get (settings::interfaceScale) / 100.0f);
}

void Preferences::resetAll()
{
    file->clear();
    file->save();
    changed ({});
}

juce::File Preferences::getFile() const    { return file->getFile(); }

void Preferences::changed (const juce::String& key)
{
    listeners.call ([&key] (Listener& l) { l.preferenceChanged (key); });
}

} // namespace ikaros
