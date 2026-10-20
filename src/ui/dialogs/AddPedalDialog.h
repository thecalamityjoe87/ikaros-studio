#pragma once

#include "engine/AudioEngine.h"
#include "ui/widgets/IconButton.h"

namespace ikaros
{

//==============================================================================
/**
    "Add a pedal": every pedal, drawn as it looks on the board (at 70%),
    one row per group as in PedalStyles, scrolling both ways. Click one
    (or move to it with the arrow keys and press Return or Space) to add it.

    Shown inside the main window over a dimmed background, like the New
    Track dialog (see show()). Deletes itself when closed.
*/
class AddPedalDialog  : public juce::Component
{
public:
    /** Opens the dialog over `parent`. The dialog owns itself and calls
        `onChosen` with the chosen pedal's plugin type, if one is chosen. */
    static void show (juce::Component& parent, AudioEngine&, std::function<void (const juce::String& type)> onChosen);

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    void mouseDown (const juce::MouseEvent&) override;   // click outside the panel = cancel

private:
    class Tile;

    AddPedalDialog (AudioEngine&, std::function<void (const juce::String&)>);
    ~AddPedalDialog() override;

    std::function<void (const juce::String&)> onChosen;

    // The scrolling area inside the panel holds the section headings
    // (painted by `content`) and the tiles.
    struct Content  : public juce::Component
    {
        struct Heading { juce::String name; int count = 0; juce::Rectangle<int> area; };
        std::vector<Heading> headings;
        void paint (juce::Graphics&) override;
    };

    juce::Viewport viewport;
    Content content;
    juce::OwnedArray<Tile> tiles;   // in PedalStyles' order; OwnedArray deletes them with the dialog
    IconButton cancelButton { "Cancel", std::nullopt, "Cancel" };
    juce::Rectangle<int> panel;

    void choose (const juce::String& type);
    void close();
    juce::Rectangle<int> layOutTiles();   // positions the tiles and headings; returns the size they need
    void moveFocus (int dx, int dy);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AddPedalDialog)
};

} // namespace ikaros
