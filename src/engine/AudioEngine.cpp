#include "engine/AudioEngine.h"
#include "engine/AudioPreview.h"
#include "engine/PluginLibrary.h"
#include "engine/SoundLibrary.h"
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
    bool shouldOpenAudioInputByDefault() override   { return true; }

    // We initialise the device manager ourselves (see AudioEngine::Impl) so we
    // can ask for a plain stereo setup instead of "every channel available".
    bool autoInitialiseDeviceManager() override     { return false; }

    // Recorded audio goes to Documents/Ikaros Studio/Recordings rather than
    // next to the session file: an untitled session lives in a temp folder,
    // and takes recorded there would be lost. Saving copies them into the
    // session's own folder (see AudioEngine::saveSessionAs).
    juce::String getDefaultAudioRecordingFilePattern() override
    {
        auto folder = AudioEngine::getRecordingsFolder();
        folder.createDirectory();
        return folder.getFullPathName() + "/%edit%_%track%_%date%_%time%_Take_%take%";
    }

    // Audio inputs: each open input channel on its own (microphones and
    // guitars are mono), plus each neighbouring pair as a stereo input
    // (keyboards, mixers). Tracktion allows inputs that share channels.
    // Outputs stay in stereo pairs.
    bool isDescriptionOfWaveDevicesSupported() override   { return true; }

    void describeWaveDevices (std::vector<te::WaveDeviceDescription>& descriptions,
                              juce::AudioIODevice& device, bool isInput) override
    {
        if (! isInput)
        {
            const int num = device.getOutputChannelNames().size();

            for (int i = 0; i < num; i += 2)
                descriptions.push_back (te::WaveDeviceDescription::withNumChannels ({}, (uint32_t) i, i + 1 < num ? 2u : 1u, true));

            return;
        }

        const int num = device.getInputChannelNames().size();
        auto open = device.getActiveInputChannels();

        // Not open yet: assume the first two, which is what we ask for.
        if (open.isZero())
            open.setRange (0, juce::jmin (2, num), true);

        for (int i = 0; i < num; ++i)
            if (open[i])
                descriptions.push_back (te::WaveDeviceDescription::withNumChannels ({}, (uint32_t) i, 1, true));

        for (int i = 0; i + 1 < num; i += 2)
            if (open[i] && open[i + 1])
                descriptions.push_back (te::WaveDeviceDescription::withNumChannels ({}, (uint32_t) i, 2, true));
    }
};

//==============================================================================
// Shows a third-party plug-in's own editor. Lives inside the window the UI
// made; deleted (with the window) by Tracktion when the window closes or the
// plug-in goes away, which is always before the plug-in itself.
struct PluginEditorHolder  : public juce::Component
{
    PluginEditorHolder (te::Plugin& p, std::unique_ptr<juce::Component> e)
        : plugin (p), editor (std::move (e))
    {
        addAndMakeVisible (*editor);
        setSize (editor->getWidth(), editor->getHeight());
    }

    ~PluginEditorHolder() override
    {
        editor.reset();   // the editor before saving: some plug-ins only commit changes when their editor closes
        plugin.edit.flushPluginStateIfNeeded (plugin);
    }

    // The plug-in resized its editor: follow it (the window follows us).
    void childBoundsChanged (juce::Component* child) override
    {
        if (child == editor.get())
            setSize (editor->getWidth(), editor->getHeight());
    }

    void resized() override   { editor->setBounds (getLocalBounds()); }

    te::Plugin& plugin;
    std::unique_ptr<juce::Component> editor;
};

// UIBehaviour is how Tracktion asks the app for UI. We only supply plug-in
// windows, and pass the framing on to the UI layer's factory.
struct IkarosUIBehaviour  : public te::UIBehaviour
{
    PluginWindowFactory factory;

