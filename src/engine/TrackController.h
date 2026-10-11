#pragma once

#include "engine/EffectsController.h"
#include "engine/InputRouter.h"
#include "engine/NoteEditor.h"
#include "engine/RegionEditor.h"
#include <memory>
#include <optional>

namespace ikaros
{

class AudioEngine;

//==============================================================================
/**
    Track-level operations: creating and deleting tracks, the selected track,
    and the mixer controls (mute, solo, volume, pan, meters).

    It owns the other track-related controllers and hands them out:
        inputs()   - live MIDI/audio input routing, arming, input meters
        regions()  - regions, the region clipboard, audio file import
        notes()    - notes in a MIDI region (the piano roll)
        effects()  - the Sound tab's knobs, the instrument and the effect chain

    Every method runs on the message thread. Mutating methods record into the
    session's UndoManager.
*/
class TrackController
{
public:
    explicit TrackController (AudioEngine&);
    ~TrackController();

    InputRouter& inputs() noexcept                          { return *inputRouter; }
    const InputRouter& inputs() const noexcept              { return *inputRouter; }
    RegionEditor& regions() noexcept                        { return *regionEditor; }
    const RegionEditor& regions() const noexcept            { return *regionEditor; }
    NoteEditor& notes() noexcept                            { return *noteEditor; }
    EffectsController& effects() noexcept                   { return *effectsController; }
    const EffectsController& effects() const noexcept       { return *effectsController; }

    //==============================================================================
    // Tracks
    std::vector<TrackInfo> getTracks() const;
    std::optional<TrackInfo> getTrack (TrackId) const;

    /** Adds an instrument track (Tracktion's built-in 4OSC synth) after the last track. */
    TrackId addInstrumentTrack();

    /** Adds an audio track, for recording a microphone, guitar or other input. */
    TrackId addAudioTrack();
    void deleteTrack (TrackId);

    /** Adds a copy of a track just below it: same kind, sound, effects, mixer
        settings and colour. With withRegions, its regions are copied too
        (GarageBand's "Duplicate Track"); without, it starts empty ("New Track
        with Duplicate Settings"). Returns the new track, or 0. */
    TrackId duplicateTrack (TrackId, bool withRegions);

    void renameTrack (TrackId, const juce::String& newName);
    void setTrackColour (TrackId, juce::Colour);

    /** The icon shown in the track header, as a key such as "guitar" (the UI
        maps keys to pictures). Empty = automatic: follow the sound's category. */
    void setTrackIcon (TrackId, const juce::String& iconKey);

    /** The colours offered for tracks, in the order new tracks use them. */
    static std::vector<juce::Colour> getTrackColours();

    /** Moves a track one place up (-1) or down (+1) the list. */
    void moveTrack (TrackId, int direction);
    bool canMoveTrack (TrackId, int direction) const;

    /** The selected track is the one the on-screen and hardware keyboards play. */
    void setSelectedTrack (TrackId);
    TrackId getSelectedTrack() const noexcept   { return selectedTrack; }

    void setMute (TrackId, bool);
    void setSolo (TrackId, bool);
    void setVolume (TrackId, float sliderPosition0to1);

    /** -1 = left, 0 = centre, 1 = right. Like setVolume, the caller starts
        the undo step (once per drag). */
    void setPan (TrackId, float pan);

    /** The master fader (fader position 0..1, as for tracks). */
    float getMasterVolume() const;
    float getMasterVolumeDb() const;
    void setMasterVolume (float sliderPosition0to1);

    /** Assigns a library preset to a track. Updates the name shown in the UI.
        TODO(milestone-3): load real sampler/plugin presets from the sound library. */
    void setSound (TrackId, const juce::String& category, const juce::String& presetName);

    StereoLevel getTrackLevel (TrackId);

    /** The "Show details" box: plugin, output and inputs. */
    TrackDetails getDetails (TrackId) const;

    //==============================================================================
    // Forwarders, so existing callers keep working. See the classes they
    // forward to for documentation.

    // TODO: callers should use inputs() directly
    void setArmed (TrackId id, bool shouldArm)                               { inputRouter->setArmed (id, shouldArm); }
    TrackId getArmedTrack() const noexcept                                   { return inputRouter->getArmedTrack(); }
    TrackId armForRecording()                                                { return inputRouter->armForRecording(); }
    std::vector<AudioInput> getAudioInputs() const                           { return inputRouter->getAudioInputs(); }
    RecordingSettings getRecordingSettings (TrackId id) const                { return inputRouter->getRecordingSettings (id); }
    void setTrackInput (TrackId id, const juce::String& inputId)             { inputRouter->setTrackInput (id, inputId); }
    void setMonitoring (TrackId id, bool shouldMonitor)                      { inputRouter->setMonitoring (id, shouldMonitor); }
    StereoLevel getInputLevel (TrackId id)                                   { return inputRouter->getInputLevel (id); }
    void noteOn (int noteNumber, float velocity0to1)                         { inputRouter->noteOn (noteNumber, velocity0to1); }
    void noteOff (int noteNumber)                                            { inputRouter->noteOff (noteNumber); }
    void allNotesOff()                                                       { inputRouter->allNotesOff(); }

