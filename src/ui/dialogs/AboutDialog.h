#pragma once

#include "engine/AudioEngine.h"
#include "ui/dialogs/AboutTechnology.h"
#include "ui/widgets/IconButton.h"

namespace ikaros
{

//==============================================================================
/**
    Help > About Ikaros Studio. Five pages behind tabs:

        About             the icon, the version and what the app is
        Credits           who made it
        Release Notes     what's in this version
        Built With        the projects it's made from, with their logos
        Acknowledgements  who made the sounds and the software it's built on

    The About page's View License button shows the GPL's full text in place
    of the page, with the About tab still chosen.

    The sound credits come from each installed pack's pack.json, so downloaded
    packs are credited as soon as they're installed (several of them require
    it: CC BY).

    Shown inside the main window over a dimmed background, like the other
    dialogs. Deletes itself when closed.
*/
class AboutDialog  : public juce::Component
{
public:
    static void show (juce::Component& parent, AudioEngine&);

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    void mouseDown (const juce::MouseEvent&) override;   // click outside the panel = close
    void lookAndFeelChanged() override;                  // the theme changed: recolour the text

private:
    enum class Page { about, credits, releaseNotes, builtWith, acknowledgements, license };

    explicit AboutDialog (AudioEngine&);

    void applyTextColours();
    void showPage (Page);
    void fillCredits();
    void fillReleaseNotes();
    void fillAcknowledgements();
    void fillLicense();
    void close();

    // The text pages share one read-only editor; the About page is painted.
    juce::TextEditor text;
    juce::Viewport builtWithView;
    AboutTechnology builtWith;
    juce::Image icon;

    IconButton aboutTab            { "About", std::nullopt, "About" };
    IconButton creditsTab          { "Credits", std::nullopt, "Credits" };
    IconButton releaseNotesTab     { "Release Notes", std::nullopt, "Release Notes" };
    IconButton builtWithTab        { "Built With", std::nullopt, "Built With" };
    IconButton acknowledgementsTab { "Acknowledgements", std::nullopt, "Acknowledgements" };
    IconButton licenseButton       { "View License", std::nullopt, "View License" };
    IconButton doneButton          { "Done", std::nullopt, "Done" };

    AudioEngine& engine;
    Page page = Page::about;
    juce::Rectangle<int> panel, tabsArea, contentArea;

    static constexpr int panelWidth = 680, panelHeight = 580;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AboutDialog)
};

} // namespace ikaros
