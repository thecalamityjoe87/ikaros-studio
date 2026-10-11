#include "engine/TrackController.h"
#include "engine/AudioEngine.h"
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
      noteEditor (std::make_unique<NoteEditor> (o)),
      effectsController (std::make_unique<EffectsController> (o))
{
}

TrackController::~TrackController() = default;

void TrackController::sessionChanged()
{
    impl->meters.clear();
    selectedTrack = 0;
    inputRouter->sessionChanged();    // held notes, armed and selected track
    regionEditor->sessionChanged();   // clipboard, waveform cache
}

//==============================================================================
std::vector<TrackInfo> TrackController::getTracks() const
{
    std::vector<TrackInfo> result;
    auto* edit = owner.getEdit();

    if (edit == nullptr)
        return result;

    auto tracks = te::getAudioTracks (*edit);
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
        info.soundName = t->state.getProperty (soundNameProp, "Basic Synth").toString();
        info.category = t->state.getProperty (categoryProp, "Synths").toString();

        if (info.kind == TrackKind::audio)
        {
            // Show the input it records from, e.g. "Input 1".
            info.soundName = "No input";
            auto inputId = inputRouter->getRecordingSettings (info.id).inputId;

            for (auto& in : inputRouter->getAudioInputs())
                if (in.id == inputId)
                    info.soundName = in.name;
        }
        info.colour = t->getColour();
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
    auto* edit = owner.getEdit();
    owner.beginUndoStep ("Add Track");

    auto existing = te::getAudioTracks (*edit);
    te::Track* last = existing.isEmpty() ? nullptr : existing.getLast();

    // TrackInsertPoint (parent, preceding): nullptr parent = top level of the Edit.
    auto track = edit->insertNewAudioTrack (te::TrackInsertPoint (nullptr, last), nullptr);

    if (track == nullptr)
        return 0;

    auto& um = edit->getUndoManager();
    const int number = existing.size() + 1;
    track->setName ("Instrument " + juce::String (number));
    track->setColour (juce::Colour (trackPalette[(size_t) (number - 1) % trackPalette.size()]));
    track->state.setProperty (soundNameProp, "Basic Synth", &um);
    track->state.setProperty (categoryProp, "Synths", &um);

    // Built-in 4OSC synth as the instrument. createNewPlugin returns a
    // ref-counted Plugin::Ptr; dynamic_cast gives us the concrete type.
    if (auto synthPtr = edit->getPluginCache().createNewPlugin (te::FourOscPlugin::xmlTypeName, {}))
    {
        if (auto* synth = dynamic_cast<te::FourOscPlugin*> (synthPtr.get()))
        {
            // A friendlier starting sound than the raw init patch: low-pass
            // filter on (so "Brightness" does something) and a little room.
            synth->filterTypeValue = 1;
            synth->filterFreq->setNormalisedParameter (0.75f, juce::sendNotification);
            synth->ampRelease->setParameter (0.3f, juce::sendNotification);
            synth->reverbOnValue = true;
            synth->reverbMix->setParameter (0.2f, juce::sendNotification);
        }

        track->pluginList.insertPlugin (synthPtr, 0, nullptr);
    }

    // No effects by default; the user adds them from the Plug-ins inspector.
    return track->itemID.getRawID();
}

TrackId TrackController::addAudioTrack()
{
    owner.beginUndoStep ("Add Audio Track");
    return createAudioTrack ({});
}

// Adds an audio track after the last track, as part of the current undo step.
// An empty name gives the next "Audio N".
TrackId TrackController::createAudioTrack (const juce::String& name)
{
    auto* edit = owner.getEdit();
    auto existing = te::getAudioTracks (*edit);
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
    track->setColour (juce::Colour (trackPalette[(size_t) existing.size() % trackPalette.size()]));
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

        inputRouter->trackDeleted (id);

        owner.getEdit()->deleteTrack (track);

        if (selectedTrack == id)
        {
            auto remaining = getTracks();
            setSelectedTrack (remaining.empty() ? 0 : remaining.front().id);
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

    copy->setName (source->getName() + " copy");
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
        }
    }
}

void TrackController::setTrackColour (TrackId id, juce::Colour c)
{
    if (auto* t = findTrack (owner.getEdit(), id))
    {
        if (t->getColour() != c)
        {
            owner.beginUndoStep ("Track Colour");
            t->setColour (c);
        }
    }
}

std::vector<juce::Colour> TrackController::getTrackColours()
{
    return { trackPalette.begin(), trackPalette.end() };
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
    auto tracks = te::getAudioTracks (*edit);
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

void TrackController::setMute (TrackId id, bool mute)
{
    if (auto* t = findTrack (owner.getEdit(), id))
    {
        owner.beginUndoStep ("Mute");
        t->setMute (mute);
    }
}

void TrackController::setSolo (TrackId id, bool solo)
{
    if (auto* t = findTrack (owner.getEdit(), id))
    {
        owner.beginUndoStep ("Solo");
        t->setSolo (solo);
    }
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

void TrackController::setMasterVolume (float pos)
{
    // No beginUndoStep(): the UI starts one when the fader drag starts.
    if (auto* edit = owner.getEdit())
        if (auto vol = edit->getMasterVolumePlugin())
            setParameterUndoably (*vol, *vol->volParam, te::IDs::volume, juce::jlimit (0.0f, 1.0f, pos));
}

void TrackController::setSound (TrackId id, const juce::String& category, const juce::String& presetName)
{
    if (auto* t = findTrack (owner.getEdit(), id))
    {
        owner.beginUndoStep ("Change Sound");
        auto* um = &owner.getEdit()->getUndoManager();
        t->state.setProperty (soundNameProp, presetName, um);
        t->state.setProperty (categoryProp, category, um);
        t->setName (presetName);
        // TODO(milestone-3): swap the instrument (sampler / plugin preset) here.
    }
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

    return { tap->client.getAndClearAudioLevel (0).dB,
             tap->client.getAndClearAudioLevel (1).dB };
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

    d.pluginName = findSynth (*t) != nullptr ? juce::String ("4OSC (built-in synth)") : juce::String ("None");

    for (auto* instance : edit->getAllInputDevices())
        if (isRoutedMidiInput (instance->getInputDevice()))
            if (instance->getTargets().contains (t->itemID))
                d.midiInputs.add (instance->getInputDevice().getName());

    return d;
}

} // namespace ikaros