    // TODO: callers should use regions() directly
    static constexpr double minRegionBeats = RegionEditor::minRegionBeats;
    std::vector<RegionInfo> getRegions (TrackId id) const                    { return regionEditor->getRegions (id); }
    std::optional<RegionInfo> getRegion (RegionId id) const                  { return regionEditor->getRegion (id); }
    RegionId createChordRegion (TrackId id, double startBeat)                { return regionEditor->createChordRegion (id, startBeat); }
    void deleteRegion (RegionId id)                                          { regionEditor->deleteRegion (id); }
    void deleteRegions (const std::vector<RegionId>& ids)                    { regionEditor->deleteRegions (ids); }
    void moveRegions (const std::vector<RegionPlacement>& placements)        { regionEditor->moveRegions (placements); }
    void trimRegion (RegionId id, double newStartBeat, double newEndBeat)    { regionEditor->trimRegion (id, newStartBeat, newEndBeat); }
    std::vector<RegionId> splitRegions (const std::vector<RegionId>& ids, double beat)  { return regionEditor->splitRegions (ids, beat); }
    std::vector<RegionId> duplicateRegions (const std::vector<RegionId>& ids)           { return regionEditor->duplicateRegions (ids); }
    void renameRegion (RegionId id, const juce::String& newName)             { regionEditor->renameRegion (id, newName); }
    void copyRegions (const std::vector<RegionId>& ids)                      { regionEditor->copyRegions (ids); }
    bool canPaste() const                                                    { return regionEditor->canPaste(); }
    std::vector<RegionId> pasteRegions (TrackId track, double beat)          { return regionEditor->pasteRegions (track, beat); }
    bool isSupportedAudioFile (const juce::File& file) const                 { return regionEditor->isSupportedAudioFile (file); }
    std::vector<RegionId> importAudioFiles (TrackId id, const juce::StringArray& paths, double beat)
                                                                             { return regionEditor->importAudioFiles (id, paths, beat); }

    // TODO: callers should use notes() directly
    using NoteIndexMap = NoteEditor::NoteIndexMap;
    int addNote (RegionId id, const NoteInfo& note)                          { return noteEditor->addNote (id, note); }
    NoteIndexMap changeNotes (RegionId id, const std::vector<NoteChange>& changes, const juce::String& undoName)
                                                                             { return noteEditor->changeNotes (id, changes, undoName); }
    void deleteNotes (RegionId id, const std::vector<int>& indices)          { noteEditor->deleteNotes (id, indices); }
    NoteIndexMap quantiseNotes (RegionId id, const std::vector<int>& indices, double gridBeats)
                                                                             { return noteEditor->quantiseNotes (id, indices, gridBeats); }

    // TODO: callers should use effects() directly
    static constexpr int maxEffectsPerTrack = EffectsController::maxEffectsPerTrack;
    static std::vector<EffectType> getAvailableEffects()                     { return EffectsController::getAvailableEffects(); }
    std::vector<SoundControl> getSoundControls (TrackId id) const            { return effectsController->getSoundControls (id); }
    void setSoundControl (TrackId id, int index, float value)                { effectsController->setSoundControl (id, index, value); }
    std::optional<PluginSlot> getInstrument (TrackId id) const               { return effectsController->getInstrument (id); }
    std::vector<PluginSlot> getEffectChain (TrackId id) const                { return effectsController->getEffectChain (id); }
    PluginId addEffect (TrackId id, const juce::String& type, int position = -1)        { return effectsController->addEffect (id, type, position); }
    PluginId replaceEffect (TrackId id, PluginId plugin, const juce::String& newType)   { return effectsController->replaceEffect (id, plugin, newType); }
    void removeEffect (TrackId id, PluginId plugin)                          { effectsController->removeEffect (id, plugin); }
    void moveEffect (TrackId id, PluginId plugin, int newPosition)           { effectsController->moveEffect (id, plugin, newPosition); }
    void setEffectEnabled (TrackId id, PluginId plugin, bool enabled)        { effectsController->setEffectEnabled (id, plugin, enabled); }
    std::vector<ParameterInfo> getEffectParameters (TrackId id, PluginId plugin) const  { return effectsController->getEffectParameters (id, plugin); }
    void setEffectParameter (TrackId id, PluginId plugin, int index, float value)       { effectsController->setEffectParameter (id, plugin, index, value); }

    //==============================================================================
    /** @internal Called by AudioEngine after the Edit has been replaced.
        Resets every controller's per-session state. */
    void sessionChanged();

private:
    struct Impl;                    // Tracktion-typed members (meter taps) live in the .cpp
    std::unique_ptr<Impl> impl;
    AudioEngine& owner;
    TrackId selectedTrack = 0;

    std::unique_ptr<InputRouter> inputRouter;
    std::unique_ptr<RegionEditor> regionEditor;
    std::unique_ptr<NoteEditor> noteEditor;
    std::unique_ptr<EffectsController> effectsController;

    TrackId createAudioTrack (const juce::String& name);
};

} // namespace ikaros
