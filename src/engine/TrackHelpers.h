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

inline bool isAudioTrack (const te::AudioTrack& t)   { return t.state.getProperty (kindProp).toString() == "audio"; }

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

inline te::AudioTrack* findTrack (te::Edit* edit, TrackId id)
{
    if (edit == nullptr || id == 0)
        return nullptr;

    return asAudioTrack (te::findTrackForID (*edit, te::EditItemID::fromRawID (id)));
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
