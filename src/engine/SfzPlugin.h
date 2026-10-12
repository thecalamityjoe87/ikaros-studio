#pragma once

#include <tracktion_engine/tracktion_engine.h>
#include <array>
#include <atomic>

// sfizz's synth class, declared here so this header doesn't pull in all of sfizz.
namespace sfz { class Sfizz; }

namespace ikaros
{

//==============================================================================
/**
    An instrument that plays SFZ files through sfizz (velocity layers,
    round-robins, and big sample sets streamed from disk), as a Tracktion
    plugin. It sits next to Tracktion's own SamplerPlugin, which still plays
    simple instruments described by key zones.

    The SFZ file to play is a property of the plugin's state, so it's saved
    with the session and undo can change it back. So are the values of the
    MIDI controllers the Sound tab's "cc" knobs set (see ccProperty): they're
    sent to sfizz whenever they change, and again to each newly loaded synth.

    Loading happens on a background thread so neither the UI nor the audio
    stops for it. sfizz can't load while it's playing, so a new sound is
    loaded into a fresh sfizz synth and swapped in, between two audio
    blocks, once it's ready. Until then the previous sound keeps playing (or
    silence, the first time). The synth being replaced is deleted on the
    loader thread, never on the audio thread.
*/
class SfzPlugin  : public tracktion::Plugin
{
public:
    explicit SfzPlugin (tracktion::PluginCreationInfo);
    ~SfzPlugin() override;

    static const char* xmlTypeName;               // "ikarosSfz", the type stored in sessions
    static const juce::Identifier sfzFileProp;    // the state property holding the .sfz file's path

    /** The state property holding a MIDI controller's value (0..1), e.g.
        "cc72". A controller without one is left at the SFZ file's default. */
    static juce::Identifier ccProperty (int controllerNumber);

    /** The .sfz file this instrument plays. */
    juce::File getSfzFile() const;

    /** True once the current file has finished loading (whether or not it
        loaded successfully). */
    bool isLoaded() const noexcept         { return loadedRequest.load() == requestCounter.load(); }

    /** How many sample regions the loaded file has (0 if it failed to load). */
    int getNumRegions() const noexcept     { return numRegions.load(); }

    //==============================================================================
    // tracktion::Plugin
    juce::String getName() const override                   { return "SFZ Instrument"; }
    juce::String getPluginType() override                   { return xmlTypeName; }
    juce::String getShortName (int) override                { return "SFZ"; }
    juce::String getSelectableDescription() override        { return getName(); }
    bool isSynth() override                                 { return true; }
    bool takesMidiInput() override                          { return true; }
    bool takesAudioInput() override                         { return false; }
    bool producesAudioWhenNoAudioInput() override           { return true; }
    int getNumOutputChannelsGivenInputs (int) override      { return 2; }
    BusLayout getBusses() const override                    { return BusLayout::singleStereoInOut(); }

    void initialise (const tracktion::PluginInitialisationInfo&) override;
    void deinitialise() override;
    void reset() override;
    void applyToBuffer (const tracktion::PluginRenderContext&) override;
    void restorePluginStateFromValueTree (const juce::ValueTree&) override;
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;

private:
    class Loader;
    std::unique_ptr<Loader> loader;

    // The synth the audio thread plays. Only the audio thread changes it
    // (and initialise/deinitialise, which Tracktion never runs while this
    // plugin is processing).
    sfz::Sfizz* active = nullptr;

    // Hand-overs between the loader thread and the audio thread, without
    // locks: the loader puts a newly loaded synth in `incoming`; the audio
    // thread swaps it in and puts the old one in `outgoing` for the loader
    // to delete.
    std::atomic<sfz::Sfizz*> incoming { nullptr }, outgoing { nullptr };

    // Each load request gets a number; when the loader finishes one it
    // records it, so isLoaded() knows whether the latest has finished.
    std::atomic<int> requestCounter { 0 }, loadedRequest { 0 };
    std::atomic<int> numRegions { 0 };

    // The audio settings new synths are made for.
    std::atomic<double> currentSampleRate { 44100.0 };
    static constexpr int maxBlockSize = 4096;     // longer blocks are rendered in pieces

    juce::AudioBuffer<float> scratch;             // sfizz renders here, then it's added to the track

    // The controller values from our state (-1 = not set), for the audio
    // thread, and a flag that says they've changed since it last sent them.
    std::array<std::atomic<float>, 128> ccValues;
    std::atomic<bool> ccValuesChanged { false };

    void requestLoad();
    bool takeIncomingSynth();     // true if a new synth was swapped in
    void readCcValues();
    void sendCcValues();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SfzPlugin)
};

} // namespace ikaros
