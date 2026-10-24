#include "engine/AudioPreview.h"

namespace ikaros
{

AudioPreview::AudioPreview (juce::AudioDeviceManager& dm, juce::AudioFormatManager& f)
    : deviceManager (dm), formats (f)
{
    player.setSource (&transport);
    player.setGain (0.8f);
    transport.addChangeListener (this);
}

void AudioPreview::reattach()
{
    if (attached)
    {
        deviceManager.removeAudioCallback (&player);
        deviceManager.addAudioCallback (&player);
    }
}

AudioPreview::~AudioPreview()
{
    detach();
    transport.removeChangeListener (this);
    transport.setSource (nullptr);
    player.setSource (nullptr);
}

bool AudioPreview::play (const juce::File& f)
{
    stop();

    auto* reader = formats.createReaderFor (f);

    if (reader == nullptr)
        return false;

    const auto rate = reader->sampleRate;
    readerSource = std::make_unique<juce::AudioFormatReaderSource> (reader, true);   // true = owns the reader

    // Passing the file's sample rate makes the transport resample to the device's.
    transport.setSource (readerSource.get(), 0, nullptr, rate);
    transport.setPosition (0.0);
    attach();       // prepares the transport for the device's rate and block size
    transport.start();
    file = f;
    return true;
}

void AudioPreview::stop()
{
    // Before detaching: stop() waits (up to a second) for the audio thread
    // to finish the fade-out, which it only does while attached.
    transport.stop();
    detach();
    transport.setSource (nullptr);
    readerSource.reset();
    file = juce::File();
}

juce::File AudioPreview::getPlayingFile() const
{
    return file;
}

// The player is only on the device while a file plays: JUCE's
// AudioSourcePlayer and AudioTransportSource each take a lock on every audio
// callback, even with nothing to play (perf report, A4). Adding and removing
// a callback takes the device manager's callback lock, so the message thread
// waits for the current block at most.
void AudioPreview::attach()
{
    if (! attached)
    {
        deviceManager.addAudioCallback (&player);   // calls player.audioDeviceAboutToStart if the device is open
        attached = true;
    }
}

void AudioPreview::detach()
{
    if (attached)
    {
        deviceManager.removeAudioCallback (&player);   // once this returns, the audio thread no longer calls it
        attached = false;
    }
}

void AudioPreview::changeListenerCallback (juce::ChangeBroadcaster*)
{
    if (readerSource != nullptr && ! transport.isPlaying() && transport.hasStreamFinished())
    {
        detach();
        file = juce::File();
    }
}

} // namespace ikaros
