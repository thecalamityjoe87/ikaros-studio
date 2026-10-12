#pragma once

#include "engine/AudioEngine.h"
#include "ui/widgets/IconButton.h"

namespace ikaros
{

//==============================================================================
/**
    "More Sounds": the sound packs the user can download, like GarageBand's
    Sound Library. Each pack shows its name, size and description, and a
    button for what can be done with it: Download, Update, Remove, or Cancel
    while it downloads. Packs that come with the app can't be removed.

    It reads everything from the engine's PackStore, and redraws whenever the
    store says something changed (download progress included). Without the
    internet it says so and still lists the installed packs.

    Shown inside the main window over a dimmed background, like the New
    Track dialog. Deletes itself when closed.
*/
class MoreSoundsDialog  : public juce::Component,
                          private juce::ChangeListener
{
public:
    /** Opens the dialog over `parent`. With a pack id, that pack starts
        downloading straight away (the Sound tab's Download button). */
    static void show (juce::Component& parent, AudioEngine&, const juce::String& downloadPackId = {});

    ~MoreSoundsDialog() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    void mouseDown (const juce::MouseEvent&) override;   // click outside the panel = close

private:
    class PackRow;

    MoreSoundsDialog (AudioEngine&, const juce::String& downloadPackId);

    AudioEngine& engine;
    juce::String pendingDownload;      // the pack to download once the index has arrived
    bool requestedPackUnavailable = false;   // ...and it turned out not to be in the index

    juce::Viewport listViewport;
    juce::Component listHolder;
    juce::OwnedArray<PackRow> rows;
    IconButton retryButton { "Try again", std::nullopt, "Try Again" };
    IconButton doneButton { "Done", std::nullopt, "Done" };
    juce::Rectangle<int> panel, statusArea;
    juce::String statusText;

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void refresh();
    void confirmRemove (const StorePack&);
    void close();

    static constexpr int panelWidth = 680, panelHeight = 560, rowHeight = 92;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MoreSoundsDialog)
};

} // namespace ikaros
