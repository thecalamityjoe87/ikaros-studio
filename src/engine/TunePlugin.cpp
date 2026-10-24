#include "engine/TunePlugin.h"
#include <retuner.h>   // external/fat1

namespace te = tracktion;

namespace ikaros
{

namespace
{
    const juce::Identifier keyProp ("tuneKey");
    const juce::Identifier lockProp ("tuneLockToKey");
    const juce::Identifier noteEditsProp ("tuneNoteEdits");
    const juce::Identifier followProp ("tuneFollowMidi");

    // Speed 0..1 is fat1's filter time from 0.5 s (Natural) to 0.02 s,
    // evenly in log time; fast mode is on from here up (Robotic).
    constexpr float fastModeFrom = 0.99f;
    float filterSecondsFor (float speed)   { return 0.5f * std::pow (0.02f / 0.5f, juce::jlimit (0.0f, 1.0f, speed)); }

    // How much fat1 holds on to the note it's on (its "bias", at fat1's default).
    constexpr float noteBias = 0.5f;

    const juce::String minusSign = juce::String::fromUTF8 ("\xe2\x88\x92");
    const juce::String centSign = juce::String::fromUTF8 ("\xc2\xa2");
}

//==============================================================================
// Watches the session for what changes the allowed notes or the MIDI notes:
// the song's key, the followed track's regions, and the tempo (which moves
// those regions in time). Message thread only.
struct TunePlugin::EditWatcher  : private juce::ValueTree::Listener,
                                  private juce::AsyncUpdater
{
    explicit EditWatcher (TunePlugin& o) : owner (o)
    {
        // Tracktion keeps these sequences' trees private; the Edit's state has them.
        pitchState = owner.edit.state.getChildWithName (te::IDs::PITCHSEQUENCE);
        tempoState = owner.edit.state.getChildWithName (te::IDs::TEMPOSEQUENCE);
        pitchState.addListener (this);
        tempoState.addListener (this);
        followTrack();
    }

    ~EditWatcher() override
    {
        pitchState.removeListener (this);
        tempoState.removeListener (this);
        trackState.removeListener (this);
        cancelPendingUpdate();
    }

    /** Starts listening to the track Follow MIDI is set to (or none). */
    void followTrack()
    {
        trackState.removeListener (this);
        trackState = {};

        if (auto id = (juce::int64) owner.state.getProperty (followProp, 0); id != 0)
            if (auto* track = te::findTrackForID (owner.edit, te::EditItemID::fromRawID ((juce::uint64) id)))
                trackState = track->state;

        trackState.addListener (this);
        triggerAsyncUpdate();
    }

private:
    TunePlugin& owner;
    juce::ValueTree pitchState, tempoState, trackState;
    bool keyChanged = false, notesChanged = true;

    void changed (const juce::ValueTree& tree)
    {
        // A ValueTree listener hears about changes anywhere below the tree
        // it's attached to; find which of the three this one is under.
        if (tree == pitchState || tree.isAChildOf (pitchState))
            keyChanged = true;
        else
            notesChanged = true;

        triggerAsyncUpdate();   // several changes in one edit: one rebuild
    }

    void valueTreePropertyChanged (juce::ValueTree& t, const juce::Identifier&) override   { changed (t); }
    void valueTreeChildAdded (juce::ValueTree& t, juce::ValueTree&) override               { changed (t); }
    void valueTreeChildRemoved (juce::ValueTree& t, juce::ValueTree&, int) override        { changed (t); }
    void valueTreeChildOrderChanged (juce::ValueTree& t, int, int) override                { changed (t); }

    void handleAsyncUpdate() override
    {
        if (std::exchange (keyChanged, false))
            owner.updateAllowedNotes();

        if (std::exchange (notesChanged, false))
            owner.rebuildMidiNotes();
    }
};

//==============================================================================
const char* TunePlugin::xmlTypeName = "ikarosTune";

TunePlugin::TunePlugin (te::PluginCreationInfo info) : BuiltInEffectPlugin (info)
{
    correction = addSetting ("correction", "Correction", { 0.0f, 1.0f }, 0.6f, percent);
    speed      = addSetting ("speed", "Speed", { 0.0f, 1.0f }, 0.3f, speedText);
    fineTune   = addSetting ("fineTune", "Fine Tune", { -50.0f, 50.0f }, 0.0f, [] (float cents)
    {
        const int c = juce::roundToInt (cents);
        return (c > 0 ? "+" : c < 0 ? minusSign : juce::String()) + juce::String (std::abs (c)) + " " + centSign;
    });

    // Plain settings: stored on our state through the undo manager, like the
    // knobs, but not automatable.
    key.referTo (state, keyProp, getUndoManager(), songKey);
    lockToKey.referTo (state, lockProp, getUndoManager(), true);
    noteEdits.referTo (state, noteEditsProp, getUndoManager(), 0);
    followTrack.referTo (state, followProp, getUndoManager(), 0);

    watcher = std::make_unique<EditWatcher> (*this);
    followingMidi = followTrack.get() != 0;
    updateAllowedNotes();
}

TunePlugin::~TunePlugin()
{
    watcher.reset();
    notifyListenersOfDeletion();
}

juce::String TunePlugin::speedText (float s)
{
    if (s <= 0.005f)        return "Natural";
    if (s >= fastModeFrom)  return "Robotic";
    return percent (s);
}

//==============================================================================
// Settings
std::pair<int, bool> TunePlugin::getEffectiveKey() const
{
    if (const int k = state.getProperty (keyProp, songKey); k >= 0)   // (from the state: see updateAllowedNotes)
        return { k % 12, k >= 12 };

    // Ikaros keeps one key for the whole session, at the start of the pitch sequence.
    if (auto* pitch = edit.pitchSequence.getPitch (0))
    {
        const auto scale = pitch->getScale();
        return { ((pitch->getPitch() % 12) + 12) % 12, scale == te::Scale::minor || scale == te::Scale::aeolian };
    }

    return { 0, false };
}

int TunePlugin::scaleNotes (int root, bool minor)
{
    // Steps of the major scale and the natural minor, from the root.
    static constexpr int major[] { 0, 2, 4, 5, 7, 9, 11 };
    static constexpr int naturalMinor[] { 0, 2, 3, 5, 7, 8, 10 };
    int mask = 0;

    for (int step : minor ? naturalMinor : major)
        mask |= 1 << ((root + step) % 12);

    return mask;
}

void TunePlugin::updateAllowedNotes()
{
    // Read from the state itself: this runs from our own tree listener,
    // which may be told about a change (an undo, say) before the
    // CachedValues are.
    const auto [root, minor] = getEffectiveKey();
    const bool locked = state.getProperty (lockProp, true);
    const int base = locked ? scaleNotes (root, minor) : 0xfff;
    allowedNotes = (base ^ (int) state.getProperty (noteEditsProp, 0)) & 0xfff;
}

void TunePlugin::setKey (int k)
{
    key = juce::jlimit (songKey, 23, k);
}

void TunePlugin::setLockToKey (bool on)
{
    lockToKey = on;
    noteEdits = 0;   // Lock to Key sets the notes from the scale (or all 12)
}

void TunePlugin::toggleNote (int pitchClass)
{
    noteEdits = noteEdits.get() ^ (1 << (((pitchClass % 12) + 12) % 12));
}

void TunePlugin::setFollowTrack (juce::int64 trackId)
{
    followTrack = trackId;
}

void TunePlugin::valueTreePropertyChanged (juce::ValueTree& v, const juce::Identifier& id)
{
    te::Plugin::valueTreePropertyChanged (v, id);

    // Our own settings changed (an edit, or an undo putting them back).
    if (v == state && (id == keyProp || id == lockProp || id == noteEditsProp))
        updateAllowedNotes();

    if (v == state && id == followProp)
    {
        followingMidi = (juce::int64) state.getProperty (followProp, 0) != 0;

        if (watcher != nullptr)
            watcher->followTrack();
    }
}

void TunePlugin::restorePluginStateFromValueTree (const juce::ValueTree& v)
{
    BuiltInEffectPlugin::restorePluginStateFromValueTree (v);

    // A copied Tune pasted over this one brings its plain settings too.
    for (auto id : { keyProp, lockProp, noteEditsProp, followProp })
        if (v.hasProperty (id))
            state.setProperty (id, v.getProperty (id), getUndoManager());
}

//==============================================================================
// Presets
juce::StringArray TunePlugin::getPresetNames()
{
    return { "Natural", "Hard Tune" };
}

void TunePlugin::applyPreset (int index)
{
    if (! juce::isPositiveAndBelow (index, getPresetNames().size()))
        return;

    const float values[2][2] { { 0.6f, 0.3f }, { 1.0f, 1.0f } };

    for (int i = 0; i < 2; ++i)
    {
        auto& param = i == 0 ? *correction : *speed;

        // Setting the current value first stores it on the state, so undo
        // has a value to go back to (a knob never touched has none).
        param.setParameter (param.getCurrentValue(), juce::sendNotification);
        param.setParameter (values[index][i], juce::sendNotification);
    }
}

//==============================================================================
// The followed track's notes
void TunePlugin::rebuildMidiNotes()
{
    std::vector<MidiNote> notes;

    if (auto id = (juce::int64) state.getProperty (followProp, 0); id != 0)
    {
        if (auto* track = dynamic_cast<te::AudioTrack*> (te::findTrackForID (edit, te::EditItemID::fromRawID ((juce::uint64) id))))
        {
            for (auto* clip : track->getClips())
            {
                auto* midi = dynamic_cast<te::MidiClip*> (clip);

                if (midi == nullptr || midi->isMuted())
                    continue;

                // The looped sequence repeats the notes over a looped region.
                const auto range = midi->getEditTimeRange();

                for (auto* note : midi->getSequenceLooped().getNotes())
                {
                    const auto start = juce::jmax (range.getStart(), note->getEditStartTime (*midi));
                    const auto end = juce::jmin (range.getEnd(), note->getEditEndTime (*midi));

                    if (end > start)
                        notes.push_back ({ start.inSeconds(), end.inSeconds(), note->getNoteNumber() % 12 });
                }
            }
        }
    }

    std::sort (notes.begin(), notes.end(), [] (const MidiNote& a, const MidiNote& b) { return a.start < b.start; });

    // Swapping vectors only swaps their pointers: nothing is allocated or
    // freed while the lock is held. The old notes are freed when `notes`
    // goes out of scope, outside the lock.
    const juce::SpinLock::ScopedLockType lock (notesLock);
    std::swap (midiNotes, notes);
}

int TunePlugin::midiMaskAt (double t)
{
    // Audio thread: never wait for the message thread. If it's swapping the
    // notes right now, use what this block found last time.
    const juce::SpinLock::ScopedTryLockType lock (notesLock);

    if (! lock.isLocked())
        return lastMidiMask;

    int mask = 0;

    for (auto& n : midiNotes)
    {
        if (n.start > t)
            break;   // sorted by start: none of the rest has started

        if (n.end > t)
            mask |= 1 << n.pitchClass;
    }

    lastMidiMask = mask;
    return mask;
}

//==============================================================================
// Processing
double TunePlugin::getLatencySeconds()
{
    return latencySamples / rate;
}

void TunePlugin::initialise (const te::PluginInitialisationInfo& info)
{
    silence.prepare (info.sampleRate, 1.0);   // tail: its latency and pitch tracking
    // Everything that allocates happens here, on the message thread.
    rate = info.sampleRate;
    retuner = std::make_unique<LV2AT::Retuner> ((int) rate);
    retuner->set_refpitch (440.0f);
    retuner->set_notebias (noteBias);

    latencySamples = retuner->get_latency() + retuner->get_upsampler_delay();
    fastLead = retuner->get_fastmode_lead();

    const auto chunk = (size_t) juce::jmax (512, info.blockSizeSamples);
    mono.assign (chunk, 0.0f);
    tuned.assign (chunk, 0.0f);
    delayLine.assign ((size_t) fastLead + 1, 0.0f);
    delayWrite = currentDelay = fadingFrom = fadeLeft = 0;

    rebuildMidiNotes();
}

void TunePlugin::deinitialise()
{
}

void TunePlugin::reset()
{
    // initialise() has just made a fresh retuner (Tracktion calls this right
    // after it, on the message thread); only the fast-mode delay is ours.
    std::fill (delayLine.begin(), delayLine.end(), 0.0f);
    delayWrite = currentDelay = fadingFrom = fadeLeft = 0;
}

void TunePlugin::applyToBuffer (const te::PluginRenderContext& fc)
{
    if (fc.destBuffer == nullptr || retuner == nullptr)
        return;

    // Nothing to do while the input is silent and the tail is over (see SilenceSkip).
    if (! silence.shouldProcess (*fc.destBuffer, fc.bufferStartSample, fc.bufferNumSamples))
        return;

    SCOPED_REALTIME_CHECK
    juce::ScopedNoDenormals noDenormals;   // tiny numbers in the filters are slow on some CPUs

    auto& buffer = *fc.destBuffer;
    const int channels = juce::jmin (2, buffer.getNumChannels());

    if (channels == 0)
        return;

    // The knobs.
    const float speedValue = speed->getCurrentValue();
    const bool fast = speedValue >= fastModeFrom;
    retuner->set_corrgain (correction->getCurrentValue());
    retuner->set_corrfilt (filterSecondsFor (speedValue));
    retuner->set_fastmode (fast ? 1.0f : 0.0f);
    retuner->set_corroffs (fineTune->getCurrentValue() / 100.0f);   // cents -> semitones

    // The notes it can land on: the followed track's while they sound
    // (fat1's MIDI mode, falling back to the allowed notes in between).
    int mask = allowedNotes.load();

    if (followingMidi.load() && fc.isPlaying)
        if (const int midi = midiMaskAt (fc.editTime.getStart().inSeconds()); midi != 0)
            mask = midi;

    retuner->set_notemask (mask);

    // Fast mode reads ahead; delay it back so the latency stays the same.
    if (const int wanted = fast ? fastLead : 0; wanted != currentDelay && fadeLeft == 0)
    {
        fadingFrom = currentDelay;
        currentDelay = wanted;
        fadeLeft = fadeLength;
    }

    const int ringSize = (int) delayLine.size();
    auto readDelayed = [&] (int delay) { return delayLine[(size_t) ((delayWrite - delay + ringSize) % ringSize)]; };

    float* left = buffer.getWritePointer (0, fc.bufferStartSample);
    float* right = channels > 1 ? buffer.getWritePointer (1, fc.bufferStartSample) : nullptr;

    for (int done = 0; done < fc.bufferNumSamples;)
    {
        const int num = juce::jmin ((int) mono.size(), fc.bufferNumSamples - done);

        for (int i = 0; i < num; ++i)
            mono[(size_t) i] = right != nullptr ? 0.5f * (left[done + i] + right[done + i]) : left[done + i];

        retuner->process (num, mono.data(), tuned.data());

        for (int i = 0; i < num; ++i)
        {
            delayLine[(size_t) delayWrite] = tuned[(size_t) i];
            float out = readDelayed (currentDelay);

            if (fadeLeft > 0)
            {
                const float a = (float) fadeLeft / (float) fadeLength;
                out = a * readDelayed (fadingFrom) + (1.0f - a) * out;
                --fadeLeft;
            }

            delayWrite = (delayWrite + 1) % ringSize;
            left[done + i] = out;

            if (right != nullptr)
                right[done + i] = out;
        }

        done += num;
    }

    // The readout's numbers.
    const bool voiced = retuner->is_voiced() && retuner->get_cycle() > 0.0f;
    readingVoiced = voiced;

    if (voiced)
    {
        const float sung = 69.0f + 12.0f * std::log2 ((float) rate / retuner->get_cycle() / 440.0f);
        const int pitchClass = retuner->get_lastnote();
        readingSung = sung;

        // The target is the note of that name nearest to what's sung.
        readingTarget = pitchClass < 0 ? -1
                                       : pitchClass + 12 * juce::roundToInt ((sung - (float) pitchClass) / 12.0f);
    }
}

TunePlugin::Reading TunePlugin::getReading() const
{
    Reading r;
    r.voiced = readingVoiced.load();
    r.sungNote = readingSung.load();
    r.targetNote = readingTarget.load();
    return r;
}

} // namespace ikaros
