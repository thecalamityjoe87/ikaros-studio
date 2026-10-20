#pragma once

#include "ui/state/Selection.h"
#include "ui/widgets/IconButton.h"

namespace ikaros
{

//==============================================================================
/**
    The "New Track" chooser, like GarageBand's: pick a Software Instrument, a
    Real Instrument (a microphone or anything else plugged in) or Guitar or
    Bass (a Real Instrument with a pedalboard), then Create.

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

    // The track types, in the cards' order (left to right).
    enum class Kind { instrument, audio, guitar };

    std::unique_ptr<Card> instrumentCard, audioCard, guitarCard;
    juce::ComboBox inputBox;
    juce::ToggleButton monitorToggle { "Hear the input while recording" };
    IconButton cancelButton { "Cancel", std::nullopt, "Cancel" };
    IconButton createButton { "Create", std::nullopt, "Create" };

    Kind chosen = Kind::instrument;
    std::vector<AudioInput> inputs;
    juce::Rectangle<int> panel, audioOptionsArea;

    void choose (Kind);
    Card& cardFor (Kind) const;
    bool recordsInput() const noexcept     { return chosen != Kind::instrument; }   // audio and guitar tracks
    void create();
    void close();

    static constexpr int panelWidth = 800, panelHeight = 400;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NewTrackDialog)
};

} // namespace ikaros
