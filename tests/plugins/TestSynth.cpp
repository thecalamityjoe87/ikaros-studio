// A tiny sine-wave synth, built as a VST3/LV2 plug-in for testing plug-in
// hosting (scanning, instruments, plug-in windows, saving plug-in state).
// Not part of the app. Build with -DIKAROS_BUILD_TEST_PLUGINS=ON.

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>

namespace
{
    struct SineSound  : public juce::SynthesiserSound
    {
        bool appliesToNote (int) override      { return true; }
        bool appliesToChannel (int) override   { return true; }
    };

    struct SineVoice  : public juce::SynthesiserVoice
    {
        explicit SineVoice (std::atomic<float>& l) : level (l) {}

        bool canPlaySound (juce::SynthesiserSound* s) override   { return dynamic_cast<SineSound*> (s) != nullptr; }

        void startNote (int note, float velocity, juce::SynthesiserSound*, int) override
        {
            angle = 0.0;
            gain = velocity * 0.2;
            tail = 0.0;
            delta = juce::MathConstants<double>::twoPi * juce::MidiMessage::getMidiNoteInHertz (note) / getSampleRate();
        }

        void stopNote (float, bool allowTailOff) override
        {
            if (allowTailOff)
                tail = 1.0;
            else
                clearCurrentNote(), delta = 0.0;
        }

        void pitchWheelMoved (int) override {}
        void controllerMoved (int, int) override {}

        void renderNextBlock (juce::AudioBuffer<float>& out, int start, int num) override
        {
            if (delta == 0.0)
                return;

            for (int i = start; i < start + num; ++i)
            {
                auto sample = (float) (std::sin (angle) * gain * level.load() * (tail > 0.0 ? tail : 1.0));

                for (int ch = 0; ch < out.getNumChannels(); ++ch)
                    out.addSample (ch, i, sample);

                angle += delta;

                if (tail > 0.0 && (tail *= 0.999) < 0.005)
                {
                    clearCurrentNote();
                    delta = 0.0;
                    break;
                }
            }
        }

        std::atomic<float>& level;
        double angle = 0.0, delta = 0.0, gain = 0.0, tail = 0.0;
    };
}

//==============================================================================
class TestSynth  : public juce::AudioProcessor
{
public:
    TestSynth()
        : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo()))
    {
        addParameter (level = new juce::AudioParameterFloat ({ "level", 1 }, "Level", 0.0f, 1.0f, 0.8f));

        for (int i = 0; i < 8; ++i)
            synth.addVoice (new SineVoice (levelValue));

        synth.addSound (new SineSound());
    }

    const juce::String getName() const override             { return "Ikaros Test Synth"; }
    bool acceptsMidi() const override                        { return true; }
    bool producesMidi() const override                       { return false; }
    double getTailLengthSeconds() const override             { return 0.5; }
    int getNumPrograms() override                            { return 1; }
    int getCurrentProgram() override                         { return 0; }
    void setCurrentProgram (int) override                    {}
    const juce::String getProgramName (int) override         { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    void prepareToPlay (double rate, int) override           { synth.setCurrentPlaybackSampleRate (rate); }
    void releaseResources() override                         {}

    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override
    {
        buffer.clear();
        levelValue = level->get();
        synth.renderNextBlock (buffer, midi, 0, buffer.getNumSamples());
    }

    bool hasEditor() const override                          { return true; }
    juce::AudioProcessorEditor* createEditor() override      { return new juce::GenericAudioProcessorEditor (*this); }

    void getStateInformation (juce::MemoryBlock& data) override
    {
        juce::MemoryOutputStream (data, true).writeFloat (level->get());
    }

    void setStateInformation (const void* data, int size) override
    {
        juce::MemoryInputStream in (data, (size_t) size, false);
        *level = in.readFloat();
    }

private:
    juce::Synthesiser synth;
    juce::AudioParameterFloat* level = nullptr;
    std::atomic<float> levelValue { 0.8f };
};

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new TestSynth();
}
