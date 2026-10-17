#include "engine/AudioEngine.h"
#include "engine/AudioDeviceWatcher.h"
#include "engine/AudioPreview.h"
#include "engine/BuiltInEffects.h"
#include "engine/Autosaver.h"
#include "engine/PluginLibrary.h"
#include "engine/PackStore.h"
#include "engine/RealtimeAudioThread.h"
#include "engine/FreesoundStore.h"
#include "engine/LoopKey.h"
#include "engine/RecordingGuard.h"
#include "engine/SfzPlugin.h"
#include "engine/SoundShaper.h"
#include "engine/SoundLibrary.h"
#include "engine/SongExporter.h"
#include "engine/TakeHelpers.h"
#include "engine/TrackController.h"
#include "engine/TrackHelpers.h"
#include "platform/AppFolders.h"
#include "platform/FileLock.h"

#include <tracktion_engine/tracktion_engine.h>

#include <regex>
#include <set>

namespace te = tracktion;   // the conventional short alias used in Tracktion's own examples
using namespace std::literals;

namespace ikaros
{

namespace
{
    // Sessions saved by 0.1.0 can hold effect settings written under their
    // parameter IDs (mix proportion="0.3", 12="0.5"), which aren't valid XML
    // attribute names, so the whole file failed to load. Nothing ever read
    // those attributes back, so dropping them loses nothing. Returns true if
    // the file needed (and got) the repair.
    bool removeInvalidAttributes (const juce::File& sessionFile)
    {
        // An attribute whose "name" is several words, or starts with a digit.
        static const std::regex invalid (R"(\s+(?:[A-Za-z_][\w.\-]*\s+)+[A-Za-z_][\w.\-]*="[^"]*"|\s+\d[\w.\-]*="[^"]*")");

        const auto text = sessionFile.loadFileAsString().toStdString();
        const auto fixed = std::regex_replace (text, invalid, "");

        return fixed != text && sessionFile.replaceWithText (juce::String::fromUTF8 (fixed.c_str()));
    }
}

//==============================================================================
// EngineBehaviour lets an app customise Tracktion's defaults by overriding
// virtual functions.
struct IkarosEngineBehaviour  : public te::EngineBehaviour
{
    bool shouldOpenAudioInputByDefault() override   { return true; }

    // We initialise the device manager ourselves (see AudioEngine::Impl) so we
    // can ask for a plain stereo setup instead of "every channel available".
    bool autoInitialiseDeviceManager() override     { return false; }

    // Render everything on the audio thread itself. Tracktion's default
    // hands parts of each block to a worker per CPU core, and with small
    // buffers (64 samples is 750 blocks a second) waking them costs more than
    // it saves; worse, the workers run at ordinary priority, so when the
    // desktop is busy the audio thread sits waiting for them and the sound
    // breaks up. A session of a beginner's size fits on one core easily.
    int getNumberOfCPUsToUseForAudio() override     { return 1; }

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
        // Tracktion passes in the previous device's list when the device
        // changes: start afresh, or old inputs would linger beside the new.
        descriptions.clear();

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
    Impl (AudioEngine& o, const juce::String& settingsName, bool openAudioDevice)
        : owner (o),
          engine (settingsName, std::make_unique<IkarosUIBehaviour>(), std::make_unique<IkarosEngineBehaviour>())
    {
        // 2 inputs, 2 outputs by default. (Tracktion's default of "every
        // channel" crashed PipeWire's ALSA plugin.) The user can change devices
        // in File > Audio & MIDI Settings, and Tracktion remembers the choice.
        if (openAudioDevice)
        {
            engine.getDeviceManager().initialise (2, 2);
            chooseSmallBufferOnce();

            // After initialise, so its (silent) callback comes after Tracktion's.
            realtimeThread = std::make_unique<RealtimeAudioThread> (engine.getDeviceManager().deviceManager);
        }

        // Our own instrument for SFZ sounds, next to Tracktion's built-in
        // ones, so sessions can create it (and load it back) by type name.
        engine.getPluginManager().createBuiltInType<SfzPlugin>();

        // The hidden plugins behind sampled instruments' Sound tab knobs.
        engine.getPluginManager().createBuiltInType<SoundShaperPlugin>();
        engine.getPluginManager().createBuiltInType<TransposePlugin>();

        // Our own effects, offered beside Tracktion's in "+ Add effect".
        engine.getPluginManager().createBuiltInType<LimiterPlugin>();
        engine.getPluginManager().createBuiltInType<NoiseGatePlugin>();
        engine.getPluginManager().createBuiltInType<DistortionPlugin>();
        engine.getPluginManager().createBuiltInType<TremoloPlugin>();

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
        // asked about unsaved changes; after a crash it would stay behind,
        // to be offered at the next launch.)
        autosaver.detach();
        edit = nullptr;
        deleteWorkFolder (workFolder);
    }

    // What you hear while monitoring is late by about the device's buffer
    // (each way). The first time the app runs, ask for a small one, ~5 ms;
    // after that the choice in Audio & MIDI Settings sticks (Tracktion saves it).
    void chooseSmallBufferOnce()
    {
        auto& props = engine.getPropertyStorage().getPropertiesFile();

        if (props.getBoolValue ("smallBufferChosen"))
            return;

        props.setValue ("smallBufferChosen", true);

        auto& adm = engine.getDeviceManager().deviceManager;
        auto* device = adm.getCurrentAudioDevice();

        if (device == nullptr)
            return;

        // The smallest size the device offers of at least 5 ms (240 samples
        // at 48 kHz). Smaller ones tend to drop out on ordinary computers.
        const auto current = device->getCurrentBufferSizeSamples();
        const auto atLeast = juce::roundToInt (device->getCurrentSampleRate() * 0.005);
        auto best = current;

        for (auto size : device->getAvailableBufferSizes())
            if (size >= atLeast && size < best)
                best = size;

        if (best < current)
        {
            auto setup = adm.getAudioDeviceSetup();
            setup.bufferSize = best;
            adm.setAudioDeviceSetup (setup, true);
        }
    }

    AudioEngine& owner;

    // te::Engine is the root object: device manager, plugin manager, temp files...
    // There should only be one per app.
    te::Engine engine;

    std::unique_ptr<te::Edit> edit;

    // Declared after `engine`, so it's destroyed (and leaves the device) first.
    std::unique_ptr<RealtimeAudioThread> realtimeThread;

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

    // Recovery copies, the "session is open" marker, and takes written as
    // they're recorded (see Autosaver).
    Autosaver autosaver;

