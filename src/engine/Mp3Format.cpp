#include "engine/Mp3Format.h"
#include <lame.h>

namespace ikaros
{

namespace
{
    const int bitRates[] { 128, 160, 192, 224, 256, 320 };
}

//==============================================================================
class Mp3AudioFormat::Writer  : public juce::AudioFormatWriter
{
public:
    // `lame` is already set up (lame_init_params succeeded); this takes it over.
    Writer (juce::OutputStream* out, double rate, int channels, lame_global_flags* l)
        : juce::AudioFormatWriter (out, "MP3 file", rate, (unsigned int) channels, 32), lame (l)
    {
        // write() is then handed float samples (-1..1) instead of ints.
        usesFloatingPointData = true;
    }

    ~Writer() override
    {
        // Encode what LAME still holds, then go back and fill in the tag
        // frame at the start of the file (its length, for players).
        auto num = lame_encode_flush (lame, mp3.data(), (int) mp3.size());

        if (num > 0)
            output->write (mp3.data(), (size_t) num);

        const auto tagSize = lame_get_lametag_frame (lame, mp3.data(), mp3.size());

        if (tagSize > 0 && tagSize <= mp3.size() && output->setPosition (0))
            output->write (mp3.data(), tagSize);

        lame_close (lame);
        // The base class's destructor flushes and deletes the stream.
    }

    bool write (const int** samples, int numSamples) override
    {
        // usesFloatingPointData is true, so these are really floats.
        auto left = reinterpret_cast<const float*> (samples[0]);
        auto right = numChannels > 1 && samples[1] != nullptr ? reinterpret_cast<const float*> (samples[1]) : left;

        // LAME's own rule of thumb for the worst-case output size.
        const auto needed = (size_t) (numSamples * 5 / 4 + 7200);

        if (mp3.size() < needed)
            mp3.resize (needed);

        const auto num = lame_encode_buffer_ieee_float (lame, left, right, numSamples, mp3.data(), (int) mp3.size());
        return num >= 0 && output->write (mp3.data(), (size_t) num);
    }

private:
    lame_global_flags* lame;
    std::vector<unsigned char> mp3 = std::vector<unsigned char> (8192);   // encoded bytes on their way out
};

//==============================================================================
Mp3AudioFormat::Mp3AudioFormat() : juce::AudioFormat ("MP3 file", ".mp3") {}

juce::StringArray Mp3AudioFormat::getQualityOptions()
{
    juce::StringArray options;

    for (auto rate : bitRates)
        options.add (juce::String (rate) + " kbps");

    return options;
}

std::unique_ptr<juce::AudioFormatWriter> Mp3AudioFormat::createWriterFor (std::unique_ptr<juce::OutputStream>& out,
                                                                          const juce::AudioFormatWriterOptions& options)
{
    const auto channels = options.getNumChannels();

    if (out == nullptr || channels < 1 || channels > 2 || ! getPossibleSampleRates().contains ((int) options.getSampleRate()))
        return nullptr;

    auto* lame = lame_init();

    if (lame == nullptr)
        return nullptr;

    const auto index = juce::jlimit (0, juce::numElementsInArray (bitRates) - 1, options.getQualityOptionIndex());
    lame_set_in_samplerate (lame, (int) options.getSampleRate());
    lame_set_out_samplerate (lame, (int) options.getSampleRate());
    lame_set_num_channels (lame, channels);
    lame_set_mode (lame, channels == 1 ? MONO : JOINT_STEREO);
    lame_set_brate (lame, bitRates[index]);
    lame_set_quality (lame, 2);           // 0 best/slowest .. 9; 2 is LAME's "high quality"
    lame_set_bWriteVbrTag (lame, 1);      // leave room for the tag frame (see ~Writer)

    if (lame_init_params (lame) < 0)
    {
        lame_close (lame);
        return nullptr;
    }

    // The writer owns the stream from here on, so take it out of `out`.
    return std::make_unique<Writer> (out.release(), options.getSampleRate(), channels, lame);
}

} // namespace ikaros
