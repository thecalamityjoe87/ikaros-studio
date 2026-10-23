#pragma once

#include "ui/state/Selection.h"
#include "ui/widgets/IconButton.h"

namespace ikaros
{

//==============================================================================
/**
    The "New Track" chooser, like GarageBand's: pick a Software Instrument,
    Audio (a microphone or anything else plugged in), Vocals (an audio track
    for singing), Guitar or Bass (an audio track with a pedalboard),
    Drummer (a virtual drummer, see DrummerEditor) or Sampler (an audio
    file played on the keys: Create asks for the file), then Create.

    Shown inside the main window over a dimmed background (see show()), so it
    looks the same on every OS. Deletes itself when closed.
*/
class NewTrackDialog  : public juce::Component
{
public:
    /** Opens the dialog over `parent`. The dialog owns itself and calls
        `onCreated` with the new track's ID if the user creates one. */
    static void show (juce::Component& parent, AudioEngine&, std::function<void (TrackId)> onCreated);

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    void mouseDown (const juce::MouseEvent&) override;   // click outside the panel = cancel

private:
    class Card;

    NewTrackDialog (AudioEngine&, std::function<void (TrackId)>);

    AudioEngine& engine;
    std::function<void (TrackId)> onCreated;

    // The track types, in the cards' order (left to right, then the next row).
    enum class Kind { instrument, audio, vocals, guitar, drummer, sampler };
    static constexpr int numKinds = 6, columns = 3;

    std::unique_ptr<Card> instrumentCard, audioCard, vocalsCard, guitarCard, drummerCard, samplerCard;
    std::unique_ptr<juce::FileChooser> soundChooser;   // Sampler: the sound to play
    juce::ComboBox inputBox;
    juce::ComboBox playsBox;   // Guitar or Bass: which amp voice the track starts with
    juce::ToggleButton monitorToggle { "Hear the input while recording" };
    IconButton cancelButton { "Cancel", std::nullopt, "Cancel" };
    IconButton createButton { "Create", std::nullopt, "Create" };

    Kind chosen = Kind::instrument;
    std::vector<AudioInput> inputs;
    juce::Rectangle<int> panel, audioOptionsArea, playsLabelArea;

    void choose (Kind);
    Card& cardFor (Kind) const;
    bool recordsInput() const noexcept     { return chosen == Kind::audio || chosen == Kind::vocals || chosen == Kind::guitar; }
    void create();
    void chooseSamplerSound();
    void finish (TrackId);   // closes, then tells onCreated about a new track
    void close();

    static constexpr int panelWidth = 748, panelHeight = 606;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NewTrackDialog)
};

} // namespace ikaros
