#include "engine/SfzPlugin.h"
#include <sfizz.hpp>

namespace te = tracktion;

namespace ikaros
{

const char* SfzPlugin::xmlTypeName = "ikarosSfz";
const juce::Identifier SfzPlugin::sfzFileProp ("sfzFile");

juce::Identifier SfzPlugin::ccProperty (int controllerNumber)
{
    return juce::Identifier ("cc" + juce::String (controllerNumber));
}

void SfzPlugin::prepareSfizz()
{
    // sfizz keeps some tables in globals that the first synth made fills in
    // (the SIMD dispatch table, the sinc interpolators, the silent wavetable;
    // some behind a plain bool flag). Making that first synth here, before
    // there's any loader thread or audio, means those are only ever written
    // on this thread, and every other thread only reads them.
    // (external/patches/sfizz-init-once.patch stops later synths rewriting them.)
    sfz::Sfizz warmUp;
}

//==============================================================================
namespace
{
    // Threads finishing on their own after their owner has gone (see
    // ~SfzPlugin). Each is deleted once it has ended, so nobody waits for it
    // and it's never killed. JUCE deletes this object when the app quits
    // (that's what DeletedAtShutdown is for), and that waits for any still
    // running. A JUCE singleton: getInstance() makes it the first time.
    class FinishingThreads  : public juce::DeletedAtShutdown,
                              private juce::Timer
    {
    public:
        ~FinishingThreads() override
        {
            for (auto& t : threads)
                t->stopThread (-1);   // -1: wait as long as it takes

            clearSingletonInstance();
        }

        static void adopt (std::unique_ptr<juce::Thread> thread)
        {
            auto* self = getInstance();

            // Already gone (the app is past its shutdown): wait here instead.
            if (self == nullptr)
            {
                thread->stopThread (-1);
                return;
            }

            self->threads.push_back (std::move (thread));
            self->startTimer (500);
        }

        JUCE_DECLARE_SINGLETON_INLINE (FinishingThreads, true)

    private:
        std::vector<std::unique_ptr<juce::Thread>> threads;

        void timerCallback() override
        {
            std::erase_if (threads, [] (auto& t) { return ! t->isThreadRunning(); });

            if (threads.empty())
                stopTimer();
        }
    };
}

//==============================================================================
SfzPlugin::Shared::~Shared()
{
    delete incoming.exchange (nullptr);
    delete outgoing.exchange (nullptr);
}

//==============================================================================
// Loads SFZ files on a thread of its own, and deletes synths the audio thread
// has finished with. It wakes up when asked to load, and every 50 ms anyway
// to tidy up. It only uses `shared`, never the plug-in itself, so it can
// outlive the plug-in.
class SfzPlugin::Loader  : public juce::Thread
{
public:
    explicit Loader (std::shared_ptr<Shared> s) : juce::Thread ("SFZ loader"), shared (std::move (s))
    {
        startThread();
    }

    ~Loader() override
    {
        // Only deleted once it has stopped, or when it's about to (see
        // ~SfzPlugin), so this waits as long as it takes: loadSfzFile can't be
        // interrupted, and killing the thread would leave it running on into
        // freed memory.
        stopThread (-1);
    }

    void request (const juce::File& file, int id)
    {
        {
            const juce::ScopedLock sl (lock);
            requestedFile = file;
            requestedId = id;
        }

        notify();
    }

