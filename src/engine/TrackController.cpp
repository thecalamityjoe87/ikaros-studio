#include "engine/TrackController.h"
#include "engine/AudioEngine.h"
#include "engine/DrummerHelpers.h"
#include "engine/SoundLibrary.h"
#include "engine/TrackHelpers.h"
#include <map>

namespace te = tracktion;

namespace ikaros
{

using namespace detail;

//==============================================================================
// Holds a LevelMeasurer::Client registered with a track's level-meter plugin.
// The Client is filled in by the audio thread; we read-and-clear it from the UI.
struct MeterTap
{
    explicit MeterTap (te::LevelMeterPlugin& p) : plugin (&p)
    {
        p.measurer.addClient (client);
    }

    ~MeterTap()
    {
        if (auto* meter = dynamic_cast<te::LevelMeterPlugin*> (plugin.get()))
            meter->measurer.removeClient (client);
    }

    te::Plugin::Ptr plugin;          // ref-counted pointer keeps the plugin alive while we're attached
    te::LevelMeasurer::Client client;

    // Reading the client clears it, so a second reader on the same UI tick
    // (the track header and the Mixer both show a track's level) would get
    // nothing. Readings within a few milliseconds share the first one.
    StereoLevel lastLevel;
    juce::uint32 lastReadMs = 0;
};

struct TrackController::Impl
{
    std::map<TrackId, std::unique_ptr<MeterTap>> meters;
};

//==============================================================================
TrackController::TrackController (AudioEngine& o)
    : impl (std::make_unique<Impl>()), owner (o),
      inputRouter (std::make_unique<InputRouter> (o)),
      // Importing audio may need a new audio track: RegionEditor gets a
      // function that makes one, instead of depending on this class.
      regionEditor (std::make_unique<RegionEditor> (o, [this] (const juce::String& name) { return createAudioTrack (name); })),
      takeEditor (std::make_unique<TakeEditor> (o)),
      noteEditor (std::make_unique<NoteEditor> (o)),
      effectsController (std::make_unique<EffectsController> (o)),
      sendBuses (std::make_unique<SendBuses> (o)),
      eqController (std::make_unique<EqController> (o)),
      automationEditor (std::make_unique<AutomationEditor> (o, *sendBuses)),
      sectionEditor (std::make_unique<SectionEditor> (o)),
      flexEditor (std::make_unique<FlexEditor> (o)),
      drummerEditor (std::make_unique<DrummerEditor> (o, *effectsController, *sectionEditor))
{
}

TrackController::~TrackController() = default;

void TrackController::sessionChanged()
{
    impl->meters.clear();
    selectedTrack = 0;
    inputRouter->sessionChanged();    // held notes, armed and selected track
    regionEditor->sessionChanged();   // clipboard, waveform cache
    flexEditor->sessionChanged();     // which files the hits were found in
    drummerEditor->sessionChanged();  // an XY pad drag in progress
}

//==============================================================================
std::vector<TrackInfo> TrackController::getTracks() const
{
    std::vector<TrackInfo> result;
    auto* edit = owner.getEdit();

    if (edit == nullptr)
        return result;

    auto tracks = getUserTracks (*edit);
    bool anySolo = false;

    for (auto* t : tracks)
        anySolo = anySolo || t->isSolo (false);

    int index = 0;

    for (auto* t : tracks)
    {
        TrackInfo info;
        info.id = t->itemID.getRawID();
        info.index = index++;
        info.name = t->getName();
        info.kind = isAudioTrack (*t) ? TrackKind::audio : TrackKind::instrument;
        info.drummer = isDrummerTrack (*t);
        info.soundName = t->state.getProperty (soundNameProp, "Basic Synth").toString();
        info.category = t->state.getProperty (categoryProp, "Synths").toString();
        info.icon = t->state.getProperty (iconProp).toString();
        info.soundIcon = t->state.getProperty (soundIconProp).toString();
        info.soundId = effectsController->getSoundId (info.id);
        info.reverbSend = sendBuses->getSendLevel (info.id, SendBuses::reverb);
        info.delaySend = sendBuses->getSendLevel (info.id, SendBuses::delay);

        if (info.kind == TrackKind::audio)
        {
            // Show the input it records from, e.g. "Input 1".
            info.soundName = "No input";
            auto settings = inputRouter->getRecordingSettings (info.id);
            info.monitoring = settings.monitoring;

            for (auto& in : inputRouter->getAudioInputs())
                if (in.id == settings.inputId)
                    info.soundName = in.name;
        }
        info.muted = t->isMuted (false);
        info.solo = t->isSolo (false);
        info.silencedBySolo = anySolo && ! info.solo;
        info.armed = info.id == inputRouter->getArmedTrack();

        if (auto* vol = t->getVolumePlugin())
        {
            info.volume = vol->getSliderPos();
            info.volumeDb = vol->getVolumeDb();
            info.pan = vol->getPan();
        }

        info.automationShown = automationEditor->isLaneShown (info.id);
        info.automationTarget = automationEditor->getLaneTarget (info.id);
        info.automated = automationEditor->getAutomatedTargets (info.id);

        result.push_back (info);
    }

    return result;
}

std::optional<TrackInfo> TrackController::getTrack (TrackId id) const
{
    for (auto& t : getTracks())
        if (t.id == id)
            return t;

    return std::nullopt;
}

TrackId TrackController::addInstrumentTrack()
{
    owner.beginUndoStep ("Add Track");
    return createInstrumentTrack();
}

TrackId TrackController::makeInstrumentFromRegion (RegionId regionId)
{
    const auto region = regionEditor->getRegion (regionId);
    const auto excerpt = regionEditor->getSourceExcerpt (regionId);

    if (! region.has_value() || ! excerpt.has_value())
        return 0;

    owner.beginUndoStep ("Make Instrument from Region");
    return createSamplerTrack (excerpt->file, excerpt->startSeconds, excerpt->lengthSeconds, region->name);
}

TrackId TrackController::addSamplerTrack (const juce::File& file)
{
    owner.beginUndoStep ("Add Sampler Track");
    return createSamplerTrack (file, 0.0, 0.0, {});
}

TrackId TrackController::createSamplerTrack (const juce::File& file, double startSeconds, double lengthSeconds,
                                             const juce::String& name)
{
    const auto id = createInstrumentTrack();

    if (id == 0 || ! effectsController->setOwnSound (id, file, startSeconds, lengthSeconds, name, false))
    {
        // The file can't be read: take the new track back out.
        owner.getEdit()->getUndoManager().undoCurrentTransactionOnly();
        return 0;
    }

    return id;
}

TrackId TrackController::createInstrumentTrack()
{
    auto* edit = owner.getEdit();
    auto existing = getUserTracks (*edit);
    te::Track* last = existing.isEmpty() ? nullptr : existing.getLast();

    // TrackInsertPoint (parent, preceding): nullptr parent = top level of the Edit.
    auto track = edit->insertNewAudioTrack (te::TrackInsertPoint (nullptr, last), nullptr);

    if (track == nullptr)
        return 0;

    const auto id = track->itemID.getRawID();
    const int number = existing.size() + 1;

    // The instrument, as part of this undo step. No effects by default; the
    // user adds them from the Plug-ins inspector.
    effectsController->setSound (id, SoundLibrary::defaultSoundId, false);

    // Named "Instrument N" rather than after the sound. It's still the
    // app's name, so picking a sound renames it.
    const auto name = "Instrument " + juce::String (number);
    track->setName (name);
    track->state.setProperty (autoNameProp, name, &edit->getUndoManager());
    return id;
}

TrackId TrackController::addAudioTrack()
{
    owner.beginUndoStep ("Add Audio Track");
    return createAudioTrack ({});
}

TrackId TrackController::addGuitarTrack (bool bass)
{
    const juce::String icon = bass ? "bass" : "guitar";
    int count = 1;

    for (auto* t : getUserTracks (*owner.getEdit()))
        if (isAudioTrack (*t) && t->state.getProperty (iconProp).toString() == icon)
            ++count;

    owner.beginUndoStep (bass ? "Add Bass Track" : "Add Guitar Track");
    const auto id = createAudioTrack ((bass ? "Bass " : "Guitar ") + juce::String (count));

    if (auto* t = findTrack (owner.getEdit(), id))
        t->state.setProperty (iconProp, icon, &owner.getEdit()->getUndoManager());

    // The Amp, as part of this undo step, on the voice for the icon set above.
    effectsController->addAmp (id, -1, false);
    return id;
}

TrackId TrackController::addVocalTrack()
{
    int count = 1;

    for (auto* t : getUserTracks (*owner.getEdit()))
        if (isAudioTrack (*t) && t->state.getProperty (iconProp).toString() == "vocals")
            ++count;

    owner.beginUndoStep ("Add Vocals Track");
    const auto id = createAudioTrack ("Vocals " + juce::String (count));

    if (auto* t = findTrack (owner.getEdit(), id))
        t->state.setProperty (iconProp, "vocals", &owner.getEdit()->getUndoManager());

    return id;
}

TrackId TrackController::addDrummerTrack()
{
    auto* edit = owner.getEdit();
    owner.beginUndoStep ("Add Drummer Track");

    auto existing = getUserTracks (*edit);
    te::Track* last = existing.isEmpty() ? nullptr : existing.getLast();
    auto track = edit->insertNewAudioTrack (te::TrackInsertPoint (nullptr, last), nullptr);

    if (track == nullptr)
        return 0;

    const auto id = track->itemID.getRawID();
    auto& um = edit->getUndoManager();
    const auto name = uniqueTrackName (*edit, "Drummer");
    track->setName (name);
    track->state.setProperty (autoNameProp, name, &um);
    track->state.setProperty (iconProp, "drummer", &um);

    // The kind, the kit and the first region (all in this undo step).
    drummerEditor->setUpDrummerTrack (id);
    return id;
}

// Adds an audio track after the last track, as part of the current undo step.
// An empty name gives the next "Audio N".
TrackId TrackController::createAudioTrack (const juce::String& name)
{
    auto* edit = owner.getEdit();
    auto existing = getUserTracks (*edit);
    te::Track* last = existing.isEmpty() ? nullptr : existing.getLast();
    auto track = edit->insertNewAudioTrack (te::TrackInsertPoint (nullptr, last), nullptr);

    if (track == nullptr)
        return 0;

    auto& um = edit->getUndoManager();
    int audioCount = 1;

    for (auto* t : existing)
        if (isAudioTrack (*t))
            ++audioCount;

    track->setName (name.isNotEmpty() ? name : "Audio " + juce::String (audioCount));
    track->state.setProperty (kindProp, "audio", &um);
    track->state.setProperty (categoryProp, "Audio Files", &um);

    // Record from the first input by default, without monitoring (a
    // microphone near the speakers would otherwise feed back).
    auto inputs = inputRouter->getAudioInputs();
    track->state.setProperty (inputProp, inputs.empty() ? juce::String() : inputs.front().id, &um);
    track->state.setProperty (monitorProp, false, &um);

    return track->itemID.getRawID();
}

void TrackController::deleteTrack (TrackId id)
{
    if (auto* track = findTrack (owner.getEdit(), id))
    {
        owner.beginUndoStep ("Delete Track");
        impl->meters.erase (id);

        // Where the track was, so the selection can move to the one that
        // takes its place (or the one above, if it was the last).
        auto before = getTracks();
        const auto index = std::find_if (before.begin(), before.end(), [id] (const TrackInfo& t) { return t.id == id; })
                             - before.begin();

        inputRouter->trackDeleted (id);

        owner.getEdit()->deleteTrack (track);

        if (selectedTrack == id)
        {
            auto remaining = getTracks();
            setSelectedTrack (remaining.empty() ? 0 : remaining[(size_t) juce::jmin ((int) index, (int) remaining.size() - 1)].id);
        }
    }
}

TrackId TrackController::duplicateTrack (TrackId id, bool withRegions)
{
    auto* edit = owner.getEdit();
    auto* source = findTrack (edit, id);

    if (source == nullptr)
        return 0;

    owner.beginUndoStep (withRegions ? "Duplicate Track" : "New Track with Duplicate Settings");

    // copyTrack deep-copies the track's state (plugins, volume, our ikaros*
    // properties, clips) with fresh IDs, and inserts it after the source.
    auto copy = asAudioTrack (edit->copyTrack (source, te::TrackInsertPoint (nullptr, source)).get());

    if (copy == nullptr)
        return 0;

    if (! withRegions)
    {
        auto clips = copy->getClips();   // a copy: removing a clip changes the track's own list

        for (auto* clip : clips)
            clip->removeFromParent();
    }

    // "Solo Violin" → "Solo Violin 2". If the app named the source (after
    // its sound), the copy's name is the app's too, so picking a different
    // sound for it renames it.
    const auto newName = uniqueTrackName (*edit, source->getName(), copy);
    copy->setName (newName);

    if (source->state.getProperty (autoNameProp).toString() == source->getName())
        copy->state.setProperty (autoNameProp, newName, &edit->getUndoManager());

    return copy->itemID.getRawID();
}

void TrackController::renameTrack (TrackId id, const juce::String& newName)
{
    auto name = newName.trim();

    if (auto* t = findTrack (owner.getEdit(), id))
    {
        if (name.isNotEmpty() && name != t->getName())
        {
            owner.beginUndoStep ("Rename Track");
            t->setName (name);

            // The user's name now: picking a sound won't replace it.
            t->state.setProperty (autoNameProp, juce::String(), &owner.getEdit()->getUndoManager());
        }
    }
}

void TrackController::setTrackIcon (TrackId id, const juce::String& iconKey)
{
    if (auto* t = findTrack (owner.getEdit(), id))
    {
        if (t->state.getProperty (iconProp).toString() != iconKey)
        {
            owner.beginUndoStep ("Track Icon");
            t->state.setProperty (iconProp, iconKey, &owner.getEdit()->getUndoManager());
        }
    }
}

bool TrackController::canMoveTrack (TrackId id, int direction) const
{
    auto tracks = getTracks();

    for (size_t i = 0; i < tracks.size(); ++i)
        if (tracks[i].id == id)
            return juce::isPositiveAndBelow ((int) i + direction, (int) tracks.size());

    return false;
}

void TrackController::moveTrack (TrackId id, int direction)
{
    if (! canMoveTrack (id, direction))
        return;

    auto* edit = owner.getEdit();
    auto tracks = getUserTracks (*edit);
    auto* track = findTrack (edit, id);
    const int index = tracks.indexOf (track);
    owner.beginUndoStep (direction < 0 ? "Move Track Up" : "Move Track Down");

    // The Edit holds other (non-audio) tracks too, so always move relative to
    // a neighbouring audio track: down = after the next one; up = move the one
    // above to after this one.
    if (direction > 0)
        edit->moveTrack (track, te::TrackInsertPoint (nullptr, tracks[index + 1]));
    else
        edit->moveTrack (tracks[index - 1], te::TrackInsertPoint (nullptr, track));
}

void TrackController::setSelectedTrack (TrackId id)
{
    const auto newSelection = findTrack (owner.getEdit(), id) != nullptr ? id : TrackId {};

    // With nothing armed, the input follows the selection: release held keys
    // first so the previous track's synth doesn't keep sounding.
    if (inputRouter->getArmedTrack() == 0 && newSelection != selectedTrack)
        inputRouter->allNotesOff();

    selectedTrack = newSelection;
    inputRouter->setSelectedTrack (selectedTrack);   // re-routes the inputs
}

// Mute and solo are about what you're listening to, not changes to the song,
// so they aren't undoable (Tracktion keeps them out of the undo history too).
// They're still saved with the session.
void TrackController::setMute (TrackId id, bool mute)
{
    if (auto* t = findTrack (owner.getEdit(), id))
        t->setMute (mute);
}

void TrackController::setSolo (TrackId id, bool solo)
{
    if (auto* t = findTrack (owner.getEdit(), id))
        t->setSolo (solo);
}

void TrackController::setVolume (TrackId id, float pos)
{
    // No beginUndoStep() here: the UI calls AudioEngine::beginUndoStep() on
    // drag start, so a whole fader move is one undo step.
    if (auto* t = findTrack (owner.getEdit(), id))
        if (auto* vol = t->getVolumePlugin())
            setParameterUndoably (*vol, *vol->volParam, te::IDs::volume, juce::jlimit (0.0f, 1.0f, pos));
}

void TrackController::setPan (TrackId id, float pan)
{
    if (auto* t = findTrack (owner.getEdit(), id))
        if (auto* vol = t->getVolumePlugin())
            setParameterUndoably (*vol, *vol->panParam, te::IDs::pan, juce::jlimit (-1.0f, 1.0f, pan));
}

float TrackController::getMasterVolume() const
{
    auto* edit = owner.getEdit();
    auto vol = edit != nullptr ? edit->getMasterVolumePlugin() : nullptr;
    return vol != nullptr ? vol->getSliderPos() : 0.0f;
}

float TrackController::getMasterVolumeDb() const
{
    auto* edit = owner.getEdit();
    auto vol = edit != nullptr ? edit->getMasterVolumePlugin() : nullptr;
    return vol != nullptr ? vol->getVolumeDb() : -100.0f;
}

float TrackController::getFaderPositionForDb (float db)
{
    return te::decibelsToVolumeFaderPosition (db);
}

float TrackController::getDbForFaderPosition (float position)
{
    return te::volumeFaderPositionToDB (position);
}

void TrackController::setMasterVolume (float pos)
{
    // No beginUndoStep(): the UI starts one when the fader drag starts.
    if (auto* edit = owner.getEdit())
        if (auto vol = edit->getMasterVolumePlugin())
            setParameterUndoably (*vol, *vol->volParam, te::IDs::volume, juce::jlimit (0.0f, 1.0f, pos));
}

StereoLevel TrackController::getTrackLevel (TrackId id)
{
    auto* t = findTrack (owner.getEdit(), id);

    if (t == nullptr)
        return {};

    auto& tap = impl->meters[id];

    if (tap == nullptr)
    {
        if (auto* meter = t->getLevelMeterPlugin())
            tap = std::make_unique<MeterTap> (*meter);
        else
            return {};
    }

    const auto now = juce::Time::getMillisecondCounter();

    if (now - tap->lastReadMs > 10)
    {
        tap->lastLevel = { tap->client.getAndClearAudioLevel (0).dB, tap->client.getAndClearAudioLevel (1).dB };
        tap->lastReadMs = now;
    }

    return tap->lastLevel;
}

TrackDetails TrackController::getDetails (TrackId id) const
{
    TrackDetails d;
    auto* edit = owner.getEdit();
    auto* t = findTrack (edit, id);

    if (t == nullptr)
        return d;

    d.outputName = t->getOutput().getOutputName();

    if (isAudioTrack (*t))
    {
        d.pluginName = "None (audio track)";
        auto inputId = inputRouter->getRecordingSettings (id).inputId;

        for (auto& in : inputRouter->getAudioInputs())
            if (in.id == inputId)
                d.midiInputs.add (in.name);   // shown as this track's input

        return d;
    }

    if (auto instrument = effectsController->getInstrument (id))
    {
        if (instrument->isThirdParty)
            d.pluginName = instrument->name + (instrument->disabledByUser ? " (disabled)"
                                                     : instrument->missing ? " (not installed)"
                                                                           : " (" + instrument->manufacturer + ")");
        else
            d.pluginName = instrument->name + " (built-in)";
    }
    else
    {
        d.pluginName = "None";
    }

    for (auto* instance : edit->getAllInputDevices())
        if (isRoutedMidiInput (instance->getInputDevice()))
            if (instance->getTargets().contains (t->itemID))
                d.midiInputs.add (instance->getInputDevice().getName());

    return d;
}

} // namespace ikaros
