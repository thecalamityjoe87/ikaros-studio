#pragma once

// Internal helpers shared by the engine's controllers (TrackController,
// InputRouter, RegionEditor, NoteEditor, EffectsController).
//
// Only .cpp files in src/engine include this: it pulls in Tracktion, which
// the UI must never see. Everything is `inline` so it can live in a header
// without multiple-definition link errors.

#include "engine/EngineTypes.h"
#include <tracktion_engine/tracktion_engine.h>
#include <array>

namespace te = tracktion;   // the conventional short alias used in Tracktion's own examples

namespace ikaros::detail
{

// Extra properties we store on each track's ValueTree. Tracktion saves
// unknown properties with the Edit, so these round-trip through
// .tracktionedit files for free.
inline const juce::Identifier soundNameProp ("ikarosSound");
inline const juce::Identifier categoryProp  ("ikarosCategory");
inline const juce::Identifier kindProp      ("ikarosKind");      // "audio" for audio tracks
inline const juce::Identifier inputProp     ("ikarosInput");     // audio input device ID
inline const juce::Identifier monitorProp   ("ikarosMonitor");   // hear the input live
inline const juce::Identifier iconProp      ("ikarosIcon");      // the track icon the user chose (empty = automatic)
inline const juce::Identifier soundIdProp   ("ikarosSoundId");   // the library sound (SoundInfo::id)
inline const juce::Identifier autoNameProp  ("ikarosAutoName");  // the name the app gave the track (so we know if the user renamed it)
inline const juce::Identifier busProp       ("ikarosBus");       // on the hidden tracks that hold the shared effect buses: the bus number

// On a plugin's state: this plugin is the track's instrument. Tracktion only
// knows a third-party plugin is an instrument once it has loaded, so a
// missing one would otherwise look like an effect.
inline const juce::Identifier instrumentProp ("ikarosInstrument");

inline bool isAudioTrack (const te::AudioTrack& t)   { return t.state.getProperty (kindProp).toString() == "audio"; }

// The shared Reverb and Delay buses each live on a track of their own,
// hidden from the user (see SendBuses).
inline bool isBusTrack (const te::Track& t)          { return t.state.hasProperty (busProp); }

inline bool isInstrumentPlugin (te::Plugin& p)       { return p.isSynth() || (bool) p.state.getProperty (instrumentProp, false); }

// "plugin:<id>" names a third-party plugin (PluginInfo::id) wherever a
// plugin type is expected; anything else is one of Tracktion's built-in types.
inline const juce::String thirdPartyPrefix ("plugin:");

// Track colours from the design tokens: blue, orange, green, purple, yellow.
// They live here (not in the UI tokens) because a track's colour is part of
// the saved session.
inline const std::array<juce::uint32, 5> trackPalette { 0xff5aa9ff, 0xffff8f5a, 0xff4fd18b, 0xffc78bff, 0xffffd25a };

inline te::AudioTrack* asAudioTrack (te::Track* t)   { return dynamic_cast<te::AudioTrack*> (t); }

inline te::FourOscPlugin* findSynth (te::AudioTrack& t)
{
    // findFirstPluginOfType is a template: it walks the track's plugin list
    // and dynamic_casts each one to the requested type.
    return t.pluginList.findFirstPluginOfType<te::FourOscPlugin>();
}

// The tracks the user sees, in order: every audio track except the hidden bus tracks.
inline juce::Array<te::AudioTrack*> getUserTracks (te::Edit& edit)
{
    auto tracks = te::getAudioTracks (edit);
    tracks.removeIf ([] (te::AudioTrack* t) { return isBusTrack (*t); });
    return tracks;
}

// A track the user can see, by ID (nullptr for the hidden bus tracks).
inline te::AudioTrack* findTrack (te::Edit* edit, TrackId id)
{
    if (edit == nullptr || id == 0)
        return nullptr;

    auto* track = asAudioTrack (te::findTrackForID (*edit, te::EditItemID::fromRawID (id)));
    return track != nullptr && ! isBusTrack (*track) ? track : nullptr;
}

// Creates (but doesn't insert) a plugin: a built-in type such as "reverb",
// or "plugin:<id>" for a third-party plugin from the scan. nullptr if it
// can't be made (e.g. the plugin is no longer installed).
inline te::Plugin::Ptr createPlugin (te::Edit& edit, const juce::String& type)
{
    if (type.startsWith (thirdPartyPrefix))
    {
        auto& known = edit.engine.getPluginManager().knownPluginList;

        if (auto desc = known.getTypeForIdentifierString (type.substring (thirdPartyPrefix.length())))
            return edit.getPluginCache().createNewPlugin (te::ExternalPlugin::xmlTypeName, *desc);

        return {};
    }

    return edit.getPluginCache().createNewPlugin (type, {});
}

// The type string createPlugin() takes to make another one of these.
inline juce::String getPluginTypeString (te::Plugin& p)
{
    if (auto* external = dynamic_cast<te::ExternalPlugin*> (&p))
        return thirdPartyPrefix + external->getIdentifierString();

    return p.getPluginType();
}

inline te::Clip* findClip (te::Edit* edit, RegionId id)
{
    if (edit == nullptr || id == 0)
        return nullptr;

    return te::findClipForID (*edit, te::EditItemID::fromRawID (id));
}

inline te::MidiClip* findMidiClip (te::Edit* edit, RegionId id)
{
    return dynamic_cast<te::MidiClip*> (findClip (edit, id));
}

inline te::TimePosition beatToTime (te::Edit& edit, double beat)
{
    return edit.tempoSequence.toTime (te::BeatPosition::fromBeats (beat));
}

inline double timeToBeat (te::Edit& edit, te::TimePosition time)
{
    return edit.tempoSequence.toBeats (time).inBeats();
}

// Note positions in the UI are relative to the region start. Tracktion stores
// them relative to the clip's *content* start, which is earlier once a
// region has been trimmed from the left. This is the difference, in beats.
inline double contentOffsetBeats (te::Clip& clip)
{
    return (clip.getContentStartBeat() - clip.getStartBeat()).inBeats();
}

// Sets a plugin parameter so that Undo can restore it.
//
// Parameters are backed by a property on the plugin's state, which
// Tracktion writes through the Edit's UndoManager. A parameter still at its
// default often has no stored property yet, so undoing the first change
// would delete the property rather than restore a value. Storing the
// current value first (outside the undo history) keeps the saved session
// explicit. AudioEngine::undo() then pushes restored values back into the
// live parameters.
inline void setParameterUndoably (te::Plugin& plugin, te::AutomatableParameter& param,
                                  const juce::Identifier& stateProperty, float value)
{
    if (! plugin.state.hasProperty (stateProperty))
        plugin.state.setProperty (stateProperty, param.getCurrentValue(), nullptr);

    param.setParameter (value, juce::sendNotification);
}

inline const juce::String keyboardDeviceName ("Ikaros On-Screen Keyboard");

// The MIDI inputs we route to tracks: hardware keyboards and our own
// on-screen keyboard. (Tracktion's "All MIDI Ins" merges the hardware
// ones, so routing it too would double every note.)
inline bool isRoutedMidiInput (te::InputDevice& d)
{
    return d.getDeviceType() == te::InputDevice::physicalMidiDevice
        || (d.getDeviceType() == te::InputDevice::virtualMidiDevice && d.getName() == keyboardDeviceName);
}

} // namespace ikaros::detail
