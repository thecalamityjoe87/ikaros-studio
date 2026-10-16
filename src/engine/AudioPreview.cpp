#include "engine/AudioPreview.h"

namespace ikaros
{

AudioPreview::AudioPreview (juce::AudioDeviceManager& dm, juce::AudioFormatManager& f)
    : deviceManager (dm), formats (f)
{
    player.setSource (&transport);
    player.setGain (0.8f);
    transport.addChangeListener (this);
    deviceManager.addAudioCallback (&player);
}

void AudioPreview::reattach()
{
    deviceManager.removeAudioCallback (&player);
    deviceManager.addAudioCallback (&player);
}

AudioPreview::~AudioPreview()
{
    deviceManager.removeAudioCallback (&player);
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
    transport.start();
    file = f;
    return true;
}

void AudioPreview::stop()
{
    transport.stop();
    transport.setSource (nullptr);
    readerSource.reset();
    file = juce::File();
}

juce::File AudioPreview::getPlayingFile() const
{
    return file;
}

void AudioPreview::changeListenerCallback (juce::ChangeBroadcaster*)
{
    if (readerSource != nullptr && ! transport.isPlaying() && transport.hasStreamFinished())
        file = juce::File();
}

} // namespace ikaros
