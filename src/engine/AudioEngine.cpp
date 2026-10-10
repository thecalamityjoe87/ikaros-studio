#include "engine/AudioEngine.h"
#include "engine/TrackController.h"

#include <tracktion_engine/tracktion_engine.h>

namespace te = tracktion;   // the conventional short alias used in Tracktion's own examples
using namespace std::literals;

namespace ikaros
{

//==============================================================================
// EngineBehaviour lets an app customise Tracktion's defaults by overriding
// virtual functions.
struct IkarosEngineBehaviour  : public te::EngineBehaviour
{
    // Recording isn't implemented yet, so don't open audio inputs. This also
    // avoids opening a duplex device, which some ALSA→PipeWire setups crash on.
    // TODO(milestone-2): open inputs when the user arms a track for recording.
    bool shouldOpenAudioInputByDefault() override   { return false; }

    // We initialise the device manager ourselves (see AudioEngine::Impl) so we
    // can ask for a plain stereo output instead of "every channel available".
    bool autoInitialiseDeviceManager() override     { return false; }
};

//==============================================================================
// Everything that needs a Tracktion type lives in Impl, so AudioEngine.h stays
// Tracktion-free ("pimpl" = pointer to implementation, a common C++ idiom for
// hiding private members from a header).
struct AudioEngine::Impl  : private juce::ValueTree::Listener,
                            private juce::AsyncUpdater,
                            private juce::ChangeListener,
                            private juce::Timer
{
    explicit Impl (AudioEngine& o) : owner (o)
    {
        // 0 inputs, 2 outputs by default. The user can change devices in
        // File > Audio & MIDI Settings, and Tracktion remembers the choice.
        engine.getDeviceManager().initialise (0, 2);

        // Listen for MIDI devices being plugged in or removed so they can be
        // routed to the selected track.
        engine.getDeviceManager().addChangeListener (this);
    }

    ~Impl() override
    {
        stopTimer();
        engine.getDeviceManager().removeChangeListener (this);
        cancelPendingUpdate();
        detachMasterMeter();
        stopObservingEdit();
    }

    AudioEngine& owner;

    // te::Engine is the root object: device manager, plugin manager, temp files...
    // There should only be one per app.
    te::Engine engine { "IkarosStudio", std::make_unique<te::UIBehaviour>(),
                        std::make_unique<IkarosEngineBehaviour>() };

    std::unique_ptr<te::Edit> edit;
    juce::File sessionFile;
    bool untitled = true;

    // Master meter. The playback context (and its masterLevels measurer) can be
    // re-created by Tracktion at any time, so we hold a WeakReference and
    // re-attach when it changes.
    juce::WeakReference<te::LevelMeasurer> masterMeasurer;
    te::LevelMeasurer::Client masterClient;

    //==============================================================================
    void observeEdit()
    {
        stopObservingEdit();
        observedState = edit->state;
        observedState.addListener (this);
    }

    void stopObservingEdit()
    {
        observedState.removeListener (this);
        observedState = {};
    }

    void detachMasterMeter()
    {
        if (auto* m = masterMeasurer.get())
            m->removeClient (masterClient);

        masterMeasurer = nullptr;
    }

    StereoLevel readMasterLevel()
    {
        if (edit == nullptr)
            return {};

        auto* ctx = edit->getCurrentPlaybackContext();
        auto* current = ctx != nullptr ? &ctx->masterLevels : nullptr;

        if (current != masterMeasurer.get())
        {
            detachMasterMeter();

            if (current != nullptr)
            {
                current->addClient (masterClient);
                masterMeasurer = current;
            }
        }

        if (masterMeasurer == nullptr)
            return {};

        // Reading a Client clears its peak, so several UI widgets polling in
        // the same timer tick would steal each other's readings. Cache briefly.
        auto now = juce::Time::getMillisecondCounter();

        if (now - lastMasterRead > 15)
        {
            lastMasterRead = now;
            lastMasterLevel = { masterClient.getAndClearAudioLevel (0).dB,
                                masterClient.getAndClearAudioLevel (1).dB };
        }

        return lastMasterLevel;
    }

    juce::uint32 lastMasterRead = 0;
    StereoLevel lastMasterLevel;

    //==============================================================================
    // After a session is created or opened, Tracktion finishes setting up on
    // the message loop (plugin init, device routing, its own undo-grouping
    // timer) and some of that lands in the undo history. A beginner shouldn't
    // be able to "undo" invisible setup, so once things settle we start from a
    // clean history, unless the user has already started working.
    bool userHasMadeChanges = false;

    void startFromCleanSlateSoon()
    {
        userHasMadeChanges = false;
        startTimer (600);   // longer than Tracktion's 350 ms undo-grouping timer
    }

private:
    juce::ValueTree observedState;

    // The transport writes its position into the Edit's ValueTree many times a
    // second while playing. Those changes aren't "model" changes, so skip them.
    static bool isTransportState (juce::ValueTree v)
    {
        for (; v.isValid(); v = v.getParent())
            if (v.hasType (te::IDs::TRANSPORT))
                return true;

        return false;
    }

    // ValueTree::Listener callbacks can fire many times for a single user action
    // (and occasionally off the message thread). AsyncUpdater coalesces them
    // into one callback on the message thread.
    void valueTreePropertyChanged (juce::ValueTree& v, const juce::Identifier&) override
    {
        if (! isTransportState (v))
            triggerAsyncUpdate();
    }

    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override          { triggerAsyncUpdate(); }
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override   { triggerAsyncUpdate(); }
    void valueTreeChildOrderChanged (juce::ValueTree&, int, int) override           { triggerAsyncUpdate(); }

    void handleAsyncUpdate() override
    {
        owner.sendChangeMessage();
    }

    void timerCallback() override
    {
        stopTimer();

        if (edit != nullptr && ! userHasMadeChanges)
        {
            edit->getUndoManager().clearUndoHistory();
            edit->resetChangedStatus();
            owner.sendChangeMessage();
        }
    }

    void changeListenerCallback (juce::ChangeBroadcaster*) override
    {
        // Device list changed: re-apply MIDI routing to the selected track.
        if (edit != nullptr)
            owner.getTracks().setSelectedTrack (owner.getTracks().getSelectedTrack());
    }
};

//==============================================================================
AudioEngine::AudioEngine()
    : impl (std::make_unique<Impl> (*this))
{
    trackController = std::make_unique<TrackController> (*this);
    newSession();
}

AudioEngine::~AudioEngine()
{
    // Order matters: stop playback and drop anything that references the Edit
    // before the Edit itself goes away, and the Edit before the Engine.
    if (impl->edit != nullptr)
        impl->edit->getTransport().stop (false, true);

    impl->detachMasterMeter();
    trackController.reset();
    impl->stopObservingEdit();
    impl->edit.reset();
}

te::Engine& AudioEngine::getTeEngine() noexcept    { return impl->engine; }
te::Edit* AudioEngine::getEdit() const noexcept     { return impl->edit.get(); }

//==============================================================================
static juce::File getUntitledSessionFile (te::Engine& engine)
{
    // Tracktion needs a file for every Edit, even an unsaved one. Untitled
    // sessions live in Tracktion's temp folder until the user picks "Save As".
    auto dir = engine.getTemporaryFileManager().getTempDirectory().getChildFile ("UntitledSessions");
    dir.createDirectory();
    return dir.getNonexistentChildFile ("Untitled Session", AudioEngine::sessionFileExtension, false);
}

void AudioEngine::newSession()
{
    auto file = getUntitledSessionFile (impl->engine);
    auto edit = te::createEmptyEdit (impl->engine, file);

    // createEmptyEdit adds a plain audio track; we want our own instrument track instead.
    for (auto* t : te::getAudioTracks (*edit))
        edit->deleteTrack (t);

    // Start every new session with one instrument track so a beginner can play
    // the keyboard straight away.
    loadEdit (std::move (edit), file, true);
    auto first = trackController->addInstrumentTrack();
    trackController->setSelectedTrack (first);

    impl->edit->getUndoManager().clearUndoHistory();
    impl->edit->resetChangedStatus();
    impl->startFromCleanSlateSoon();
    notifyModelChanged();
}

bool AudioEngine::openSession (const juce::File& file)
{
    if (! file.existsAsFile())
        return false;

    auto edit = te::loadEditFromFile (impl->engine, file);

    if (edit == nullptr)
        return false;

    loadEdit (std::move (edit), file, false);

    auto tracks = trackController->getTracks();
    trackController->setSelectedTrack (tracks.empty() ? 0 : tracks.front().id);
    impl->startFromCleanSlateSoon();
    notifyModelChanged();
    return true;
}

void AudioEngine::loadEdit (std::unique_ptr<te::Edit> newEdit, const juce::File& file, bool untitled)
{
    if (impl->edit != nullptr)
    {
        impl->edit->getTransport().stop (false, true);
        trackController->allNotesOff();
    }

    impl->detachMasterMeter();
    impl->stopObservingEdit();

    // Let go of anything that references the old Edit's tracks or plugins
    // (meter taps, held notes) *before* the old Edit is destroyed.
    trackController->sessionChanged();

    impl->edit = std::move (newEdit);
    impl->sessionFile = file;
    impl->untitled = untitled;

    auto& edit = *impl->edit;

    // Tells Tracktion where the session lives (used to resolve relative paths).
    edit.editFileRetriever = [file] { return file; };

    // Keep the audio graph running while stopped so live MIDI (keyboard) is audible.
    edit.playInStopEnabled = true;

    // Allocates the playback graph now rather than on first play.
    edit.getTransport().ensureContextAllocated();

    impl->observeEdit();
}

bool AudioEngine::saveSession()
{
    if (impl->untitled)
        return false;

    return saveSessionAs (impl->sessionFile);
}

bool AudioEngine::saveSessionAs (const juce::File& requestedFile)
{
    auto file = requestedFile.withFileExtension (sessionFileExtension);

    // writeToFile is synchronous, which keeps the menu code simple.
    if (! te::EditFileOperations (*impl->edit).writeToFile (file, false))
        return false;

    impl->sessionFile = file;
    impl->untitled = false;
    impl->edit->editFileRetriever = [file] { return file; };
    impl->edit->resetChangedStatus();
    notifyModelChanged();
    return true;
}

bool AudioEngine::isUntitled() const                { return impl->untitled; }
juce::File AudioEngine::getSessionFile() const      { return impl->sessionFile; }

juce::String AudioEngine::getSessionName() const
{
    return impl->untitled ? juce::String ("Untitled Session")
                          : impl->sessionFile.getFileNameWithoutExtension();
}

bool AudioEngine::hasUnsavedChanges() const
{
    return impl->edit != nullptr && impl->edit->hasChangedSinceSaved();
}

void AudioEngine::notifyModelChanged()
{
    sendChangeMessage();
}

//==============================================================================
// Transport
void AudioEngine::play()
{
    impl->edit->getTransport().play (false);
}

void AudioEngine::stop()
{
    impl->edit->getTransport().stop (false, false);
}

void AudioEngine::togglePlay()
{
    if (impl->edit->getTransport().isPlaying())
        stop();
    else
        play();
}

void AudioEngine::goToStart()
{
    impl->edit->getTransport().setPosition (0s);
}

void AudioEngine::setPositionBeats (double beats)
{
    auto& ts = impl->edit->tempoSequence;
    impl->edit->getTransport().setPosition (ts.toTime (te::BeatPosition::fromBeats (juce::jmax (0.0, beats))));
}

void AudioEngine::setLooping (bool shouldLoop)
{
    auto& transport = impl->edit->getTransport();

    // If there's no loop range yet, loop the first four bars so the button
    // always does something visible.
    if (shouldLoop && transport.getLoopRange().isEmpty())
        setLoopRangeBeats (0.0, beatsPerBar() * 4.0);

    transport.looping = shouldLoop;
}

void AudioEngine::setLoopRangeBeats (double startBeat, double endBeat)
{
    if (endBeat <= startBeat)
        return;

    auto& ts = impl->edit->tempoSequence;
    impl->edit->getTransport().setLoopRange ({ ts.toTime (te::BeatPosition::fromBeats (startBeat)),
                                               ts.toTime (te::BeatPosition::fromBeats (endBeat)) });
}

void AudioEngine::setMetronomeEnabled (bool enabled)
{
    impl->edit->clickTrackEnabled = enabled;
}

void AudioEngine::setBpm (double bpm)
{
    // CachedValue / ValueTree writes inside Tracktion record undo automatically.
    beginUndoStep ("Tempo");
    impl->edit->tempoSequence.getTempo (0)->setBpm (juce::jlimit (20.0, 300.0, bpm));
}

void AudioEngine::setTimeSignature (int numerator, int denominator)
{
    beginUndoStep ("Time Signature");
    auto* sig = impl->edit->tempoSequence.getTimeSig (0);
    sig->numerator = juce::jlimit (1, 16, numerator);
    sig->denominator = denominator;
}

double AudioEngine::beatsPerBar() const
{
    // Beats are quarter notes throughout the app. 6/8 = 3 quarter notes per bar.
    auto* sig = impl->edit->tempoSequence.getTimeSig (0);
    return sig->numerator.get() * 4.0 / sig->denominator.get();
}

TransportState AudioEngine::getTransportState() const
{
    TransportState s;

    if (impl->edit == nullptr)
        return s;

    auto& edit = *impl->edit;
    auto& transport = edit.getTransport();
    auto& ts = edit.tempoSequence;
    auto pos = transport.getPosition();

    s.playing = transport.isPlaying();
    s.looping = transport.looping.get();
    s.metronome = edit.clickTrackEnabled.get();
    s.positionSeconds = pos.inSeconds();
    s.positionBeats = ts.toBeats (pos).inBeats();

    auto barsBeats = ts.toBarsAndBeats (pos);
    s.bar = barsBeats.bars + 1;
    s.beat = barsBeats.getWholeBeats() + 1;

    s.bpm = ts.getTempo (0)->getBpm();
    s.timeSigNumerator = ts.getTimeSig (0)->numerator.get();
    s.timeSigDenominator = ts.getTimeSig (0)->denominator.get();

    auto loop = transport.getLoopRange();
    s.loopStartBeats = ts.toBeats (loop.getStart()).inBeats();
    s.loopEndBeats = ts.toBeats (loop.getEnd()).inBeats();
    return s;
}

juce::String AudioEngine::getKeyName() const
{
    // TODO(milestone-2): let the user change the key from the LCD.
    if (auto* pitch = impl->edit->pitchSequence.getPitch (0))
    {
        auto root = juce::MidiMessage::getMidiNoteName (pitch->getPitch(), true, false, 3);
        auto scale = te::Scale::getShortNameForType (pitch->getScale());
        return root + " " + scale.toLowerCase();
    }

    return "C maj";
}

//==============================================================================
// Undo
// Undo/redo restore plugin parameters' stored values, but Tracktion doesn't
// push those back into the live parameters (a property change could also come
// from automation). Plugins resync themselves this way when their state is
// restored (see EqualiserPlugin::restorePluginStateFromValueTree), so we do the
// same for every plugin after an undo or redo.
static void syncParametersFromState (te::Edit& edit)
{
    for (auto* plugin : te::getAllPlugins (edit, true))
        for (auto* param : plugin->getAutomatableParameters())
            param->updateFromAttachedValue();
}

void AudioEngine::undo()
{
    impl->edit->undo();
    syncParametersFromState (*impl->edit);
}

void AudioEngine::redo()
{
    impl->edit->redo();
    syncParametersFromState (*impl->edit);
}
bool AudioEngine::canUndo() const   { return impl->edit->getUndoManager().canUndo(); }
bool AudioEngine::canRedo() const   { return impl->edit->getUndoManager().canRedo(); }

juce::String AudioEngine::getUndoDescription() const { return impl->edit->getUndoManager().getUndoDescription(); }
juce::String AudioEngine::getRedoDescription() const { return impl->edit->getUndoManager().getRedoDescription(); }

void AudioEngine::beginUndoStep (const juce::String& name)
{
    impl->userHasMadeChanges = true;
    impl->edit->getUndoManager().beginNewTransaction (name);
}

//==============================================================================
StereoLevel AudioEngine::getMasterLevel()
{
    return impl->readMasterLevel();
}

SystemStats AudioEngine::getSystemStats() const
{
    auto& dm = impl->engine.getDeviceManager();

    SystemStats s;
    s.cpuPercent = dm.getCpuUsage() * 100.0f;
    s.sampleRate = dm.getSampleRate();
    s.latencyMs = dm.getOutputLatencySeconds() * 1000.0;
    return s;
}

std::unique_ptr<juce::Component> AudioEngine::createAudioSettingsComponent()
{
    auto comp = std::make_unique<juce::AudioDeviceSelectorComponent> (impl->engine.getDeviceManager().deviceManager,
                                                                      0, 2,     // audio inputs
                                                                      1, 2,     // audio outputs
                                                                      true,     // MIDI inputs
                                                                      false,    // MIDI output
                                                                      true,     // stereo pairs
                                                                      false);   // advanced options
    comp->setSize (500, 450);
    return comp;
}

} // namespace ikaros
