#pragma once

#include <juce_data_structures/juce_data_structures.h>
#include <memory>
#include <string_view>

namespace ikaros
{

// A setting is its key in the file plus the value it has until the user
// changes it. Defining each one as a constant (see `settings` below) means
// the key and the default are written down once.
struct BoolSetting  { const char* key; bool defaultValue; };
struct IntSetting   { const char* key; int defaultValue; };
struct TextSetting  { const char* key; const char* defaultValue; };

//==============================================================================
/** Every app-wide setting, with its default. Tempo, key and time signature
    aren't here: they belong to the session. Where a default says "current
    behaviour", it's what the app did before the setting existed. */
namespace settings
{
    // General
    namespace OnStartup { enum { sessionChooser, reopenLastSession }; }
    inline constexpr IntSetting  onStartup           { "onStartup", OnStartup::sessionChooser };
    inline constexpr TextSetting sessionFolder       { "sessionFolder", "" };         // empty: Documents/Ikaros Studio

    // Audio & MIDI
    namespace Velocity { enum { soft, normal, hard }; }
    inline constexpr IntSetting  keyboardVelocity    { "keyboardVelocity", Velocity::normal };
    inline constexpr BoolSetting monitoringWarningShown { "monitoringWarningShown", false };
    namespace DeviceChange { enum { ask, alwaysSwitch, neverAsk }; }
    inline constexpr IntSetting  deviceChange        { "deviceChange", DeviceChange::ask };   // plugging in or unplugging an audio device
    inline constexpr BoolSetting muteWhileTuning     { "muteWhileTuning", true };    // the tuner silences the input it's listening to

    // Recording
    inline constexpr IntSetting  countIn             { "countIn", 0 };                // bars: 0 (off), 1 or 2
    inline constexpr BoolSetting midiCycleTakes      { "midiCycleTakes", false };     // false: merge the passes
    inline constexpr BoolSetting autoArmTrack        { "autoArmTrack", true };

    // Editing
    namespace Grid { enum { bar, beat }; }
    inline constexpr BoolSetting snapEnabled         { "snapEnabled", true };
    inline constexpr IntSetting  snapGrid            { "snapGrid", Grid::beat };
    inline constexpr BoolSetting followPlayhead      { "followPlayhead", true };
    inline constexpr BoolSetting tempoLaneShown      { "tempoLaneShown", false };   // the tempo lane under the ruler
    namespace Wheel { enum { scroll, zoomWithModifier }; }
    inline constexpr IntSetting  wheelMode           { "wheelMode", Wheel::zoomWithModifier };

    // Sounds & Plug-ins
    inline constexpr TextSetting userSoundsFolder    { "userSoundsFolder", "" };      // empty: Documents/Ikaros Studio/Sounds
    inline constexpr TextSetting pluginFolders       { "pluginFolders", "" };         // one folder per line, searched as well as the standard ones
    inline constexpr TextSetting disabledPluginFormats { "disabledPluginFormats", "" };   // one format name per line
    inline constexpr TextSetting disabledPlugins     { "disabledPlugins", "" };       // one plug-in ID (PluginInfo::id) per line
    inline constexpr TextSetting freesoundKey        { "freesoundKey", "" };          // the user's own Freesound API key

    // Export (also the last choices made in the Export window)
    inline constexpr TextSetting exportFormat        { "exportFormat", "wav" };
    inline constexpr BoolSetting exportNormalize     { "exportNormalize", false };
    inline constexpr TextSetting exportFolder        { "exportFolder", "" };          // empty: the Music folder

    /** The quality chosen for one format: bits for WAV and FLAC, kbps for OGG and MP3.
        `extension` is the format's file extension ("wav", "flac", "ogg" or "mp3"). */
    inline constexpr IntSetting exportQuality (std::string_view extension)
    {
        return extension == "mp3"  ? IntSetting { "exportQuality.mp3", 320 }
             : extension == "ogg"  ? IntSetting { "exportQuality.ogg", 192 }
             : extension == "flac" ? IntSetting { "exportQuality.flac", 24 }
                                   : IntSetting { "exportQuality.wav", 24 };
    }

    // Appearance
    namespace ThemeChoice { enum { light, dark, matchSystem }; }
    inline constexpr IntSetting  themeChoice         { "theme", ThemeChoice::matchSystem };   // Preferences > Appearance > Theme
    inline constexpr IntSetting  interfaceScale      { "interfaceScale", 100 };       // percent
    namespace TrackHeight { enum { compact, tall }; }
    inline constexpr IntSetting  trackHeight         { "trackHeight", TrackHeight::compact };   // View > Compact/Tall Tracks
}

//==============================================================================
/**
    The app's settings: one small file in the platform's config folder
    (JUCE's PropertiesFile). Every change is written straight away, so there's
    no OK or Apply, and listeners hear about it at once.

    A setting that isn't in the file has its default, so a file from an older
    version (without the newer keys) just works, and resetting is emptying it.

    The first time the file is made, the values the app used to keep in its
    older settings file are copied over, so nobody loses their choices.

    Message thread only.
*/
class Preferences
{
public:
    /** `legacy`: the older settings file to take values from if `file`
        doesn't exist yet. It's only read, here. */
    explicit Preferences (const juce::File& file, juce::PropertiesFile* legacy = nullptr);
    ~Preferences();

    bool get (BoolSetting) const;
    int get (IntSetting) const;
    juce::String get (TextSetting) const;

    void set (BoolSetting, bool);
    void set (IntSetting, int);
    void set (TextSetting, const juce::String&);

    /** A text setting holding a list, one entry per line. */
    juce::StringArray getList (TextSetting) const;
    void setList (TextSetting, const juce::StringArray&);

    /** The folder a text setting names, or `fallback` if it's empty or the
        folder is gone. */
    juce::File getFolder (TextSetting, const juce::File& fallback) const;

    /** The interface size as a factor: 1.0 (100%) to 2.0 (200%). */
    float getInterfaceScale() const;

    /** Puts every setting back to its default. */
    void resetAll();

    juce::File getFile() const;

    struct Listener
    {
        virtual ~Listener() = default;

        /** A setting changed, or (empty key) all of them were reset. Called
            at once, from set(). */
        virtual void preferenceChanged (const juce::String& key) = 0;
    };

    void addListener (Listener* l)       { listeners.add (l); }
    void removeListener (Listener* l)    { listeners.remove (l); }

private:
    std::unique_ptr<juce::PropertiesFile> file;
    juce::ListenerList<Listener> listeners;

    void changed (const juce::String& key);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Preferences)
};

} // namespace ikaros
