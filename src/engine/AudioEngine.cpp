#include "engine/AudioEngine.h"
#include "engine/AudioPreview.h"
#include "engine/PluginLibrary.h"
#include "engine/SfzPlugin.h"
#include "engine/SoundShaper.h"
#include "engine/SoundLibrary.h"
#include "engine/TrackController.h"
#include "engine/TrackHelpers.h"
#include "platform/AppFolders.h"

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

    // Takes are recorded straight into the session's working copy (its
    // Audio folder), whether or not it has been saved yet.
    std::function<juce::File()> getRecordingFolder;   // set by AudioEngine::Impl

    juce::String getDefaultAudioRecordingFilePattern() override
    {
        auto folder = getRecordingFolder();
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

    // Tracktion hands long jobs (like rendering offline, see
    // AudioEngine::renderPeak) to the UI to run "with a progress bar". There's
    // no progress bar yet: just run the job here until it's finished.
    void runTaskWithProgressBar (te::ThreadPoolJobWithProgress& task) override
    {
        while (task.runJob() == juce::ThreadPoolJob::jobNeedsRunningAgain)
        {
        }
    }

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

        // Our own instrument for SFZ sounds, next to Tracktion's built-in
        // ones, so sessions can create it (and load it back) by type name.
        engine.getPluginManager().createBuiltInType<SfzPlugin>();

        // The hidden plugins behind sampled instruments' Sound tab knobs.
        engine.getPluginManager().createBuiltInType<SoundShaperPlugin>();
        engine.getPluginManager().createBuiltInType<TransposePlugin>();

        // The Engine owns the behaviour we gave it, so this cast always works.
        dynamic_cast<IkarosEngineBehaviour&> (engine.getEngineBehaviour()).getRecordingFolder = [this]
        {
            return getAudioFolder();
        };

        deleteStaleWorkFolders();

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

        // The working copy goes with the session. (The user has already been
        // asked about unsaved changes; after a crash it would stay behind.)
        edit = nullptr;
        deleteWorkFolder (workFolder);
    }

    AudioEngine& owner;

    // te::Engine is the root object: device manager, plugin manager, temp files...
    // There should only be one per app.
    te::Engine engine;

    std::unique_ptr<te::Edit> edit;

    // The session being edited: its working copy (an unpacked bundle, see
    // AudioEngine.h), the session file inside it that Tracktion reads and
    // writes, and the bundle it's saved as (empty while untitled).
    juce::File workFolder;
    juce::File sessionFile;               // workFolder/Session.tracktionedit
    juce::File bundleFile;
    bool untitled = true;

    // A session from an earlier version: its audio is still in its old
    // folder until the first save copies it in. Empty otherwise.
    juce::File legacySessionFile;

    juce::File getAudioFolder() const     { return workFolder.getChildFile (SessionBundle::audioFolder); }

    //==============================================================================
    // Working copies live in the local data folder, one folder each.
    static juce::File getWorkRoot()
    {
        return platform::getLocalDataFolder().getChildFile ("Open Sessions");
    }

    static juce::File createWorkFolder()
    {
        // A unique name (a UUID), so two sessions (or two copies of the app)
        // never share one.
        auto folder = getWorkRoot().getChildFile (juce::Uuid().toString());
        folder.getChildFile (SessionBundle::audioFolder).createDirectory();
        return folder;
    }

    static void deleteWorkFolder (const juce::File& folder)
    {
        // Only ever delete inside the working-copy root, whatever we're given.
        if (folder != juce::File() && folder.isAChildOf (getWorkRoot()))
            folder.deleteRecursively();
    }

    // Working copies left behind by a crash. Nothing offers to recover them
    // yet, so after two weeks untouched they're tidied away.
    static void deleteStaleWorkFolders()
    {
        const auto cutoff = juce::Time::getCurrentTime() - juce::RelativeTime::days (14);

        for (auto& folder : getWorkRoot().findChildFiles (juce::File::findDirectories, false))
        {
            auto session = folder.getChildFile (SessionBundle::sessionEntry);
            auto lastUsed = (session.existsAsFile() ? session : folder).getLastModificationTime();

            if (lastUsed < cutoff)
                deleteWorkFolder (folder);
        }
    }

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

    // Audio regions refer to their files by path, relative to the session
    // ("Audio/Take 1.wav"). The working copy is tried first. A session from an
    // earlier version still has its audio in its old folder; there, paths can
    // also be relative to the session *file* (one ".." more), which is what
    // Tracktion wrote when the file didn't exist yet.
    juce::File resolvePath (const juce::String& path) const
    {
        if (path.isEmpty())
            return {};

        // getChildFile handles absolute paths too: it just returns them.
        auto inWorkFolder = workFolder.getChildFile (path);

        if (inWorkFolder.exists() || legacySessionFile == juce::File())
            return inWorkFolder;

        for (auto candidate : { legacySessionFile.getParentDirectory().getChildFile (path),
                                legacySessionFile.getChildFile (path) })
            if (candidate.exists())
                return candidate;

        return inWorkFolder;
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
void AudioEngine::newSession (const NewSessionSettings& settings)
{
    // Even an untitled session has a working copy: recordings go there.
    loadEdit (te::createEmptyEdit (impl->engine), Impl::createWorkFolder(), {}, {});

    setBpm (settings.bpm);
    setTimeSignature (settings.numerator, settings.denominator);
    setKey (settings.keyRoot, settings.minor);

    // Instrument tracks take the sound's name ("Soft Piano"). If a sound pack
    // has been removed, the track keeps the built-in synth.
    auto& tc = *trackController;
    const juce::String pack ("sampler:ikaros-studio.basics/");

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

// The last part of a path saved on any system ("C:\\Samples\\C3.flac" or
// "/home/me/Samples/C3.flac" -> "C3.flac").
static juce::String fileNameOf (const juce::String& path)
{
    return path.fromLastOccurrenceOf ("/", false, false).fromLastOccurrenceOf ("\\", false, false);
}

// A session saves where each sampled instrument's files were on the computer
// that saved it. Before the session is built, point every instrument at the
// files of the same instrument here, found by its pack id (or, for sessions
// from before pack ids, its pack folder's name). Instruments whose pack isn't
// installed are left as they were, so saving keeps the reference.
static void pointInstrumentsAtLocalFiles (juce::ValueTree tree, const SoundLibrary& sounds)
{
    using namespace detail;

    for (auto child : tree)
    {
        if (! te::TrackList::isTrack (child))
            continue;

        pointInstrumentsAtLocalFiles (child, sounds);   // tracks inside folder tracks

        auto* instrument = sounds.resolveSampledInstrument (child[soundIdProp].toString());

        if (instrument == nullptr)
            continue;

        child.setProperty (soundIdProp, instrument->info.id, nullptr);   // saved the new way from now on

        for (auto plugin : child)
        {
            if (! plugin.hasType (te::IDs::PLUGIN))
                continue;

            const auto type = plugin[te::IDs::type].toString();

            if (type == te::SamplerPlugin::xmlTypeName)
            {
                // Each of the sampler's sounds, matched to a key zone by file name.
                for (auto sound : plugin)
                    for (auto& zone : instrument->zones)
                        if (sound.hasType (te::IDs::SOUND) && zone.file.getFileName() == fileNameOf (sound[te::IDs::source].toString()))
                            sound.setProperty (te::IDs::source, zone.file.getFullPathName(), nullptr);
            }
            else if (type == SfzPlugin::xmlTypeName && instrument->sfzFile.existsAsFile())
            {
                plugin.setProperty (SfzPlugin::sfzFileProp, instrument->sfzFile.getFullPathName(), nullptr);
            }
        }
    }
}

bool AudioEngine::openSession (const juce::File& file)
{
    if (! file.existsAsFile())
        return false;

    // Unpack the bundle (or, for a session from an earlier version, copy its
    // session file) into a new working copy and read it, before anything
    // changes: a broken file leaves the current session alone.
    const bool isBundle = SessionBundle::isBundleFile (file);
    auto work = Impl::createWorkFolder();
    auto workSession = work.getChildFile (SessionBundle::sessionEntry);

    const bool unpacked = isBundle ? SessionBundle::unpack (file, work).wasOk()
                                   : file.copyFileTo (workSession);

    auto state = unpacked ? te::loadEditFromFile (impl->engine, workSession, te::ProjectItemID()) : juce::ValueTree();

    if (! state.isValid())
    {
        Impl::deleteWorkFolder (work);
        return false;
    }

    pointInstrumentsAtLocalFiles (state, *sounds);

    if (isBundle)
    {
        loadEdit (state, work, file, {});
    }
    else
    {
        // An older session ("My Song/My Song.tracktionedit") is saved as a
        // bundle next to its folder ("My Song.iksession"), unless one is
        // already there: then Save asks where.
        auto folder = file.getParentDirectory();
        auto bundle = folder.getFileName() == file.getFileNameWithoutExtension()
                        ? folder.getSiblingFile (file.getFileNameWithoutExtension() + sessionFileExtension)
                        : file.withFileExtension (sessionFileExtension);

        loadEdit (state, work, bundle.exists() ? juce::File() : bundle, file);
    }

    impl->addToRecentSessions (file);

    auto tracks = trackController->getTracks();
    trackController->setSelectedTrack (tracks.empty() ? 0 : tracks.front().id);
    impl->startFromCleanSlateSoon();
    notifyModelChanged();
    return true;
}

void AudioEngine::loadEdit (const juce::ValueTree& state, const juce::File& workFolder,
                            const juce::File& bundle, const juce::File& legacySessionFile)
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
    const auto oldWorkFolder = impl->workFolder;
    impl->workFolder = workFolder;
    impl->sessionFile = workFolder.getChildFile (SessionBundle::sessionEntry);
    impl->bundleFile = bundle;
    impl->legacySessionFile = legacySessionFile;
    impl->untitled = bundle == juce::File();
    impl->edit = impl->createEdit (state);   // the old Edit is destroyed here

    // ...so the old session's working copy isn't needed any more.
    if (oldWorkFolder != workFolder)
        Impl::deleteWorkFolder (oldWorkFolder);

    auto& edit = *impl->edit;

    // Keep the audio graph running while stopped so live MIDI (keyboard) is audible.
    edit.playInStopEnabled = true;

    // Allocates the playback graph now rather than on first play.
    edit.getTransport().ensureContextAllocated();

    impl->observeEdit();
}

bool AudioEngine::saveSession()
{
    return ! impl->untitled && saveSessionAs (impl->bundleFile);
}

bool AudioEngine::saveSessionAs (const juce::File& file)
{
    auto job = beginSave (file);

    if (job == nullptr)
        return false;

    job->run();
    return finishSave (*job);
}

juce::File AudioEngine::getSessionFileFor (const juce::File& chosenFile)
{
    if (chosenFile.hasFileExtension (sessionFileExtension))
        return chosenFile;

    // "My Song" or "My Song.tracktionedit" -> "My Song.iksession". (Not
    // withFileExtension on its own: "Take 2.1" would lose its ".1".)
    if (chosenFile.hasFileExtension (legacySessionFileExtension))
        return chosenFile.withFileExtension (sessionFileExtension);

    return chosenFile.getSiblingFile (chosenFile.getFileName() + sessionFileExtension);
}

juce::File AudioEngine::getPreviewFile() const
{
    return impl->workFolder.getChildFile (SessionBundle::previewEntry);
}

std::vector<RecentSession> AudioEngine::getRecentSessions() const
{
    std::vector<RecentSession> result;
    auto list = impl->getRecentSessionsList();

    // A bundle's preview is inside it; it's copied out once (and again only
    // when the bundle changes), so the session chooser can show it as a file.
    auto previews = platform::getLocalDataFolder().getChildFile ("Previews");

    for (int i = 0; i < list.getNumFiles(); ++i)
    {
        auto file = list.getFile (i);

        if (! file.existsAsFile())
            continue;

        juce::File preview;

        if (SessionBundle::isBundleFile (file))
        {
            preview = previews.getChildFile (juce::String::toHexString (file.getFullPathName().hashCode64()) + ".png");

            if (! preview.existsAsFile() || preview.getLastModificationTime() < file.getLastModificationTime())
            {
                auto data = SessionBundle::readEntry (file, SessionBundle::previewEntry);

                if (data.getSize() > 0 && previews.createDirectory())
                    preview.replaceWithData (data.getData(), data.getSize());
            }
        }
        else
        {
            preview = file.getSiblingFile (SessionBundle::previewEntry);   // an older session's folder
        }

        result.push_back ({ file, file.getFileNameWithoutExtension(), file.getLastModificationTime(),
                            preview.existsAsFile() ? preview : juce::File() });
    }

    return result;
}

// Copies an audio file into the session's Audio folder under a name every
// system accepts, unless an earlier save already did (a file of that name
// and the same content is there). Returns the copy, or the original if
// copying failed.
static juce::File copyIntoFolder (const juce::File& source, const juce::File& folder)
{
    const auto name = SessionBundle::makePortableFileName (source.getFileName());
    auto target = folder.getChildFile (name);

    if (target.existsAsFile() && target.hasIdenticalContentTo (source))
        return target;

    if (target.exists())
        target = folder.getNonexistentChildFile (juce::File (name).getFileNameWithoutExtension(),
                                                 source.getFileExtension(), false);

    return folder.createDirectory() && source.copyFileTo (target) ? target : source;
}

std::unique_ptr<AudioEngine::SaveJob> AudioEngine::beginSave (const juce::File& requestedFile)
{
    const auto bundle = getSessionFileFor (requestedFile);
    auto& edit = *impl->edit;
    const auto workFolder = impl->workFolder;
    const auto audioFolder = impl->getAudioFolder();

    if (! bundle.getParentDirectory().createDirectory() || ! audioFolder.createDirectory())
        return nullptr;

    // Audio regions refer to their files by path. Find every region's file
    // (while the paths still resolve as they did).
    struct Source { te::Clip::Ptr clip; juce::File file; juce::var oldPath; };
    std::vector<Source> sources;

    for (auto* track : te::getAudioTracks (edit))
        for (auto* clip : track->getClips())
            if (dynamic_cast<te::WaveAudioClip*> (clip) != nullptr)
                sources.push_back ({ clip, clip->getSourceFileReference().getFile(), clip->state[te::IDs::source] });

    // Every file the session plays goes in its Audio folder, under a name
    // every system accepts. Each file is copied (or renamed) once, however
    // many regions use it.
    std::map<juce::String, juce::File> moved;
    std::vector<juce::File> audioFiles;

    for (auto& source : sources)
    {
        auto& target = moved[source.file.getFullPathName()];

        if (target == juce::File())
        {
            target = source.file;

            if (source.file.existsAsFile() && ! source.file.isAChildOf (audioFolder))
            {
                target = copyIntoFolder (source.file, audioFolder);
            }
            else if (source.file.isAChildOf (audioFolder))
            {
                // A take named after a track called "Verse: 1", say.
                const auto portable = SessionBundle::makePortableFileName (source.file.getFileName());

                if (portable != source.file.getFileName())
                {
                    auto renamed = audioFolder.getNonexistentChildFile (juce::File (portable).getFileNameWithoutExtension(),
                                                                        source.file.getFileExtension(), false);

                    if (source.file.moveFileTo (renamed))
                        target = renamed;
                }
            }

            if (target.isAChildOf (audioFolder))
                audioFiles.push_back (target);
        }

        // Paths inside the session are relative, with "/" on every system,
        // so the session reads back the same wherever it's opened.
        // nullptr = not an undo step: the user shouldn't be able to "undo" a save.
        const auto path = target.isAChildOf (workFolder) ? target.getRelativePathFrom (workFolder).replaceCharacter ('\\', '/')
                                                          : target.getFullPathName();
        source.clip->state.setProperty (te::IDs::source, path, nullptr);
    }

    if (! te::EditFileOperations (edit).writeToFile (impl->sessionFile, false))
    {
        // Point everything back at the files it used before. (Copies made in
        // the Audio folder do no harm.)
        for (auto& source : sources)
            source.clip->state.setProperty (te::IDs::source, source.oldPath, nullptr);

        return nullptr;
    }

    auto job = std::make_unique<SaveJob>();
    job->bundle = bundle;
    job->entries.push_back ({ SessionBundle::sessionEntry, impl->sessionFile, true });

    if (auto preview = getPreviewFile(); preview.existsAsFile())
        job->entries.push_back ({ SessionBundle::previewEntry, preview, false });

    for (auto& f : audioFiles)
        job->entries.push_back ({ juce::String (SessionBundle::audioFolder) + "/" + f.getFileName(), f, false });

    // bundle.json: what saved it, and the sound packs it needs, so a computer
    // without them can say which ones (and later offer to download them).
    juce::Array<juce::var> packsUsed;
    juce::StringArray packIds;

    for (auto* track : te::getAudioTracks (edit))
    {
        const auto packId = SoundLibrary::getPackIdOf (track->state[detail::soundIdProp].toString());

        if (packId.isNotEmpty() && ! packIds.contains (packId))
        {
            packIds.add (packId);
            auto pack = sounds->findPack (packId);
            auto entry = new juce::DynamicObject();
            entry->setProperty ("id", packId);
            entry->setProperty ("version", pack.has_value() ? pack->version : juce::String());
            packsUsed.add (juce::var (entry));
        }
    }

    auto info = new juce::DynamicObject();
    info->setProperty ("format", 1);
    info->setProperty ("app", "Ikaros Studio");
    info->setProperty ("appVersion", IKAROS_VERSION);
    info->setProperty ("saved", juce::Time::getCurrentTime().toISO8601 (true));
    info->setProperty ("packs", packsUsed);
    job->info = juce::var (info);

    return job;
}

bool AudioEngine::finishSave (SaveJob& job)
{
    if (job.result.failed())
        return false;

    impl->bundleFile = job.bundle;
    impl->untitled = false;
    impl->legacySessionFile = juce::File();   // its audio has all been copied in now
    impl->addToRecentSessions (job.bundle);
    impl->edit->resetChangedStatus();
    notifyModelChanged();
    return true;
}

float AudioEngine::renderPeak (double startBeat, double endBeat)
{
    auto& edit = *impl->edit;
    const te::TimeRange range { edit.tempoSequence.toTime (te::BeatPosition::fromBeats (startBeat)),
                                edit.tempoSequence.toTime (te::BeatPosition::fromBeats (endBeat)) };

    // Every track (the renderer takes them as a set of track indexes).
    juce::BigInteger tracks;
    tracks.setRange (0, te::getAllTracks (edit).size(), true);

    return te::Renderer::measureStatistics ("Peak", edit, range, tracks, 512).peak;
}

bool AudioEngine::isUntitled() const                { return impl->untitled; }
juce::File AudioEngine::getSessionFile() const      { return impl->bundleFile; }

juce::String AudioEngine::getSessionName() const
{
    if (! impl->untitled)
        return impl->bundleFile.getFileNameWithoutExtension();

    // An older session that can't be saved next to its folder yet.
    if (impl->legacySessionFile != juce::File())
        return impl->legacySessionFile.getFileNameWithoutExtension();

    return "Untitled Session";
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