    void run() override
    {
        int finishedId = 0;

        while (! threadShouldExit())
        {
            // A synth the audio thread has swapped out (delete of nullptr does nothing).
            delete shared->outgoing.exchange (nullptr);

            juce::File file;
            int id = 0;

            {
                const juce::ScopedLock sl (lock);
                file = requestedFile;
                id = requestedId;
            }

            if (id != finishedId)
            {
                // A fresh synth for the new sound. If loading fails it stays
                // empty, and playing it is silent.
                auto synth = std::make_unique<sfz::Sfizz>();
                synth->setSampleRate ((float) shared->currentSampleRate.load());
                synth->setSamplesPerBlock (maxBlockSize);

                const bool ok = file.existsAsFile() && synth->loadSfzFile (file.getFullPathName().toStdString());
                shared->numRegions = ok ? synth->getNumRegions() : 0;

                // Which keys make a sound: the on-screen keyboard shows that range. (Each key is played
                // for one block on this new synth, which the audio thread doesn't have yet.)
                int lowest = -1, highest = -1;

                if (ok)
                {
                    juce::AudioBuffer<float> probe (2, 256);
                    float* channels[2] { probe.getWritePointer (0), probe.getWritePointer (1) };

                    for (int key = 0; key < 128; ++key)
                    {
                        synth->noteOn (0, key, 100);
                        synth->renderBlock (channels, 256, 1);

                        if (synth->getNumActiveVoices() > 0)
                        {
                            if (lowest < 0)
                                lowest = key;

                            highest = key;
                        }

                        synth->allSoundOff();
                        synth->renderBlock (channels, 256, 1);
                    }
                }

                shared->lowestKey = lowest;
                shared->highestKey = highest;

                // Hand it over. If an earlier one is still waiting (never
                // played), it's out of date: delete it.
                delete shared->incoming.exchange (synth.release());

                finishedId = id;
                shared->loadedRequest = id;
                continue;   // another request may have arrived meanwhile
            }

            wait (50);
        }
    }

private:
    std::shared_ptr<Shared> shared;
    juce::CriticalSection lock;   // guards the request (the message thread writes it, this thread reads it)
    juce::File requestedFile;
    int requestedId = 0;
};

//==============================================================================
SfzPlugin::SfzPlugin (te::PluginCreationInfo info) : te::Plugin (info)
{
    // A parameter for each controller the instrument's knobs use, so they
    // can be automated. (Which controllers those are was stored in the
    // state when the instrument was picked: see createSfzInstrument.)
    for (int cc = 0; cc < (int) ccValues.size(); ++cc)
    {
        if (! state.hasProperty (ccProperty (cc)))
            continue;

        CcParameter p;
        p.cc = cc;
        p.value = std::make_unique<juce::CachedValue<float>> (state, ccProperty (cc), getUndoManager(), 0.5f);
        p.parameter = addParam (ccProperty (cc).toString(), "CC " + juce::String (cc), { 0.0f, 1.0f });
        p.parameter->attachToCurrentValue (*p.value);
        ccParameters.push_back (std::move (p));
    }

    readCcValues();
    loader = std::make_unique<Loader> (shared);
    requestLoad();
}

SfzPlugin::~SfzPlugin()
{
    notifyListenersOfDeletion();

    for (auto& p : ccParameters)
        p.parameter->detachFromCurrentValue();

    // Stop the loader. It can't stop in the middle of loading a file, which
    // can take many seconds for a big library: if it hasn't stopped in a
    // moment, it finishes on its own and is deleted afterwards, keeping
    // `shared` (and any synth it hands over there) alive until then.
    loader->signalThreadShouldExit();
    loader->notify();

    if (loader->waitForThreadToExit (200))
        loader.reset();
    else
        FinishingThreads::adopt (std::move (loader));

    delete active;   // the synths in `shared` go with it
}

te::AutomatableParameter* SfzPlugin::getCcParameter (int controllerNumber) const
{
    for (auto& p : ccParameters)
        if (p.cc == controllerNumber)
            return p.parameter.get();

    return nullptr;
}

juce::File SfzPlugin::getSfzFile() const
{
    return juce::File (state.getProperty (sfzFileProp).toString());
}

void SfzPlugin::requestLoad()
{
    loader->request (getSfzFile(), ++requestCounter);
}

void SfzPlugin::valueTreePropertyChanged (juce::ValueTree& v, const juce::Identifier& id)
{
    // A new file (or the old one back, after an undo): load it.
    if (v == state && id == sfzFileProp)
        requestLoad();

    // A knob turned (or put back by an undo).
    if (v == state && id.toString().startsWith ("cc"))
        readCcValues();

    te::Plugin::valueTreePropertyChanged (v, id);
}

void SfzPlugin::restorePluginStateFromValueTree (const juce::ValueTree& v)
{
    if (v.hasProperty (sfzFileProp))
        state.setProperty (sfzFileProp, v.getProperty (sfzFileProp), getUndoManager());

    for (int cc = 0; cc < (int) ccValues.size(); ++cc)
        if (v.hasProperty (ccProperty (cc)))
            state.setProperty (ccProperty (cc), v.getProperty (ccProperty (cc)), getUndoManager());
}

void SfzPlugin::readCcValues()
{
    for (int cc = 0; cc < (int) ccValues.size(); ++cc)
    {
        const auto& value = state.getProperty (ccProperty (cc));
        ccValues[(size_t) cc] = value.isVoid() ? -1.0f : juce::jlimit (0.0f, 1.0f, (float) value);
    }

    ccValuesChanged = true;
}

void SfzPlugin::sendCcValues()
{
    for (int cc = 0; cc < (int) ccValues.size(); ++cc)
    {
        const float value = ccValues[(size_t) cc].load();

        if (value >= 0.0f)
            active->hdcc (0, cc, value);    // "high definition" CC: 0..1 rather than 0..127
    }
}

//==============================================================================
void SfzPlugin::initialise (const te::PluginInitialisationInfo& info)
{
    scratch.setSize (2, maxBlockSize);

    // A different sample rate needs a synth made for it. (Tracktion doesn't
    // run this while the plugin is processing, so `active` can be touched.)
    if (info.sampleRate != shared->currentSampleRate.load())
    {
        shared->currentSampleRate = info.sampleRate;

        if (active != nullptr)
            active->setSampleRate ((float) info.sampleRate);

        requestLoad();
    }

    if (takeIncomingSynth())
        ccValuesChanged = true;    // the new synth gets the knobs' values on the first block
}

void SfzPlugin::deinitialise()
{
    if (active != nullptr)
        active->allSoundOff();
}

void SfzPlugin::reset()
{
    if (active != nullptr)
        active->allSoundOff();
}

bool SfzPlugin::takeIncomingSynth()
{
    // Only swap when the loader has deleted the last synth we gave back, so
    // there's never more than one waiting to be deleted.
    if (shared->outgoing.load() != nullptr)
        return false;

    if (auto* next = shared->incoming.exchange (nullptr))
    {
        shared->outgoing.store (active);   // the loader deletes it (nullptr the first time)
        active = next;
        return true;
    }

    return false;
}

//==============================================================================
void SfzPlugin::applyToBuffer (const te::PluginRenderContext& fc)
{
    if (fc.destBuffer == nullptr)
        return;

    const bool newSynth = takeIncomingSynth();

    auto& dest = *fc.destBuffer;
    const int start = fc.bufferStartSample;
    const int total = fc.bufferNumSamples;

    // The instrument's output replaces whatever came in.
    for (int ch = 0; ch < dest.getNumChannels(); ++ch)
        dest.clear (ch, start, total);

    if (active == nullptr)
        return;

    // The knobs' controller values: a new synth needs all of them; otherwise
    // only send them when one has changed. (exchange reads the flag and
    // clears it in one step, so a change made meanwhile isn't lost.)
    if (ccValuesChanged.exchange (false) || newSynth)
        sendCcValues();

    auto* midi = fc.bufferForMidiMessages;

    if (midi != nullptr && midi->isAllNotesOff)
        active->allSoundOff();

    // Render in pieces of at most maxBlockSize samples (sfizz's limit),
    // sending each MIDI message along with the piece it falls in. Message
    // times are in seconds from the start of this block.
    for (int done = 0; done < total; done += maxBlockSize)
    {
        const int num = juce::jmin (maxBlockSize, total - done);
        const bool lastPiece = done + num >= total;

        if (midi != nullptr)
        {
            for (auto& m : *midi)
            {
                const int at = juce::roundToInt (m.getTimeStamp() * sampleRate);

                // Messages before this piece were sent with an earlier one;
                // ones after the end of the block go with the last piece.
                if (at < done || (at >= done + num && ! lastPiece))
                    continue;

                const int delay = juce::jlimit (0, num - 1, at - done);

                if (m.isNoteOn())                    active->noteOn (delay, m.getNoteNumber(), m.getVelocity());
                else if (m.isNoteOff())              active->noteOff (delay, m.getNoteNumber(), m.getVelocity());
                else if (m.isController())           active->cc (delay, m.getControllerNumber(), m.getControllerValue());
                else if (m.isPitchWheel())           active->pitchWheel (delay, m.getPitchWheelValue() - 8192);
                else if (m.isChannelPressure())      active->channelAftertouch (delay, m.getChannelPressureValue());
                else if (m.isAftertouch())           active->polyAftertouch (delay, m.getNoteNumber(), m.getAfterTouchValue());
                else if (m.isAllNotesOff() || m.isAllSoundOff())  active->allSoundOff();
            }
        }

        float* outputs[] { scratch.getWritePointer (0), scratch.getWritePointer (1) };
        active->renderBlock (outputs, (size_t) num);

        if (dest.getNumChannels() >= 2)
        {
            dest.copyFrom (0, start + done, scratch, 0, 0, num);
            dest.copyFrom (1, start + done, scratch, 1, 0, num);
        }
        else if (dest.getNumChannels() == 1)
        {
            // A mono track: the average of left and right.
            dest.copyFrom (0, start + done, scratch, 0, 0, num);
            dest.addFrom (0, start + done, scratch, 1, 0, num);
            dest.applyGain (0, start + done, num, 0.5f);
        }
    }
}

} // namespace ikaros
