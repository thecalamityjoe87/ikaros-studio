// A tiny gain effect, built as a VST3/LV2 plug-in for testing plug-in
// hosting (scanning, effects, plug-in windows, saving plug-in state).
// Not part of the app. Build with -DIKAROS_BUILD_TEST_PLUGINS=ON.

#include <juce_audio_processors/juce_audio_processors.h>

class TestGain  : public juce::AudioProcessor
{
public:
    TestGain()
        : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo())
                                           .withOutput ("Output", juce::AudioChannelSet::stereo()))
    {
        addParameter (gain = new juce::AudioParameterFloat ({ "gain", 1 }, "Gain", 0.0f, 1.0f, 0.5f));
    }

    const juce::String getName() const override             { return "Ikaros Test Gain"; }
    bool acceptsMidi() const override                        { return false; }
    bool producesMidi() const override                       { return false; }
    double getTailLengthSeconds() const override             { return 0.0; }
    int getNumPrograms() override                            { return 1; }
    int getCurrentProgram() override                         { return 0; }
    void setCurrentProgram (int) override                    {}
    const juce::String getProgramName (int) override         { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    void prepareToPlay (double, int) override                {}
    void releaseResources() override                         {}

    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        buffer.applyGain (gain->get());
    }

    bool hasEditor() const override                          { return true; }
    juce::AudioProcessorEditor* createEditor() override      { return new juce::GenericAudioProcessorEditor (*this); }

    void getStateInformation (juce::MemoryBlock& data) override
    {
        juce::MemoryOutputStream (data, true).writeFloat (gain->get());
    }

    void setStateInformation (const void* data, int size) override
    {
        juce::MemoryInputStream in (data, (size_t) size, false);
        *gain = in.readFloat();
    }

private:
    juce::AudioParameterFloat* gain = nullptr;
};

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new TestGain();
}
