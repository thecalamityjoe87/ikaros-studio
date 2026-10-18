#pragma once

#include "engine/AudioEngine.h"
#include "ui/widgets/IconButton.h"

namespace ikaros
{

//==============================================================================
/**
    "Manage Sounds": the sound packs on this computer and the ones the user
    can download, like GarageBand's Sound Library. The library lists a
    category's packs to download too; this window is where they're all
    managed. Two sections:

      Installed               with the space they take, Update and Remove
      Available to download   collections (the starter sounds) first, with
                              "Download All", then single packs grouped by
                              library category (Keyboards, Drums, Bass...)

    Chips along the top (All, Keys, Drums...) narrow both sections to one
    category, so a bass pack is one click away among dozens of packs.

    Each pack shows its name, size and description, and a button for what
    can be done with it: Download, Update, Remove, or Cancel while it
    downloads. Packs that come with the app can't be removed.

    It reads everything from the engine's PackStore, and redraws whenever the
    store says something changed (download progress included). Without the
    internet it says so and still lists the installed packs.

    On first run it opens in its welcome form instead (showWelcome), offering
    the starter sounds.

    Shown inside the main window over a dimmed background, like the New
    Track dialog. Deletes itself when closed.
*/
class ManageSoundsDialog  : public juce::Component,
                          private juce::ChangeListener
{
public:
    /** Opens the dialog over `parent`. With a pack id, that pack starts
        downloading straight away (the Sound tab's Download button). */
    static void show (juce::Component& parent, AudioEngine&, const juce::String& downloadPackId = {});

    /** The first-run welcome: offers the starter sounds, with "Not Now".
        Calls onClosed when it closes. */
    static void showWelcome (juce::Component& parent, AudioEngine&, std::function<void()> onClosed);

    /** True until the user has answered the first-run offer (downloaded the
        starter sounds or said Not Now). */
    static bool shouldOfferStarterSounds (AudioEngine&);

    ~ManageSoundsDialog() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    void mouseDown (const juce::MouseEvent&) override;   // click outside the panel = close

private:
    class Row;

    ManageSoundsDialog (AudioEngine&, bool welcome, const juce::String& downloadPackId, std::function<void()> onClosed);

    AudioEngine& engine;
    const bool welcome;                // the first-run form
    juce::String pendingDownload;      // the pack to download once the index has arrived
    bool requestedPackUnavailable = false;   // ...and it turned out not to be in the index
    bool starterRequested = false;     // welcome: the user chose to download the starter sounds
    std::function<void()> onClosed;

    juce::Viewport listViewport;
    juce::Component listHolder;
    juce::OwnedArray<Row> rows;
    IconButton retryButton { "Try again", std::nullopt, "Try Again" };
    IconButton notNowButton { "Not now", std::nullopt, "Not Now" };
    IconButton doneButton { "Done", std::nullopt, "Done" };
    juce::String doneText { "Done" };  // what doneButton says (for its width)
    juce::Rectangle<int> panel, statusArea;

    // The category chips: one per group that has packs, plus "All".
    juce::OwnedArray<IconButton> filterChips;
    juce::StringArray filterGroups, filterLabels;   // each chip's group (empty for All) and label, in chip order
    juce::String filter;               // the chosen group: a library category, "Other", or empty for All
    juce::String statusText;

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void refresh();
    void setFilter (const juce::String& group);
    void confirmRemove (const juce::String& packId, const juce::String& name);
    void markStarterOffered();
    void close();

    static constexpr int panelWidth = 680, panelHeight = 600;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ManageSoundsDialog)
};

} // namespace ikaros
