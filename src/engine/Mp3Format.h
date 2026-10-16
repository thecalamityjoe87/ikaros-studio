#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

namespace ikaros
{

//==============================================================================
/**
    Writes MP3 files with LAME (external/lame), so the exporter can treat MP3
    like JUCE's own formats. Write-only: it can't read MP3s.

    Constant bit rate; the quality options are bit rates ("128 kbps" ...
    "320 kbps"). Each file starts with a LAME/Xing tag frame, which tells
    players its exact length so they can skip the encoder's padding.
*/
class Mp3AudioFormat  : public juce::AudioFormat
{
public:
    Mp3AudioFormat();

    juce::Array<int> getPossibleSampleRates() override  { return { 32000, 44100, 48000 }; }
    juce::Array<int> getPossibleBitDepths() override    { return { 16 }; }
    bool canDoStereo() override                         { return true; }
    bool canDoMono() override                           { return true; }
    bool isCompressed() override                        { return true; }
    juce::StringArray getQualityOptions() override;

    juce::AudioFormatReader* createReaderFor (juce::InputStream*, bool) override   { return nullptr; }

    std::unique_ptr<juce::AudioFormatWriter> createWriterFor (std::unique_ptr<juce::OutputStream>&,
                                                              const juce::AudioFormatWriterOptions&) override;

    // The base class has other createWriterFor overloads; keep them visible
    // (a function declared here would otherwise hide every one of that name).
    using juce::AudioFormat::createWriterFor;

private:
    class Writer;
};

} // namespace ikaros
