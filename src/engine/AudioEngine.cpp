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

    // A saved session records into its own Audio folder. An untitled one
    // lives in a temp folder, where takes would be lost, so its takes go to
    // Documents/Ikaros Studio/Recordings until it's saved (saving copies them
    // into the session, see AudioEngine::saveSessionAs).
    std::function<juce::File()> getRecordingFolder;   // set by AudioEngine::Impl

    juce::String getDefaultAudioRecordingFilePattern() override
    {
        auto folder = getRecordingFolder != nullptr ? getRecordingFolder() : AudioEngine::getRecordingsFolder();
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

        // The Engine owns the behaviour we gave it, so this cast always works.
        dynamic_cast<IkarosEngineBehaviour&> (engine.getEngineBehaviour()).getRecordingFolder = [this]
        {
            return untitled ? AudioEngine::getRecordingsFolder() : AudioEngine::getAudioFolderFor (sessionFile);
        };

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

    // Audio regions refer to their files by path, relative to the session's
    // folder for files near it (see saveSessionAs). Sessions saved by earlier
    // versions can also hold paths relative to the session *file* (one ".."
    // more), which is what Tracktion writes when the file doesn't exist yet;
    // if the folder reading finds nothing, try that.
    juce::File resolvePath (const juce::String& path) const
    {
        if (path.isEmpty())
            return {};

        // getChildFile handles absolute paths too: it just returns them.
        auto fromFolder = sessionFile.getParentDirectory().getChildFile (path);

        if (! fromFolder.exists())
        {
            auto fromFile = sessionFile.getChildFile (path);

            if (fromFile.exists())
                return fromFile;
        }

        return fromFolder;
    }

    // The recently opened sessions, kept in the app's settings file.
    juce::RecentlyOpenedFilesList getRecentSessionsList()
    {
        juce::RecentlyOpenedFilesList list;
        list.setMaxNumberOfItems (24);
        list.restoreFromString (engine.getPropertyStorage().getPropertiesFile().getValue ("recentSessions"));
        return list;
    }

    void addToRecentSessions (const juce::File& file)
    {
        auto list = getRecentSessionsList();
        list.addFile (file);   // moves it to the top if it's already there
        auto& props = engine.getPropertyStorage().getPropertiesFile();
        props.setValue ("recentSessions", list.toString());
        props.saveIfNeeded();
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
    // ValueTree::Listener callbacks can fire many times for a single user action
    // (and occasionally off the message thread). Each one adds what it touched
    // to `pending`, and AsyncUpdater coalesces them into one callback on the
    // message thread, which hands the whole lot to the listeners.
    void valueTreePropertyChanged (juce::ValueTree& v, const juce::Identifier&) override
    {
        addTreeChange (classifyChange (v, {}));
    }

    void valueTreeChildAdded (juce::ValueTree& parent, juce::ValueTree& child) override
    {
        addTreeChange (classifyChange (parent, child));
    }

    void valueTreeChildRemoved (juce::ValueTree& parent, juce::ValueTree& child, int) override
    {
        addTreeChange (classifyChange (parent, child));
    }

    void valueTreeChildOrderChanged (juce::ValueTree& parent, int, int) override
    {
        addTreeChange (classifyChange (parent, {}));
    }

    /** Works out what kind of thing a change touched from where it happened
        in the session's ValueTree. `tree` is the tree whose property changed,
        or the parent a child was added to or removed from (`child`).

        The Edit's tree looks roughly like this:

            EDIT
              TEMPOSEQUENCE, PITCHSEQUENCE   tempo, time signature, key
              TRANSPORT                      playhead, loop range
              MASTERVOLUME, MASTERPLUGINS    the master channel
              INPUTDEVICES                   which inputs record onto which track
              TRACK                          one per track (also folder/marker tracks)
                MIDICLIP / AUDIOCLIP         a region
                  SEQUENCE / NOTE            a MIDI region's notes
                PLUGIN                       instrument, effects, volume/pan, sends
                  (parameters, automation)
    */
    static ModelChanges classifyChange (const juce::ValueTree& tree, const juce::ValueTree& child)
    {
        ModelChanges c;

        // Walk up from the changed tree, remembering the interesting
        // ancestors on the way. (A clip's notes sit inside its SEQUENCE, so
        // e.g. a NOTE property change passes SEQUENCE, then the clip.)
        juce::ValueTree clip, plugin;
        bool inSequence = false;

        for (auto t = tree; t.isValid(); t = t.getParent())
        {
            // The transport writes its position here many times a second
            // while playing. Nothing to re-read: the UI polls it.
            if (t.hasType (te::IDs::TRANSPORT))
                return c;

            if (t.hasType (te::IDs::TEMPOSEQUENCE) || t.hasType (te::IDs::PITCHSEQUENCE))
            {
                c.transport = true;
                return c;
            }

            if (t.hasType (te::IDs::MASTERVOLUME) || t.hasType (te::IDs::MASTERPLUGINS))
            {
                c.mixer = true;
                return c;
            }

            if (t.hasType (te::IDs::INPUTDEVICES))
            {
                c.tracks = true;
                return c;
            }

            if (t.hasType (te::IDs::SEQUENCE))
                inSequence = true;

            if (! clip.isValid() && te::Clip::isClipState (t))
                clip = t;

            if (! plugin.isValid() && t.hasType (te::IDs::PLUGIN))
                plugin = t;

            if (te::TrackList::isTrack (t))
                break;

            // Tracktion stamps the time of the last change on the Edit itself
            // after every edit; there's nothing to re-read for that.
            if (t.hasType (te::IDs::EDIT))
            {
                if (te::TrackList::isTrack (child))
                    c.tracks = c.regions = true;   // a track added or removed (with its regions)
                else if (child.isValid())
                    c.session = true;              // something we don't know: play safe

                return c;
            }
        }

        if (clip.isValid())
        {
            // Something inside one region: its notes, or its own settings.
            const auto id = te::EditItemID::fromID (clip).getRawID();
            c.changedRegions.insert (id);
            c.notes = inSequence;
            return c;
        }

        if (plugin.isValid())
        {
            // Inside a plug-in: a knob turned (volume, pan, a send, an
            // effect's setting) or its automation. (Adding or removing a
            // plug-in happens on the track, below.)
            c.mixer = true;
            return c;
        }

        // On a track itself: a region or a plug-in added/removed, or one of
        // the track's own settings (name, colour, sound, mute...).
        if (te::Clip::isClipState (child))
            c.regions = true;
        else
            c.tracks = true;

        return c;
    }

    // Changes that touch nothing the UI shows (the playhead, Tracktion's own
    // "last changed" time stamp) aren't worth waking the listeners for.
    void addTreeChange (const ModelChanges& c)
    {
        if (c.any())
            addPending (c);
    }

    // `pending` can be written off the message thread (see above), so a lock
    // guards it. ScopedLock holds the lock until the end of the block.
    juce::CriticalSection pendingLock;
    ModelChanges pending;

public:
    /** Adds to the changes the listeners will hear about, and schedules the call. */
    void addPending (const ModelChanges& c)
    {
        {
            const juce::ScopedLock sl (pendingLock);
            pending.merge (c);
        }

        triggerAsyncUpdate();
    }

private:
    juce::ValueTree observedState;

    void handleAsyncUpdate() override
    {
        ModelChanges changes;

        {
            const juce::ScopedLock sl (pendingLock);
            std::swap (changes, pending);
        }

        lastModelChange = juce::Time::getMillisecondCounter();

        // Spelled out in full: inside Impl, plain "Listener" would mean the
        // juce::ValueTree::Listener we inherit from.
        owner.listeners.call ([&changes] (AudioEngine::Listener& l) { l.sessionChanged (changes); });
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
            addPending ({});   // nothing to re-read, but Undo is no longer available
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

void AudioEngine::newSession (const NewSessionSettings& settings)
{
    loadEdit (te::createEmptyEdit (impl->engine), getUntitledSessionFile (impl->engine), true);

    setBpm (settings.bpm);
    setTimeSignature (settings.numerator, settings.denominator);
    setKey (settings.keyRoot, settings.minor);

    // Instrument tracks take the sound's name ("Soft Piano"). If a sound pack
    // has been removed, the track keeps the built-in synth.
    auto& tc = *trackController;
    const juce::String pack ("sampler:Ikaros Basics/");

    auto instrument = [&tc] (const juce::String& soundId)
    {
        auto id = tc.addInstrumentTrack();
        tc.effects().setSound (id, soundId, false);
        return id;
    };

    auto audio = [&tc] (const juce::String& name)
    {
        auto id = tc.addAudioTrack();
        tc.renameTrack (id, name);
        return id;
    };

    // Every template has at least one instrument track, so a beginner can
    // play the keyboard straight away.
    switch (settings.kind)
    {
        case SessionTemplate::empty:
            tc.addInstrumentTrack();
            break;

        case SessionTemplate::keyboards:
            instrument (pack + "Soft Piano");
            instrument (pack + "Electric Piano");
            instrument ("synth:Vintage Organ");
            break;

        case SessionTemplate::voice:
            audio ("Voice");
            instrument (pack + "Soft Piano");
            break;

        case SessionTemplate::beats:
            instrument (pack + "Electronic Kit");
            instrument ("synth:Synth Bass");
            instrument ("synth:Warm Pad");
            break;

        case SessionTemplate::songwriter:
            instrument (pack + "Lo-fi Kit");
            instrument (pack + "Picked Bass");
            instrument (pack + "Nylon Guitar");
            instrument (pack + "Soft Piano");
            audio ("Voice");
            break;
    }

    auto tracks = tc.getTracks();
    tc.setSelectedTrack (tracks.empty() ? 0 : tracks.front().id);

    // Building the template isn't something to undo.
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
    impl->addToRecentSessions (file);

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

juce::File AudioEngine::getSessionFileFor (const juce::File& chosenFile)
{
    const auto name = chosenFile.getFileNameWithoutExtension();
    const auto file = chosenFile.withFileExtension (sessionFileExtension);

    if (file.getParentDirectory().getFileName() == name)
        return file;

    return chosenFile.getParentDirectory().getChildFile (name).getChildFile (name + sessionFileExtension);
}

juce::File AudioEngine::getAudioFolderFor (const juce::File& sessionFile)
{
    return sessionFile.getSiblingFile ("Audio");
}

juce::File AudioEngine::getPreviewFileFor (const juce::File& sessionFile)
{
    return sessionFile.getSiblingFile ("Preview.png");
}

std::vector<RecentSession> AudioEngine::getRecentSessions() const
{
    std::vector<RecentSession> result;
    auto list = impl->getRecentSessionsList();

    for (int i = 0; i < list.getNumFiles(); ++i)
    {
        auto file = list.getFile (i);

        if (file.existsAsFile())
            result.push_back ({ file, file.getFileNameWithoutExtension(), file.getLastModificationTime(),
                                getPreviewFileFor (file) });
    }

    return result;
}

// Copies an audio file into a session's Audio folder, unless an earlier
// save already did (a file of the same name and content is there). Returns
// the copy, or the original if copying failed.
static juce::File copyIntoFolder (const juce::File& source, const juce::File& folder)
{
    auto target = folder.getChildFile (source.getFileName());

    if (target.existsAsFile() && target.hasIdenticalContentTo (source))
        return target;

    if (target.exists())
        target = folder.getNonexistentChildFile (source.getFileNameWithoutExtension(),
                                                 source.getFileExtension(), false);

    return folder.createDirectory() && source.copyFileTo (target) ? target : source;
}

bool AudioEngine::saveSessionAs (const juce::File& requestedFile)
{
    const auto file = getSessionFileFor (requestedFile);
    auto& edit = *impl->edit;

    if (! file.getParentDirectory().createDirectory())
        return false;

    // Audio regions refer to their files by path. Find every region's file
    // while those paths still resolve from the old location.
    struct Source { te::Clip::Ptr clip; juce::File file; juce::var oldPath; };
    std::vector<Source> sources;

    for (auto* track : te::getAudioTracks (edit))
        for (auto* clip : track->getClips())
            if (dynamic_cast<te::WaveAudioClip*> (clip) != nullptr)
                sources.push_back ({ clip, clip->getSourceFileReference().getFile(), clip->state[te::IDs::source] });

    const auto oldFile = impl->sessionFile;
    const auto audioFolder = getAudioFolderFor (file);
    impl->sessionFile = file;   // paths are now worked out from the new location

    // Each file is copied once, however many regions use it.
    std::map<juce::String, juce::File> copies;

    for (auto& source : sources)
    {
        auto target = source.file;

        // Everything the session plays lives in its Audio folder, so the
        // session folder can be moved, backed up or shared on its own.
        if (source.file.existsAsFile() && ! source.file.isAChildOf (audioFolder))
        {
            auto& copy = copies[source.file.getFullPathName()];

            if (copy == juce::File())
                copy = copyIntoFolder (source.file, audioFolder);

            target = copy;
        }

        // Re-store every path relative to the new location. Tracktion decides
        // whether a relative path makes sense, but works it out from the
        // session file's folder only if the file already exists (otherwise
        // from the file itself, one ".." more). Always use the folder, so a
        // path reads back the same after any number of saves.
        // nullptr = not an undo step: the user shouldn't be able to "undo" a save.
        auto path = te::SourceFileReference::findPathFromFile (edit, target, true);

        if (! juce::File::isAbsolutePath (path))
            path = target.getRelativePathFrom (file.getParentDirectory());

        source.clip->state.setProperty (te::IDs::source, path, nullptr);
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
    impl->addToRecentSessions (file);
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

void AudioEngine::notifyModelChanged (const ModelChanges& changes)
{
    impl->addPending (changes);
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
