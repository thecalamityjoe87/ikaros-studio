#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_formats/juce_audio_formats.h>

namespace ikaros
{

//==============================================================================
/**
    Plays an audio file on its own, outside the session, so the user can
    hear a loop or file in the library before using it.

    It adds itself to the audio device as an extra callback; JUCE mixes it
    with the session's output. Call from the message thread.
*/
class AudioPreview  : private juce::ChangeListener
{
public:
    AudioPreview (juce::AudioDeviceManager&, juce::AudioFormatManager&);
    ~AudioPreview() override;

    /** Starts playing a file from the beginning (stopping any other). */
    bool play (const juce::File&);
    void stop();

    /** The file playing now, or an empty File. */
    juce::File getPlayingFile() const;

private:
    juce::AudioDeviceManager& deviceManager;
    juce::AudioFormatManager& formats;

    // source chain: reader -> transport (resampling, start/stop) -> player (device callback)
    std::unique_ptr<juce::AudioFormatReaderSource> readerSource;
    juce::AudioTransportSource transport;
    juce::AudioSourcePlayer player;
    juce::File file;

    void changeListenerCallback (juce::ChangeBroadcaster*) override;   // playback reached the end

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioPreview)
};

} // namespace ikaros