    // A session just recovered after a crash: it stays "unsaved" when the
    // clean-slate timer below resets the changed flag.
    bool keepUnsavedAfterSetup = false;

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

    // Working copies left behind by a crash with nothing worth recovering
    // (or from versions before recovery existed) are tidied away after two
    // weeks untouched. Ones with work to recover wait to be offered, and ones
    // open in another copy of the app are left alone.
    static void deleteStaleWorkFolders()
    {
        const auto cutoff = juce::Time::getCurrentTime() - juce::RelativeTime::days (14);

        for (auto& folder : getWorkRoot().findChildFiles (juce::File::findDirectories, false))
        {
            if (platform::FileLock::isHeld (folder.getChildFile (SessionRecovery::lockFile))
                 || SessionRecovery::hasWorkToRecover (folder))
                continue;

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

        if (inWorkFolder.exists())
            return inWorkFolder;

        // Tracktion writes the paths of new recordings (and their takes)
        // relative to the session *file* ("../Audio/Take 1.wav"), one ".."
        // more than the folder. Saving rewrites them.
        if (auto fromSessionFile = sessionFile.getChildFile (path); fromSessionFile.exists())
            return fromSessionFile;

        if (legacySessionFile == juce::File())
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

    // The track being recorded onto and the regions it had before, so the
    // take's new regions can be found when it ends (see TakeEditor).
    TrackId takeTrack = 0;
    std::set<RegionId> regionsBeforeTake;
    bool midiTakesThisTake = false;

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
        bool inSequence = false, inCurve = false;

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

            if (t.hasType (te::IDs::AUTOMATIONCURVE))
                inCurve = true;

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
            // plug-in happens on the track, below.) Automation changes the
            // mixer too: a control that gains or loses its automation looks
            // different.
            c.mixer = true;
            c.automation = inCurve;
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

        if (edit != nullptr && std::exchange (keepUnsavedAfterSetup, false))
            edit->markAsChanged();
    }

    void changeListenerCallback (juce::ChangeBroadcaster*) override
    {
        // Device list changed: re-apply MIDI routing to the selected track.
        if (edit != nullptr)
            owner.getTracks().setSelectedTrack (owner.getTracks().getSelectedTrack());
    }
};

//==============================================================================
AudioEngine::AudioEngine (const juce::String& settingsName, bool openAudioDevice)
    : impl (std::make_unique<Impl> (*this, settingsName, openAudioDevice))
{
    // The first run after an update takes the choices from Tracktion's settings file.
    auto& storage = impl->engine.getPropertyStorage();
    preferences = std::make_unique<Preferences> (storage.getAppPrefsFolder().getChildFile ("Preferences.xml"),
                                                 &storage.getPropertiesFile());

    recordingGuard = std::make_unique<RecordingGuard> (*this);
    deviceWatcher = std::make_unique<AudioDeviceWatcher> (*this);
    sounds = std::make_unique<SoundLibrary> (getUserSoundsFolder());
    packStore = std::make_unique<PackStore> (*sounds);
    packStore->onPacksChanged = [this] (const juce::String& packId) { soundPacksChanged (packId); };
    freesoundStore = std::make_unique<FreesoundStore> (*sounds, *preferences);
    freesoundStore->onLibraryChanged = [this] { soundPacksChanged (FreesoundStore::packId); };
    plugins = std::make_unique<PluginLibrary> (*this);
    preview = std::make_unique<AudioPreview> (impl->engine.getDeviceManager().deviceManager,
                                              impl->engine.getAudioFileFormatManager().readFormatManager);
    trackController = std::make_unique<TrackController> (*this);
    exporter = std::make_unique<SongExporter> (*this);
    preferences->addListener (this);
    newSession();
}

AudioEngine::~AudioEngine()
{
    preferences->removeListener (this);

    // Order matters: stop playback and drop anything that references the Edit
    // before the Edit itself goes away, and the Edit before the Engine.
    exporter.reset();   // stops a running export
    deviceWatcher.reset();

    if (impl->edit != nullptr)
        impl->edit->getTransport().stop (false, true);

    recordingGuard.reset();

    impl->detachMasterMeter();
    preview.reset();
    trackController.reset();
    impl->stopObservingEdit();
    impl->edit.reset();   // closes any plug-in windows
    plugins.reset();
    freesoundStore.reset();   // stops downloads
    packStore.reset();    // stops downloads
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

    setTimeSignature (settings.numerator, settings.denominator);   // first: the tempo counts its beats
    setBpm (settings.bpm);
    setKey (settings.keyRoot, settings.minor);

    // A limiter on the master (at -1 dB) keeps the mix and exports from
    // clipping. Only new sessions get one: older sessions open as they were
    // saved, since adding it there would change how they sound.
    trackController->effects().addEffect (masterTrackId, LimiterPlugin::xmlTypeName);

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
static void pointInstrumentsAtLocalFiles (juce::ValueTree tree, const SoundLibrary& sounds,
                                         const juce::String& reloadPackId = {})
{

    using namespace detail;

    for (auto child : tree)
    {
        if (! te::TrackList::isTrack (child))
            continue;

        pointInstrumentsAtLocalFiles (child, sounds, reloadPackId);   // tracks inside folder tracks

        auto* instrument = sounds.resolveSampledInstrument (child[soundIdProp].toString());

        if (instrument == nullptr)
            continue;

        child.setProperty (soundIdProp, instrument->info.id, nullptr);   // saved the new way from now on

        if (auto pack = sounds.findPack (instrument->info.packId))
            child.setProperty (packNameProp, pack->name, nullptr);         // so a computer without it can name it

        // For reloadPackId (a pack just installed into the open session),
        // each path is cleared and set again, so the instrument loads its
        // files even if the path is the same as before. Not undoable: it's
        // not an edit.
        const bool reload = reloadPackId.isNotEmpty() && instrument->info.packId == reloadPackId;

        auto setPath = [reload] (juce::ValueTree v, const juce::Identifier& property, const juce::String& path)
        {
            if (reload)
                v.setProperty (property, {}, nullptr);

            v.setProperty (property, path, nullptr);
        };

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
                            setPath (sound, te::IDs::source, zone.file.getFullPathName());
            }
            else if (type == SfzPlugin::xmlTypeName && instrument->sfzFile.existsAsFile())
            {
                setPath (plugin, SfzPlugin::sfzFileProp, instrument->sfzFile.getFullPathName());
            }
        }
    }
}

void AudioEngine::soundPacksChanged (const juce::String& packId)
{
    sounds->rescan();

    if (impl->edit != nullptr)
        pointInstrumentsAtLocalFiles (impl->edit->state, *sounds, packId);

    notifyModelChanged();   // everything: the library, and tracks' missing-pack state
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

    if (unpacked && ! state.isValid() && removeInvalidAttributes (workSession))
        state = te::loadEditFromFile (impl->engine, workSession, te::ProjectItemID());

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

// Gives every region what the rest of the app expects: note IDs on MIDI
// notes (so reading a region never has to change the session), and the
// short edge fades on audio (so cuts don't click). For recordings, and for
// sessions from before these existed.
static void prepareRegions (te::Edit& edit)
{
    for (auto* track : te::getAudioTracks (edit))
    {
        for (auto* clip : track->getClips())
        {
            if (auto* midi = dynamic_cast<te::MidiClip*> (clip))
                detail::assignNoteIdsToEveryTake (*midi);   // the piano roll needs them on whichever take plays
            else if (auto* audio = dynamic_cast<te::AudioClipBase*> (clip))
                detail::ensureEdgeFades (*audio);
        }
    }
}

void AudioEngine::loadEdit (const juce::ValueTree& state, const juce::File& workFolder,
                            const juce::File& bundle, const juce::File& legacySessionFile)
{
    exporter->cancelAndWait();   // it renders the old Edit

    if (impl->edit != nullptr)
    {
        impl->edit->getTransport().stop (false, true);
        trackController->inputs().allNotesOff();
        recordingGuard->takeEnded();   // if a take was going (it stays in the old session)
        impl->takeTrack = 0;
    }

    impl->detachMasterMeter();
    impl->stopObservingEdit();

    // Let go of anything that references the old Edit's tracks or plugins
    // (meter taps, held notes) *before* the old Edit is destroyed.
    trackController->sessionChanged();

    // The old session's recovery files go (its working copy is about to).
    impl->autosaver.detach();

    // Set before creating the Edit: it finds its audio files from here while loading.
    const auto oldWorkFolder = impl->workFolder;
    impl->workFolder = workFolder;
    impl->sessionFile = workFolder.getChildFile (SessionBundle::sessionEntry);
    impl->bundleFile = bundle;
    impl->legacySessionFile = legacySessionFile;
    impl->untitled = bundle == juce::File();
    impl->edit = impl->createEdit (state);   // the old Edit is destroyed here
    prepareRegions (*impl->edit);
    impl->keepUnsavedAfterSetup = false;

    impl->autosaver.attach (*impl->edit, workFolder, [this]
    {
        SessionRecovery::Info info;
        info.name = getSessionName();
        info.bundle = impl->untitled ? juce::File() : impl->bundleFile;
        info.legacySession = impl->legacySessionFile;
        return info;
    });

    // ...so the old session's working copy isn't needed any more.
    if (oldWorkFolder != workFolder)
        Impl::deleteWorkFolder (oldWorkFolder);

    auto& edit = *impl->edit;

    // Keep the audio graph running while stopped so live MIDI (keyboard) is audible.
    edit.playInStopEnabled = true;
    applyCountIn();

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

juce::var AudioEngine::getAppSetting (const juce::String& name, const juce::var& defaultValue) const
{
    auto& props = impl->engine.getPropertyStorage().getPropertiesFile();
    return props.containsKey (name) ? juce::var (props.getValue (name)) : defaultValue;
}

void AudioEngine::setAppSetting (const juce::String& name, const juce::var& value)
{
    auto& props = impl->engine.getPropertyStorage().getPropertiesFile();
    props.setValue (name, value);
    props.saveIfNeeded();
}

juce::File AudioEngine::getStandardSessionFolder()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Ikaros Studio");
}

juce::File AudioEngine::getSessionFolder() const
{
    auto folder = preferences->getFolder (settings::sessionFolder, getStandardSessionFolder());
    folder.createDirectory();
    return folder;
}

void AudioEngine::clearRecentSessions()
{
    auto& props = impl->engine.getPropertyStorage().getPropertiesFile();
    props.removeValue ("recentSessions");
    props.saveIfNeeded();
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

    // Audio regions, and each of their takes, refer to their files by path.
    // Find every file (while the paths still resolve as they did). `node` is
    // the clip's state or one of its TAKEs: the tree holding the path.
    struct Source { juce::ValueTree node; juce::File file; juce::var oldPath; };
    std::vector<Source> sources;

    for (auto* track : te::getAudioTracks (edit))
    {
        for (auto* clip : track->getClips())
        {
            if (dynamic_cast<te::WaveAudioClip*> (clip) == nullptr)
                continue;

            sources.push_back ({ clip->state, clip->getSourceFileReference().getFile(), clip->state[te::IDs::source] });

            for (auto take : detail::getTakesTree (*clip))
                sources.push_back ({ take, te::SourceFileReference (edit, take, te::IDs::source).getFile(), take[te::IDs::source] });
        }
    }

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
        source.node.setProperty (te::IDs::source, path, nullptr);
    }

    // Writing the file first flushes every plug-in's settings into the state,
    // and some plug-ins (4OSC) do that through the undo manager. Left alone,
    // that would be an invisible "undo step" that also throws away Redo. So
    // the flush gets a transaction of its own, which we take straight back off
    // the history: undoCurrentTransactionOnly() also puts back the redo steps
    // JUCE set aside. (Undoing it changes nothing: the flush only rewrote
    // settings with the values they already had.)
    auto& undoManager = edit.getUndoManager();
    undoManager.beginNewTransaction();
    const bool written = te::EditFileOperations (edit).writeToFile (impl->sessionFile, false);
    undoManager.undoCurrentTransactionOnly();   // does nothing if the flush recorded nothing

    if (! written)
    {
        // Point everything back at the files it used before. (Copies made in
        // the Audio folder do no harm.)
        for (auto& source : sources)
            source.node.setProperty (te::IDs::source, source.oldPath, nullptr);

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
    impl->autosaver.sessionSaved();   // the saved session has it all now
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

//==============================================================================
// Crash recovery

std::vector<RecoverableSession> AudioEngine::findRecoverableSessions()
{
    std::vector<RecoverableSession> found;

    for (auto& folder : SessionRecovery::findCrashedWorkFolders (Impl::getWorkRoot()))
    {
        if (folder == impl->workFolder)
            continue;   // (can't happen: ours is locked. Just in case.)

        auto info = SessionRecovery::readInfo (folder);

        if (! info.has_value() || ! SessionRecovery::hasWorkToRecover (folder))
        {
            Impl::deleteWorkFolder (folder);   // nothing unsaved: nothing to offer
            continue;
        }

        found.push_back ({ info->name.isNotEmpty() ? info->name : juce::String ("Untitled Session"),
                           SessionRecovery::getLastWriteTime (folder, *info),
                           info->bundle == juce::File(),
                           folder });
    }

    std::sort (found.begin(), found.end(), [] (auto& a, auto& b) { return a.savedAt > b.savedAt; });
    return found;
}

// Puts a take that a crash cut short back on its track: audio from the file
// Tracktion was recording into (repaired first), MIDI from the notes written
// as they were played. Not an undo step: it's part of the recovered session.
static bool restoreTake (te::Edit& edit, const SessionRecovery::Take& take, const juce::File& workFolder)
{
    using namespace detail;   // findTrack, beatToTime, timeToBeat (TrackHelpers.h)

    auto* track = findTrack (&edit, take.track);
    auto file = workFolder.getChildFile (take.path);

    if (track == nullptr || ! file.existsAsFile())
        return false;

    const auto start = beatToTime (edit, take.startBeats);
    const bool cycle = take.loopEndBeats > take.startBeats;
    const auto loopEnd = beatToTime (edit, cycle ? take.loopEndBeats : take.startBeats);

    if (! take.midi)
    {
        SessionRecovery::repairWavFile (file);
        const auto seconds = te::AudioFile (edit.engine, file).getLength();

        if (seconds <= 0.0)
            return false;

        auto clip = track->insertWaveClip ("Recovered Take", file, { { start, start + te::TimeDuration::fromSeconds (seconds) }, {} }, false);

        // A cycle recording's file holds every pass, one after the other.
        if (clip != nullptr && cycle)
            detail::splitIntoPassTakes (*clip, loopEnd - start);

        return clip != nullptr;
    }

    // Pair each note-on with its note-off. Times are seconds on the timeline.
    // During a cycle recording they're folded into the loop, so a time that
    // jumps back to the loop's start begins the next pass.
    struct Played { int pitch, velocity; double start, end; int pass; };
    std::vector<Played> notes;
    std::map<int, size_t> sounding;   // channel/pitch -> index in notes
    double lastTime = start.inSeconds();
    const double loopSeconds = (loopEnd - start).inSeconds();
    int pass = 0;

    for (auto& m : SessionRecovery::readJournal (file))
    {
        const auto t = m.getTimeStamp();
        const int key = m.getChannel() * 128 + m.getNoteNumber();

        if (cycle && t + loopSeconds * 0.5 < lastTime)
            ++pass;

        lastTime = cycle ? t : juce::jmax (lastTime, t);

        if (m.isNoteOn())
        {
            if (auto it = sounding.find (key); it != sounding.end())
                notes[it->second].end = t;   // played again before it was let go

            sounding[key] = notes.size();
            notes.push_back ({ m.getNoteNumber(), m.getVelocity(), t, t, pass });
        }
        else if (m.isNoteOff())
        {
            if (auto it = sounding.find (key); it != sounding.end())
            {
                notes[it->second].end = t;
                sounding.erase (it);
            }
        }
    }

    // Notes still held when the crash came end with the last thing played.
    for (auto& [key, index] : sounding)
        notes[index].end = lastTime;

    // A note held over the end of the loop ends there.
    for (auto& n : notes)
        if (cycle && n.end < n.start)
            n.end = loopEnd.inSeconds();

    if (notes.empty())
        return false;

    double startBeat = take.startBeats, endBeat = take.startBeats + 1.0;

    if (cycle)
    {
        endBeat = take.loopEndBeats;
    }
    else
    {
        for (auto& n : notes)
        {
            startBeat = juce::jmin (startBeat, timeToBeat (edit, te::TimePosition::fromSeconds (n.start)));
            endBeat = juce::jmax (endBeat, timeToBeat (edit, te::TimePosition::fromSeconds (n.end)));
        }

        endBeat = std::ceil (endBeat);
    }

    auto clip = track->insertMIDIClip ("Recovered Take", { beatToTime (edit, startBeat), beatToTime (edit, endBeat) }, nullptr);

    if (clip == nullptr)
        return false;

    // Merged, every note goes in the region; as takes, the first pass's notes
    // do and each later pass becomes a take (Tracktion's addTake wants
    // timeline seconds).
    const int firstPass = notes.front().pass;

    for (auto& n : notes)
    {
        if (take.midiTakes && n.pass != firstPass)
            continue;

        const auto noteStart = timeToBeat (edit, te::TimePosition::fromSeconds (n.start));
        const auto length = juce::jmax (1.0 / 64.0, timeToBeat (edit, te::TimePosition::fromSeconds (n.end)) - noteStart);
        clip->getSequence().addNote (n.pitch, te::BeatPosition::fromBeats (noteStart - startBeat),
                                     te::BeatDuration::fromBeats (length), n.velocity, 0, nullptr);
    }

    if (take.midiTakes)
    {
        for (int p = firstPass + 1; p <= notes.back().pass; ++p)
        {
            juce::MidiMessageSequence played;

            for (auto& n : notes)
            {
                if (n.pass == p)
                {
                    played.addEvent (juce::MidiMessage::noteOn (1, n.pitch, (juce::uint8) n.velocity), n.start);
                    played.addEvent (juce::MidiMessage::noteOff (1, n.pitch), n.end);
                }
            }

            if (played.getNumEvents() > 0)   // Tracktion leaves out passes without notes too
            {
                played.updateMatchedPairs();
                clip->addTake (played, te::MidiList::NoteAutomationType::none);
            }
        }
    }

    return true;
}

bool AudioEngine::recoverSession (const RecoverableSession& session)
{
    const auto folder = session.folder;

    // Only crashed working copies: never one another copy of the app has open.
    if (! folder.isAChildOf (Impl::getWorkRoot())
         || platform::FileLock::isHeld (folder.getChildFile (SessionRecovery::lockFile)))
        return false;

    auto info = SessionRecovery::readInfo (folder);

    if (! info.has_value())
        return false;

    // The autosave; without one, the session as it was opened (and takes go back into that).
    auto autosave = folder.getChildFile (SessionRecovery::autosaveFile);
    auto file = autosave.existsAsFile() ? autosave : folder.getChildFile (SessionBundle::sessionEntry);
    auto state = te::loadEditFromFile (impl->engine, file, te::ProjectItemID());

    if (! state.isValid() && removeInvalidAttributes (file))
        state = te::loadEditFromFile (impl->engine, file, te::ProjectItemID());

    if (! state.isValid())
        return false;

    const auto autosaveId = state[SessionRecovery::autosaveIdProperty].toString();
    state.removeProperty (SessionRecovery::autosaveIdProperty, nullptr);
    pointInstrumentsAtLocalFiles (state, *sounds);

    // The crashed session's working copy (with its recordings) becomes this
    // session's; the current one's is deleted.
    loadEdit (state, folder, info->bundle, info->legacySession);

    // Takes recorded after the autosave aren't in it yet.
    for (auto& take : info->takes)
        if (take.recordedAfter == autosaveId || ! autosave.existsAsFile())
            restoreTake (*impl->edit, take, folder);

    prepareRegions (*impl->edit);

    auto tracks = trackController->getTracks();
    trackController->setSelectedTrack (tracks.empty() ? 0 : tracks.front().id);

    // Unsaved: the user decides whether this becomes their saved session.
    impl->startFromCleanSlateSoon();
    impl->keepUnsavedAfterSetup = true;
    impl->edit->markAsChanged();

    // A fresh recovery copy with the takes in it (this also replaces the old
    // Recovery.json, takes and all).
    impl->autosaver.autosaveNow();

    notifyModelChanged();
    return true;
}

void AudioEngine::discardRecoverableSession (const RecoverableSession& session)
{
    if (! platform::FileLock::isHeld (session.folder.getChildFile (SessionRecovery::lockFile)))
        Impl::deleteWorkFolder (session.folder);
}

void AudioEngine::autosaveNow()                 { impl->autosaver.autosaveNow(); }
void AudioEngine::waitForAutosave()             { impl->autosaver.waitUntilWritten(); }
juce::Time AudioEngine::getLastAutosaveTime() const   { return impl->autosaver.getLastAutosaveTime(); }
juce::File AudioEngine::getWorkingCopyFolder() const  { return impl->workFolder; }

void AudioEngine::setAutosaveTiming (int quietMs, int minIntervalMs, int maxWaitMs)
{
    impl->autosaver.setTiming ({ quietMs, minIntervalMs, maxWaitMs });
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
    {
        impl->autosaver.recordingStopping();   // writes the last of the MIDI first
        beginUndoStep ("Record");   // the new region is added as one undo step
    }

    transport.stop (false, false);   // false = keep the recording

    if (wasRecording)
        finishTake();
}

void AudioEngine::finishTake()
{
    // The regions the take made: cycle passes become takes, and so does
    // what was recorded over.
    if (auto* track = detail::findTrack (impl->edit.get(), std::exchange (impl->takeTrack, TrackId {})))
    {
        std::vector<RegionId> recorded;

        for (auto* clip : track->getClips())
            if (impl->regionsBeforeTake.count (clip->itemID.getRawID()) == 0)
                recorded.push_back (clip->itemID.getRawID());

        trackController->takes().gatherRecordedTakes (recorded, impl->midiTakesThisTake);
    }

    prepareRegions (*impl->edit);

    if (impl->autoArmedTrack != 0)
        trackController->inputs().setArmed (std::exchange (impl->autoArmedTrack, TrackId {}), false);

    recordingGuard->takeEnded();
}

// Tracktion's shortest loop for cycle recording.
static constexpr auto minimumCycleSeconds = te::TimeDuration::fromSeconds (2.0);

void AudioEngine::toggleRecord()
{
    auto& transport = impl->edit->getTransport();

    if (transport.isRecording())
    {
        stop();
        return;
    }

    // The arm follows the selection by default; with that preference off the
    // user arms tracks themselves, so a take only goes where they put it.
    const bool autoArm = preferences->get (settings::autoArmTrack);

    if (autoArm)
        impl->autoArmedTrack = trackController->inputs().armForRecording();

    const auto armed = trackController->inputs().getArmedTrack();

    if (armed == 0)
    {
        // With auto-arm on, this only happens with no track to record onto.
        if (! autoArm)
            recordingGuard->refuseTake (RecordingStopReason::noTrackArmed);

        return;
    }

    // Cycle recording always starts at the start of the loop: Tracktion cuts
    // the recording into passes from there. It won't cycle-record a loop
    // under 2 seconds (and would only say so through its own UI hooks).
    const bool cycle = transport.looping && ! transport.getLoopRange().isEmpty();

    const bool loopTooShort = cycle && transport.getLoopRange().getLength() < minimumCycleSeconds;

    if (loopTooShort)
        recordingGuard->refuseTake (RecordingStopReason::loopTooShort);

    if (loopTooShort || ! recordingGuard->canStartTake())   // canStartTake checks the disk space
    {
        // The report explains.
        if (impl->autoArmedTrack != 0)
            trackController->inputs().setArmed (std::exchange (impl->autoArmedTrack, TrackId {}), false);

        return;
    }

    if (cycle)
        transport.setPosition (transport.getLoopRange().getStart());

    // MIDI passes are merged into one region unless the user asked for takes.
    // (Tracktion's setting is per MIDI input; recording without the loop
    // always merges, as it did before takes.)
    impl->midiTakesThisTake = cycle && getMidiCycleTakes();

    for (auto& midiIn : impl->engine.getDeviceManager().getMidiInDevices())
        midiIn->mergeRecordings = ! impl->midiTakesThisTake;

    impl->takeTrack = armed;
    impl->regionsBeforeTake.clear();

    if (auto* track = detail::findTrack (impl->edit.get(), armed))
        for (auto* clip : track->getClips())
            impl->regionsBeforeTake.insert (clip->itemID.getRawID());

    // With the count-in on, Tracktion starts playback a bar early and clicks
    // through that bar (even with the metronome off); the take itself starts here.
    auto& tempo = impl->edit->tempoSequence;
    impl->recordStartBeats = tempo.toBeats (transport.getPosition()).inBeats();
    const double loopEndBeats = cycle ? tempo.toBeats (transport.getLoopRange().getEnd()).inBeats() : 0.0;
    impl->autosaver.recordingStarting (impl->recordStartBeats, loopEndBeats, impl->midiTakesThisTake);   // so a crash mid-take keeps the take
    transport.record (false);

    if (! transport.isRecording())
        impl->takeTrack = 0;

    if (transport.isRecording())
    {
        const auto track = trackController->getTrack (armed);
        const bool audioTake = track.has_value() && track->kind == TrackKind::audio;
        recordingGuard->takeStarting (audioTake ? trackController->inputs().getRecordingSettings (armed).inputId : juce::String());
        trackController->inputs().clearClipIndicator (armed);   // the clip light shows this take
    }
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
        setLoopRangeBeats (0.0, getTempoMap().barToBeat (4));

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

void AudioEngine::setMidiCycleTakes (bool keepAsTakes)
{
    preferences->set (settings::midiCycleTakes, keepAsTakes);
}

bool AudioEngine::getMidiCycleTakes() const
{
    return preferences->get (settings::midiCycleTakes);
}

void AudioEngine::setCountInEnabled (bool enabled)
{
    // On: as many bars as were last chosen (one, until 2 is picked).
    const int bars = impl->engine.getPropertyStorage().getPropertiesFile().getIntValue ("countInBars", 1);
    setCountInBars (enabled ? bars : 0);
}

void AudioEngine::setCountInBars (int bars)
{
    preferences->set (settings::countIn, juce::jlimit (0, 2, bars));   // applyCountIn() does the rest
}

int AudioEngine::getCountInBars() const
{
    return juce::jlimit (0, 2, preferences->get (settings::countIn));
}

int AudioEngine::getCountInBeatsAt (double beat) const
{
    // The overload taking a time comes from external/patches/tracktion-count-in-time-signature.patch:
    // Tracktion itself only looked at the song's first time signature.
    auto& ts = impl->edit->tempoSequence;
    return impl->edit->getNumCountInBeats (ts.toTime (te::BeatPosition::fromBeats (beat)));
}

// Tracktion keeps its own count-in setting (in its settings file); ours is
// the one the user sees, so it's copied there whenever it changes.
void AudioEngine::applyCountIn()
{
    const int bars = getCountInBars();

    if (bars > 0)
        impl->engine.getPropertyStorage().getPropertiesFile().setValue ("countInBars", bars);   // remembered for the button

    if (impl->edit != nullptr)
        impl->edit->setCountInMode (bars == 2 ? te::Edit::CountIn::twoBar
                                  : bars == 1 ? te::Edit::CountIn::oneBar
                                              : te::Edit::CountIn::none);
}

juce::File AudioEngine::getUserSoundsFolder() const
{
    return preferences->getFolder (settings::userSoundsFolder, SoundLibrary::getUserSoundsFolder());
}

void AudioEngine::applySoundsFolder()
{
    const auto folder = getUserSoundsFolder();
    sounds->setUserFolder (folder);
    packStore->setInstallFolder (folder);
}

void AudioEngine::preferenceChanged (const juce::String& key)
{
    const bool all = key.isEmpty();   // a reset

    if (all || key == settings::countIn.key)
        applyCountIn();

    if (all || key == settings::userSoundsFolder.key)
    {
        if (getUserSoundsFolder() != sounds->getUserFolder())
        {
            applySoundsFolder();
            soundPacksChanged ({});   // rescan, and point tracks at the packs the new folder holds
        }
    }

    if (all || key == settings::pluginFolders.key || key == settings::disabledPluginFormats.key)
        plugins->preferencesChanged();
}

//==============================================================================
// Tempo and time signatures
namespace
{
    // The app's BPM counts the beats the metronome clicks: one per count of
    // the time signature (a quarter note in 4/4, an eighth in 6/8). That's
    // also how Tracktion counts beats, but its BPM always counts quarter
    // notes. They're the same for x/4 signatures; for x/8 ones the app's
    // number is twice Tracktion's.
    constexpr double minBpm = 20.0, maxBpm = 300.0;   // also Tracktion's limits

    double toTracktionBpm (double bpm, int denominator)
    {
        return juce::jlimit (te::TempoSetting::minBPM, te::TempoSetting::maxBPM,
                             juce::jlimit (minBpm, maxBpm, bpm) * 4.0 / denominator);
    }

    // Tracktion's tempo curve: 1 means "jump straight to the next tempo" (a
    // step), anything else ramps towards it.
    constexpr float stepCurve = 1.0f;

    // A beat lasts 60 / BPM seconds in the app's terms, so when a time
    // signature change makes Tracktion's quarter-note BPM differ either side
    // of it, there has to be a Tracktion tempo change there too, even if the
    // user never made one. Those are marked with this property, and the
    // tempo map leaves them out.
    const juce::Identifier keepsBeatLengthId ("ikarosKeepsBeatLength");

    TempoMap makeTempoMap (te::TempoSequence& ts)
    {
        std::vector<TempoChange> tempos;
        std::vector<TimeSignatureChange> sigs;

        for (auto* t : ts.getTempos())
        {
            const auto denominator = ts.getTimeSigAt (t->startBeatNumber.get()).denominator.get();

            if (tempos.empty() || ! t->state.getProperty (keepsBeatLengthId))
                tempos.push_back ({ t->startBeatNumber.get().inBeats(), t->getBpm() * denominator / 4.0 });
        }

        for (auto* sig : ts.getTimeSigs())
            sigs.push_back ({ sig->startBeatNumber.get().inBeats(), 0, sig->numerator.get(), sig->denominator.get() });

        return { std::move (tempos), std::move (sigs) };
    }

    // Makes the tempo sequence's `type` children (TEMPO or TIMESIG) match
    // `count` entries: the existing trees are reused in order, extras removed
    // from the end and missing ones appended, then set (tree, i) fills each
    // in. Every step is a ValueTree change through the undo manager (and
    // setting a property to the value it already has records nothing), so
    // the whole rewrite undoes in one go. Tracktion's TempoSetting and
    // TimeSigSetting objects follow their trees by themselves: they're
    // ValueTreeObjectLists, which listen for children being added and removed.
    template <typename SetFn>
    void rewriteChildren (juce::ValueTree parent, const juce::Identifier& type, size_t count,
                          juce::UndoManager* um, SetFn&& set)
    {
        std::vector<juce::ValueTree> trees;

        for (auto child : parent)
            if (child.hasType (type))
                trees.push_back (child);

        while (trees.size() > juce::jmax ((size_t) 1, count))
        {
            parent.removeChild (trees.back(), um);
            trees.pop_back();
        }

        for (size_t i = 0; i < count; ++i)
        {
            if (i == trees.size())
            {
                trees.emplace_back (type);
                parent.appendChild (trees.back(), um);
            }

            set (trees[i], i);
        }
    }

    // Writes a whole tempo map into the edit: time signatures by bar number
    // (working out their beats one after another, so later ones keep their
    // bar numbers when an earlier one gets longer or shorter bars), then the
    // tempo changes, plus the hidden ones that keep a beat's length the same
    // where the time signature's denominator changes.
    void writeTempoSequence (te::Edit& edit, const std::vector<TempoChange>& tempos, std::vector<TimeSignatureChange> sigs)
    {
        // The tempo sequence's own tree, which holds its TEMPO and TIMESIG children.
        auto sequence = edit.state.getChildWithName (te::IDs::TEMPOSEQUENCE);
        auto* um = &edit.getUndoManager();

        std::vector<TimeSignatureChange> placed;

        for (size_t i = 0; i < sigs.size(); ++i)
        {
            sigs[i].numerator = juce::jlimit (1, 16, sigs[i].numerator);
            const double beat = i == 0 ? 0.0 : placed.back().beat + (sigs[i].bar - placed.back().bar) * (double) placed.back().numerator;
            placed.push_back ({ beat, sigs[i].bar, sigs[i].numerator, sigs[i].denominator });
        }

        rewriteChildren (sequence, te::IDs::TIMESIG, placed.size(), um, [&] (juce::ValueTree& tree, size_t i)
        {
            tree.setProperty (te::IDs::startBeat, placed[i].beat, um);
            tree.setProperty (te::IDs::numerator, placed[i].numerator, um);
            tree.setProperty (te::IDs::denominator, placed[i].denominator, um);
        });

        // Lookups in the new layout (TempoMap does the "which one is in
        // effect here" searches).
        const TempoMap layout (tempos, placed);

        struct Entry { double beat, bpm; bool hidden; };
        std::vector<Entry> entries;

        for (auto& t : layout.getTempoChanges())
            entries.push_back ({ t.beat, t.bpm, false });

        for (size_t i = 1; i < placed.size(); ++i)
        {
            const double beat = placed[i].beat;
            const bool tempoThere = std::abs (layout.tempoAt (beat).beat - beat) < 1.0e-6;

            if (placed[i].denominator != placed[i - 1].denominator && ! tempoThere)
                entries.push_back ({ beat, layout.tempoAt (beat).bpm, true });
        }

        std::stable_sort (entries.begin(), entries.end(), [] (const Entry& a, const Entry& b) { return a.beat < b.beat; });

        rewriteChildren (sequence, te::IDs::TEMPO, entries.size(), um, [&] (juce::ValueTree& tree, size_t i)
        {
            const auto& e = entries[i];
            tree.setProperty (te::IDs::startBeat, e.beat, um);
            tree.setProperty (te::IDs::bpm, toTracktionBpm (e.bpm, layout.signatureAt (e.beat).denominator), um);
            tree.setProperty (te::IDs::curve, stepCurve, um);

            if (e.hidden)
                tree.setProperty (keepsBeatLengthId, true, um);
            else
                tree.removeProperty (keepsBeatLengthId, um);
        });
    }
}

void AudioEngine::changeTempoMap (const std::vector<TempoChange>& tempos, const std::vector<TimeSignatureChange>& sigs)
{
    auto& edit = *impl->edit;

    // A tempo change moves every later beat in time. Tracktion's remapper
    // notes where (in beats) every region, loop range and automation point
    // is, then puts them back on those beats after the change. Regions of
    // loops and auto-tempo audio are stretched to fit, so they follow the
    // new tempo; recordings just move (they keep their length in seconds).
    //
    // Curves on missing plug-ins have no parameters for the remapper to
    // see; the automation editor keeps those on their beats.
    trackController->automation().keepUnloadedCurvesOnBeats ([&]
    {
        te::EditTimecodeRemapperSnapshot snapshot;
        snapshot.savePreChangeState (edit);
        writeTempoSequence (edit, tempos, sigs);
        snapshot.remapEdit (edit);   // brings Tracktion's tempo data up to date first
    });
}

TempoMap AudioEngine::getTempoMap() const
{
    return impl->edit != nullptr ? makeTempoMap (impl->edit->tempoSequence) : TempoMap();
}

void AudioEngine::setBpm (double bpm)
{
    const auto map = getTempoMap();
    auto tempos = map.getTempoChanges();
    tempos[(size_t) map.indexOfTempoAt (getTransportState().positionBeats)].bpm = juce::jlimit (minBpm, maxBpm, bpm);

    // ValueTree writes inside Tracktion record undo automatically.
    beginUndoStep ("Tempo");
    changeTempoMap (tempos, map.getTimeSignatureChanges());
}

void AudioEngine::addTempoChange (int bar, double bpm)
{
    const auto map = getTempoMap();
    const double beat = map.barToBeat (juce::jmax (0, bar));
    auto tempos = map.getTempoChanges();
    const int existing = map.indexOfTempoAt (beat);

    if (std::abs (tempos[(size_t) existing].beat - beat) < 1.0e-6)
    {
        // Already a change on this bar: it gets the new tempo instead.
        beginUndoStep ("Tempo");
        tempos[(size_t) existing].bpm = juce::jlimit (minBpm, maxBpm, bpm);
    }
    else
    {
        beginUndoStep ("Add Tempo Change");
        tempos.insert (tempos.begin() + existing + 1, { beat, juce::jlimit (minBpm, maxBpm, bpm) });
    }

    changeTempoMap (tempos, map.getTimeSignatureChanges());
}

bool AudioEngine::setTempoChange (int index, double beat, double bpm)
{
    const auto map = getTempoMap();
    auto tempos = map.getTempoChanges();

    if (index < 0 || index >= (int) tempos.size())
        return false;

    if (index == 0)
    {
        beat = 0.0;
    }
    else
    {
        const double prev = tempos[(size_t) index - 1].beat;
        const double next = index + 1 < (int) tempos.size() ? tempos[(size_t) index + 1].beat : std::numeric_limits<double>::max();

        if (beat <= prev + 1.0e-6 || beat >= next - 1.0e-6)
            return false;
    }

    tempos[(size_t) index] = { beat, juce::jlimit (minBpm, maxBpm, bpm) };
    changeTempoMap (tempos, map.getTimeSignatureChanges());
    return true;
}

void AudioEngine::removeTempoChange (int index)
{
    const auto map = getTempoMap();
    auto tempos = map.getTempoChanges();

    if (index <= 0 || index >= (int) tempos.size())
        return;

    beginUndoStep ("Delete Tempo Change");
    tempos.erase (tempos.begin() + index);
    changeTempoMap (tempos, map.getTimeSignatureChanges());
}

void AudioEngine::setTimeSignature (int numerator, int denominator)
{
    setTimeSignatureChange (getTempoMap().indexOfSignatureAt (getTransportState().positionBeats), numerator, denominator);
}

void AudioEngine::addTimeSignatureChange (int bar, int numerator, int denominator)
{
    const auto map = getTempoMap();
    auto sigs = map.getTimeSignatureChanges();
    bar = juce::jmax (0, bar);

    auto at = std::find_if (sigs.begin(), sigs.end(), [bar] (const TimeSignatureChange& s) { return s.bar >= bar; });

    if (at != sigs.end() && at->bar == bar)
    {
        setTimeSignatureChange ((int) std::distance (sigs.begin(), at), numerator, denominator);
        return;
    }

    beginUndoStep ("Add Time Signature Change");
    sigs.insert (at, { 0.0, bar, numerator, denominator });   // the beat is worked out from the bar
    changeTempoMap (map.getTempoChanges(), sigs);
}

void AudioEngine::setTimeSignatureChange (int index, int numerator, int denominator)
{
    const auto map = getTempoMap();
    auto sigs = map.getTimeSignatureChanges();

    if (index < 0 || index >= (int) sigs.size())
        return;

    auto& sig = sigs[(size_t) index];

    if (sig.numerator == numerator && sig.denominator == denominator)
        return;

    beginUndoStep ("Time Signature");
    sig.numerator = numerator;
    sig.denominator = denominator;
    changeTempoMap (map.getTempoChanges(), sigs);
}

void AudioEngine::removeTimeSignatureChange (int index)
{
    const auto map = getTempoMap();
    auto sigs = map.getTimeSignatureChanges();

    if (index <= 0 || index >= (int) sigs.size())
        return;

    beginUndoStep ("Delete Time Signature Change");
    sigs.erase (sigs.begin() + index);
    changeTempoMap (map.getTempoChanges(), sigs);
}

void AudioEngine::setKey (int root, bool minor)
{
    if (auto* pitch = impl->edit->pitchSequence.getPitch (0))
    {
        beginUndoStep ("Key");
        pitch->setPitch (60 + juce::jlimit (0, 11, root));   // stored as a MIDI note: the root above middle C
        pitch->setScaleID (minor ? te::Scale::minor : te::Scale::major);

        // Loops already in the arrangement move to the new key, in the same undo step.
        detail::followSessionKey (*impl->edit);
    }
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

    // What the LCD shows: the tempo and time signature at the playhead
    // (the tempo in the app's terms, see toTracktionBpm).
    auto& sig = ts.getTimeSigAt (pos);
    s.timeSigNumerator = sig.numerator.get();
    s.timeSigDenominator = sig.denominator.get();
    s.bpm = ts.getTempoAt (pos).getBpm() * s.timeSigDenominator / 4.0;

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
    // Automated parameters are skipped: their curve sets them. (Worse,
    // Tracktion moves a one-point curve's point to a value set by hand.)
    for (auto* plugin : te::getAllPlugins (edit, true))
        for (auto* param : plugin->getAutomatableParameters())
            if (! param->hasAutomationPoints())
                param->updateFromAttachedValue();
}

void AudioEngine::undo()
{
    if (! canUndo())
        return;

    trackController->automation().keepValuesWhenAutomationGoes ([this] { impl->edit->undo(); });
    syncParametersFromState (*impl->edit);
}

void AudioEngine::redo()
{
    if (! canRedo())
        return;

    trackController->automation().keepValuesWhenAutomationGoes ([this] { impl->edit->redo(); });
    syncParametersFromState (*impl->edit);
}
// Undo and redo wait while recording (count-in included): the take is being
// written into a track, and undoing that track or its regions away under it
// could lose the take. The menus, shortcuts and toolbar all ask these.
bool AudioEngine::canUndo() const   { return ! impl->edit->getTransport().isRecording() && impl->edit->getUndoManager().canUndo(); }
bool AudioEngine::canRedo() const   { return ! impl->edit->getTransport().isRecording() && impl->edit->getUndoManager().canRedo(); }

juce::String AudioEngine::getUndoDescription() const { return impl->edit->getUndoManager().getUndoDescription(); }
juce::String AudioEngine::getRedoDescription() const { return impl->edit->getUndoManager().getRedoDescription(); }

void AudioEngine::beginUndoStep (const juce::String& name)
{
    impl->userHasMadeChanges = true;
    impl->edit->getUndoManager().beginNewTransaction (name);
}

juce::String AudioEngine::getStateSnapshot() const
{
    // No flushState() here: it records undo actions (see saveSession), and the
    // built-in plug-ins keep their settings in the state tree anyway.
    return impl->edit->state.toXmlString();
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

    if (impl->realtimeThread != nullptr)
        s.realtimeAudio = impl->realtimeThread->isRealtime();

    return s;
}

void AudioEngine::reattachAudioCallbacks()
{
    preview->reattach();
    recordingGuard->reattach();

    if (impl->realtimeThread != nullptr)
        impl->realtimeThread->reattach();
}

std::optional<RecordingReport> AudioEngine::takeRecordingReport()
{
    return recordingGuard->popReport();
}

AudioDeviceStatus AudioEngine::getAudioDeviceStatus() const
{
    return recordingGuard->getDeviceStatus();
}

std::optional<AudioDevicePrompt> AudioEngine::takeAudioDevicePrompt()
{
    return deviceWatcher->popPrompt();
}

void AudioEngine::switchAudioDevice (const AudioDevicePrompt& prompt, std::function<void (juce::String)> done)
{
    deviceWatcher->switchTo (prompt, std::move (done));
}

juce::String AudioEngine::openDevicesFor (const AudioDevicePrompt& prompt)
{
    auto& adm = impl->engine.getDeviceManager().deviceManager;
    auto setup = adm.getAudioDeviceSetup();

    if (prompt.inputName.isNotEmpty())
    {
        setup.inputDeviceName = prompt.inputName;
        setup.useDefaultInputChannels = true;
    }

    if (prompt.outputName.isNotEmpty())
    {
        setup.outputDeviceName = prompt.outputName;
        setup.useDefaultOutputChannels = true;
    }

    return adm.setAudioDeviceSetup (setup, true);
}

void AudioEngine::setMinimumFreeDiskSpace (juce::int64 bytes)
{
    recordingGuard->setMinimumFreeSpace (bytes);
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
    audioSettings = comp.get();
    return comp;
}

} // namespace ikaros