    std::unique_ptr<juce::Component> createPluginWindow (te::PluginWindowState& state) override
    {
        auto* windowState = dynamic_cast<te::Plugin::WindowState*> (&state);

        if (windowState == nullptr || factory == nullptr)
            return {};

        auto& plugin = windowState->plugin;
        std::unique_ptr<juce::Component> editor = plugin.createEditor();

        // A plug-in without an editor of its own gets JUCE's generic one (a slider per setting).
        if (editor == nullptr)
            if (auto* external = dynamic_cast<te::ExternalPlugin*> (&plugin))
                if (auto* instance = external->getAudioPluginInstance())
                    editor = std::make_unique<juce::GenericAudioProcessorEditor> (*instance);

        if (editor == nullptr)
            return {};

        PluginWindowRequest request;
        request.content = std::make_unique<PluginEditorHolder> (plugin, std::move (editor));
        request.title = plugin.getName();

        if (auto* track = plugin.getOwnerTrack())
            request.title << " - " << track->getName();

        request.position = state.lastWindowBounds.has_value() ? state.lastWindowBounds->getPosition()
                                                              : state.choosePositionForPluginWindow();

        // WeakReference: the window state is gone if the plug-in was deleted.
        juce::WeakReference<te::PluginWindowState> weakState (&state);

        // Closing deletes the window, so do it after the click that asked for it.
        request.close = [weakState]
        {
            juce::MessageManager::callAsync ([weakState]
            {
                if (auto* s = weakState.get())
                    s->closeWindowExplicitly();
            });
        };

        request.moved = [weakState] (juce::Rectangle<int> bounds)
        {
            if (auto* s = weakState.get())
                s->lastWindowBounds = bounds;
        };

        return factory (std::move (request));
    }
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
    Impl (AudioEngine& o, const juce::String& settingsName)
        : owner (o),
          engine (settingsName, std::make_unique<IkarosUIBehaviour>(), std::make_unique<IkarosEngineBehaviour>())
    {
        // 2 inputs, 2 outputs by default. (Tracktion's default of "every
        // channel" crashed PipeWire's ALSA plugin.) The user can change devices
        // in File > Audio & MIDI Settings, and Tracktion remembers the choice.
        engine.getDeviceManager().initialise (2, 2);

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
    te::Engine engine;

    std::unique_ptr<te::Edit> edit;
    juce::File sessionFile;
    bool untitled = true;

    //==============================================================================
    std::unique_ptr<te::Edit> createEdit (const juce::ValueTree& state)
    {
        auto id = te::ProjectItemID::fromProperty (state, te::IDs::projectID);

        if (! id.isValid())
            id = te::ProjectItemID::createNewID ({});

        // Designated initialisers (C++20): name the Options fields we set; the
        // rest keep their defaults.
        return te::Edit::createEdit ({ .engine = engine,
                                       .editState = state,
                                       .editProjectItemID = id,
                                       .editFileRetriever = [this] { return sessionFile; },
                                       .filePathResolver = [this] (const juce::String& path) { return resolvePath (path); },
                                       .numAudioTracks = 0 });   // no default track: we add our own
    }

    // Audio regions refer to their files by path. For a file near the
    // session, Tracktion stores a path relative to the session *file*
    // ("../Loop.wav" for a file in the same folder), but its default
    // resolver reads it from the session's folder, one level too high. This
    // reads it back the way it was written.
    juce::File resolvePath (const juce::String& path) const
    {
        if (path.isEmpty())
            return {};

        // getChildFile handles absolute paths too: it just returns them.
        return sessionFile.getChildFile (path);
    }

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

    // Recording: the track the Record button armed for this take (disarmed
    // again when recording stops) and where the take started.
    TrackId autoArmedTrack = 0;
    double recordStartBeats = 0.0;

    //==============================================================================
    // After a session is created or opened, Tracktion finishes setting up on
    // the message loop (plugin init, device routing, its own undo-grouping
    // timer) and some of that lands in the undo history. A beginner shouldn't
    // be able to "undo" invisible setup, so once things settle we start from a
    // clean history, unless the user has already started working.
    bool userHasMadeChanges = false;
    juce::uint32 cleanSlateStart = 0, lastModelChange = 0;

    void startFromCleanSlateSoon()
    {
        userHasMadeChanges = false;
        cleanSlateStart = lastModelChange = juce::Time::getMillisecondCounter();
        startTimer (100);
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
        lastModelChange = juce::Time::getMillisecondCounter();
        owner.sendChangeMessage();
    }

    void timerCallback() override
    {
        // Wait until setup has gone quiet for 500 ms (longer than Tracktion's
        // 350 ms undo-grouping timer), but no more than 3 s in total.
        const auto now = juce::Time::getMillisecondCounter();

        if (now - lastModelChange < 500 && now - cleanSlateStart < 3000)
            return;

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
AudioEngine::AudioEngine (const juce::String& settingsName)
    : impl (std::make_unique<Impl> (*this, settingsName))
{
    sounds = std::make_unique<SoundLibrary>();
    plugins = std::make_unique<PluginLibrary> (*this);
    preview = std::make_unique<AudioPreview> (impl->engine.getDeviceManager().deviceManager,
                                              impl->engine.getAudioFileFormatManager().readFormatManager);
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
    preview.reset();
    trackController.reset();
    impl->stopObservingEdit();
    impl->edit.reset();   // closes any plug-in windows
    plugins.reset();
}

void AudioEngine::setPluginWindowFactory (PluginWindowFactory factory)
{
    // The UIBehaviour we gave the Engine is ours, so this cast always works.
    dynamic_cast<IkarosUIBehaviour&> (impl->engine.getUIBehaviour()).factory = std::move (factory);
}

bool AudioEngine::runPluginScanWorkerIfRequested (const juce::String& commandLine)
{
    return PluginLibrary::runScanWorkerIfRequested (commandLine);
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
    // Start every new session with one instrument track so a beginner can play
    // the keyboard straight away.
    loadEdit (te::createEmptyEdit (impl->engine), getUntitledSessionFile (impl->engine), true);
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

    // Read the file first, so a broken file leaves the current session alone.
    auto state = te::loadEditFromFile (impl->engine, file, te::ProjectItemID());

    if (! state.isValid())
        return false;

    loadEdit (state, file, false);

    auto tracks = trackController->getTracks();
    trackController->setSelectedTrack (tracks.empty() ? 0 : tracks.front().id);
    impl->startFromCleanSlateSoon();
    notifyModelChanged();
    return true;
}

void AudioEngine::loadEdit (const juce::ValueTree& state, const juce::File& file, bool untitled)
{
    if (impl->edit != nullptr)
    {
        impl->edit->getTransport().stop (false, true);
        trackController->inputs().allNotesOff();
    }

    impl->detachMasterMeter();
    impl->stopObservingEdit();

    // Let go of anything that references the old Edit's tracks or plugins
    // (meter taps, held notes) *before* the old Edit is destroyed.
    trackController->sessionChanged();

    // Set before creating the Edit: it finds its audio files from here while loading.
    impl->sessionFile = file;
    impl->untitled = untitled;
    impl->edit = impl->createEdit (state);   // the old Edit is destroyed here

    auto& edit = *impl->edit;

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

juce::File AudioEngine::getRecordingsFolder()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
               .getChildFile ("Ikaros Studio").getChildFile ("Recordings");
}

juce::File AudioEngine::getRecordingsFolderFor (const juce::File& sessionFile)
{
    return sessionFile.getSiblingFile (sessionFile.getFileNameWithoutExtension() + " Recordings");
}

// Copies a recording into a session's recordings folder, unless an earlier
// save already did. Returns the copy, or the original if copying failed.
static juce::File copyRecordingInto (const juce::File& recording, const juce::File& folder)
{
    auto target = folder.getChildFile (recording.getFileName());

    if (target.existsAsFile() && target.getSize() == recording.getSize())
        return target;

    if (target.exists())
        target = folder.getNonexistentChildFile (recording.getFileNameWithoutExtension(),
                                                 recording.getFileExtension(), false);

    return folder.createDirectory() && recording.copyFileTo (target) ? target : recording;
}

bool AudioEngine::saveSessionAs (const juce::File& requestedFile)
{
    auto file = requestedFile.withFileExtension (sessionFileExtension);
    auto& edit = *impl->edit;

    // Audio regions refer to their files by a path that Tracktion stores
    // relative to the session file when it can. Find every region's file
    // while those paths still resolve from the old location.
    struct Source { te::Clip::Ptr clip; juce::File file; juce::var oldPath; };
    std::vector<Source> sources;

    for (auto* track : te::getAudioTracks (edit))
        for (auto* clip : track->getClips())
            if (dynamic_cast<te::WaveAudioClip*> (clip) != nullptr)
                sources.push_back ({ clip, clip->getSourceFileReference().getFile(), clip->state[te::IDs::source] });

    const auto oldFile = impl->sessionFile;
    const auto oldRecordings = impl->untitled ? juce::File() : getRecordingsFolderFor (oldFile);
    const auto newRecordings = getRecordingsFolderFor (file);
    impl->sessionFile = file;   // paths are now worked out from the new location

    for (auto& source : sources)
    {
        auto target = source.file;

        // The session's recordings (new takes, or ones in the folder of the
        // session it was saved from) are copied next to the new session file.
        // Imported audio files stay where they are.
        const bool isRecording = source.file.isAChildOf (getRecordingsFolder())
                                  || (oldRecordings != juce::File() && source.file.isAChildOf (oldRecordings));

        if (isRecording && source.file.existsAsFile() && ! source.file.isAChildOf (newRecordings))
            target = copyRecordingInto (source.file, newRecordings);

        // Re-store every path relative to the new location. nullptr = not an
        // undo step: the user shouldn't be able to "undo" a save.
        source.clip->state.setProperty (te::IDs::source,
                                        te::SourceFileReference::findPathFromFile (edit, target, true), nullptr);
    }

    // writeToFile is synchronous, which keeps the menu code simple.
    if (! te::EditFileOperations (edit).writeToFile (file, false))
    {
        // Point everything back at the files it used before.
        impl->sessionFile = oldFile;

        for (auto& source : sources)
            source.clip->state.setProperty (te::IDs::source, source.oldPath, nullptr);

        return false;
    }

    impl->untitled = false;
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
    auto& transport = impl->edit->getTransport();
    const bool wasRecording = transport.isRecording();

    if (wasRecording)
        beginUndoStep ("Record");   // the new region is added as one undo step

    transport.stop (false, false);   // false = keep the recording

    if (wasRecording && impl->autoArmedTrack != 0)
        trackController->inputs().setArmed (std::exchange (impl->autoArmedTrack, TrackId {}), false);
}

void AudioEngine::toggleRecord()
{
    auto& transport = impl->edit->getTransport();

    if (transport.isRecording())
    {
        stop();
        return;
    }

    impl->autoArmedTrack = trackController->inputs().armForRecording();

    if (trackController->inputs().getArmedTrack() == 0)
        return;   // no track to record onto

    // With the count-in on, Tracktion starts playback a bar early and clicks
    // through that bar (even with the metronome off); the take itself starts here.
    impl->recordStartBeats = impl->edit->tempoSequence.toBeats (transport.getPosition()).inBeats();
    transport.record (false);
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

void AudioEngine::setCountInEnabled (bool enabled)
{
    impl->edit->setCountInMode (enabled ? te::Edit::CountIn::oneBar : te::Edit::CountIn::none);
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

void AudioEngine::setKey (int root, bool minor)
{
    if (auto* pitch = impl->edit->pitchSequence.getPitch (0))
    {
        beginUndoStep ("Key");
        pitch->setPitch (60 + juce::jlimit (0, 11, root));   // stored as a MIDI note: the root above middle C
        pitch->setScaleID (minor ? te::Scale::minor : te::Scale::major);
    }
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
    s.recording = transport.isRecording();
    s.recordStartBeats = impl->recordStartBeats;
    s.looping = transport.looping.get();
    s.metronome = edit.clickTrackEnabled.get();
    s.countIn = edit.getCountInMode() != te::Edit::CountIn::none;
    s.positionSeconds = pos.inSeconds();
    s.positionBeats = ts.toBeats (pos).inBeats();
    s.countingIn = s.recording && s.positionBeats < s.recordStartBeats;

    auto barsBeats = ts.toBarsAndBeats (pos);
    s.bar = barsBeats.bars + 1;
    s.beat = barsBeats.getWholeBeats() + 1;

    s.bpm = ts.getTempo (0)->getBpm();
    s.timeSigNumerator = ts.getTimeSig (0)->numerator.get();
    s.timeSigDenominator = ts.getTimeSig (0)->denominator.get();

    if (auto* pitch = edit.pitchSequence.getPitch (0))
    {
        s.keyRoot = pitch->getPitch() % 12;
        s.keyMinor = pitch->getScale() == te::Scale::minor || pitch->getScale() == te::Scale::aeolian;
    }

    auto loop = transport.getLoopRange();
    s.loopStartBeats = ts.toBeats (loop.getStart()).inBeats();
    s.loopEndBeats = ts.toBeats (loop.getEnd()).inBeats();
    return s;
}

juce::String AudioEngine::getKeyName() const
{
    auto state = getTransportState();
    return ikaros::getKeyName (state.keyRoot, state.keyMinor);
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
