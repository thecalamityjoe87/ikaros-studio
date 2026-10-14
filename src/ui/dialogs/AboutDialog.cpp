#include "ui/dialogs/AboutDialog.h"
#include "BinaryData.h"
#include "engine/SoundLibrary.h"
#include "ui/theme/Fonts.h"
#include "ui/theme/Tokens.h"

namespace ikaros
{

using namespace tokens;

namespace
{
    // The tabs are a segmented control, like the bottom panel's.
    constexpr int segmentInset = 3, tabHeight = 36;
    constexpr float segmentRadius = 7.0f, segmentOutlineRadius = 10.0f;
    constexpr int tabWidths[] = { 90, 90, 140, 150 };

    const juce::String bullet = juce::String::fromUTF8 ("\xe2\x80\xa2  ");
}

//==============================================================================
void AboutDialog::show (juce::Component& parent, AudioEngine& engine)
{
    // Owned by its parent from here on; it deletes itself in close().
    auto* dialog = new AboutDialog (engine);
    parent.addAndMakeVisible (dialog);
    dialog->setBounds (parent.getLocalBounds());
    dialog->grabKeyboardFocus();
}

AboutDialog::AboutDialog (AudioEngine& e)
    : engine (e)
{
    setTitle ("About Ikaros Studio");
    setWantsKeyboardFocus (true);
    setAlwaysOnTop (true);

    icon = juce::ImageFileFormat::loadFrom (BinaryData::io_github_thecalamityjoe87_IkarosStudio_png,
                                            (size_t) BinaryData::io_github_thecalamityjoe87_IkarosStudio_pngSize);

    text.setMultiLine (true);
    text.setReadOnly (true);
    text.setCaretVisible (false);
    text.setScrollbarsShown (true);
    text.setColour (juce::TextEditor::backgroundColourId, colour::wellBg);
    text.setColour (juce::TextEditor::outlineColourId, colour::borderSoft);
    text.setColour (juce::TextEditor::focusedOutlineColourId, colour::borderSoft);
    text.setColour (juce::TextEditor::textColourId, colour::textSecondary);
    text.setBorder ({ 12, 14, 12, 14 });
    addChildComponent (text);

    // The chosen tab is a rounded pill inside the shared background drawn in paint().
    const auto tabColours = IconButton::Colours { juce::Colours::transparentBlack, colour::text,
                                                  colour::accent, colour::textOnAccent };
    int index = 0;

    for (auto* b : { &aboutTab, &creditsTab, &releaseNotesTab, &acknowledgementsTab })
    {
        b->setColours (tabColours);
        b->setCornerRadius (segmentRadius);
        b->setFont (fonts::uiBold (13.0f));
        b->setRadioGroupId (1);   // radio group: turning one on turns the others off
        b->onClick = [this, index] { showPage ((Page) index); };
        addAndMakeVisible (b);
        ++index;
    }

    doneButton.setColours (IconButton::accentColours());
    doneButton.onClick = [this] { close(); };
    addAndMakeVisible (doneButton);

    showPage (Page::about);
}

void AboutDialog::showPage (Page newPage)
{
    page = newPage;

    aboutTab.setToggleState (page == Page::about, juce::dontSendNotification);
    creditsTab.setToggleState (page == Page::credits, juce::dontSendNotification);
    releaseNotesTab.setToggleState (page == Page::releaseNotes, juce::dontSendNotification);
    acknowledgementsTab.setToggleState (page == Page::acknowledgements, juce::dontSendNotification);

    text.setVisible (page != Page::about);
    text.setTitle (page == Page::credits ? "Credits" : page == Page::releaseNotes ? "Release Notes" : "Acknowledgements");
    text.clear();

    if (page == Page::credits)               fillCredits();
    else if (page == Page::releaseNotes)     fillReleaseNotes();
    else if (page == Page::acknowledgements) fillAcknowledgements();

    text.setCaretPosition (0);   // show the top
    repaint();
}

//==============================================================================
// TextEditor keeps the colour and font set when text is inserted, so
// headings and body text are added in turn.
namespace
{
    void addHeading (juce::TextEditor& t, const juce::String& s)
    {
        t.setFont (fonts::uiBold (14.0f));
        t.setColour (juce::TextEditor::textColourId, colour::text);
        t.insertTextAtCaret (s + "\n");
    }

    void addBody (juce::TextEditor& t, const juce::String& s)
    {
        t.setFont (fonts::ui (13.0f));
        t.setColour (juce::TextEditor::textColourId, colour::textSecondary);
        t.insertTextAtCaret (s + "\n");
    }
}

void AboutDialog::fillCredits()
{
    addHeading (text, "Made by");
    addBody (text, "Isaac Joseph\n    calamityjoe87@gmail.com\n    https://github.com/thecalamityjoe87\n");

    addHeading (text, "Ikaros Studio");
    addBody (text, "Designed and written by Isaac Joseph: the app, its look and feel, and the way it works "
                   "for people making music for the first time.\n"
                   "    https://github.com/thecalamityjoe87/ikaros-studio\n");

    addHeading (text, "Ikaros Basics sounds");
    addBody (text, "The instruments and loops that come with the app were synthesized for Ikaros Studio "
                   "by Isaac Joseph (CC0, free for anyone to use).\n");

    addBody (text, "Everyone else whose work is part of Ikaros Studio is under Acknowledgements.");
}

void AboutDialog::fillReleaseNotes()
{
    addHeading (text, "0.0.1 (alpha): first release");
    addBody (text, "The first release of Ikaros Studio. It's alpha software: ready to try, but expect rough "
                   "edges, and keep a copy of any session you care about.\n");

    addHeading (text, "What's in it");
    addBody (text, bullet + "Sessions saved as one .iksession file that holds every recording and loop it uses\n"
                   + bullet + "Instrument tracks with a built-in synth and sampled instruments, played from the on-screen "
                              "keyboard or a MIDI keyboard\n"
                   + bullet + "Audio tracks: record from an input, and keep several takes of the same part\n"
                   + bullet + "Regions on a timeline, a piano roll editor, loops and automation\n"
                   + bullet + "Sound knobs for every instrument, a per-track EQ, effects and a mixer with sends\n"
                   + bullet + "Plug-ins: VST3 everywhere, LV2 on Linux and Windows, Audio Units on macOS\n"
                   + bullet + "Export your song as audio, including MP3\n"
                   + bullet + "Autosave, and recovery of your work after a crash\n"
                   + bullet + "Ikaros Basics, a sound pack that comes with the app; packs you add to your Sounds "
                              "folder show up in the library\n");

    addHeading (text, "Not yet");
    addBody (text, bullet + "Downloadable sound packs: More Sounds has nothing to offer until they're hosted");
}

void AboutDialog::fillAcknowledgements()
{
    addHeading (text, "Sounds");

    for (auto& pack : engine.getSounds().getPacks())
    {
        juce::String line = pack.name + (pack.bundled ? " (comes with the app)" : juce::String()) + "\n";

        // What, who, license; then where it's from.
        for (auto& c : pack.credits)
        {
            juce::StringArray parts { c.what, c.who, c.license };
            parts.removeEmptyStrings();
            line << "    " << parts.joinIntoString (juce::String::fromUTF8 (" \xc2\xb7 ")) << "\n"
                 << (c.url.isNotEmpty() ? "    " + c.url + "\n" : juce::String());
        }

        if (pack.credits.empty() && pack.license.isNotEmpty())
            line << "    " << pack.license << "\n";

        addBody (text, line);
    }

    addHeading (text, "Software");
    addBody (text, "Tracktion Engine and JUCE (GPLv3)\n    https://github.com/Tracktion/tracktion_engine\n\n"
                   "sfizz, the SFZ sample player (BSD 2-Clause), with the libraries it includes\n    https://sfz.tools/sfizz\n\n"
                   "miniz (MIT)\n    https://github.com/richgel999/miniz\n\n"
                   "LAME, the MP3 encoder (LGPL)\n    https://lame.sourceforge.io\n\n"
                   "Figtree and JetBrains Mono fonts (SIL Open Font License 1.1)\n\n"
                   "Some icons from Lucide (ISC)\n    https://lucide.dev\n\n"
                   "The full license texts are in the CREDITS file that comes with Ikaros Studio.");
}

void AboutDialog::close()
{
    // Remove ourselves on the next message-loop turn: we may be inside our
    // own button's callback right now.
    setVisible (false);
    juce::Component::unfocusAllComponents();

    juce::MessageManager::callAsync ([safeThis = SafePointer<AboutDialog> (this)]
    {
        if (safeThis != nullptr)
            delete safeThis.getComponent();
    });
}

bool AboutDialog::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey || key == juce::KeyPress::returnKey)
    {
        close();
        return true;
    }

    return false;
}

void AboutDialog::mouseDown (const juce::MouseEvent& e)
{
    if (! panel.contains (e.getPosition()))
        close();
}

void AboutDialog::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black.withAlpha (0.55f));

    juce::DropShadow (juce::Colours::black.withAlpha (0.6f), 30, { 0, 8 }).drawForRectangle (g, panel);
    g.setColour (colour::toolbarBg);
    g.fillRoundedRectangle (panel.toFloat(), 14.0f);
    g.setColour (colour::border);
    g.drawRoundedRectangle (panel.toFloat().reduced (0.5f), 14.0f, 1.0f);

    // The tabs' shared background and outline.
    g.setColour (colour::cardBg);
    g.fillRoundedRectangle (tabsArea.toFloat(), segmentOutlineRadius);
    g.setColour (colour::border);
    g.drawRoundedRectangle (tabsArea.toFloat().reduced (0.5f), segmentOutlineRadius, 1.0f);

    if (page != Page::about)
        return;

    // The About page: icon, name, version, what the app is.
    auto r = contentArea.reduced (40, 0);
    r.removeFromTop (8);

    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
    g.drawImage (icon, r.removeFromTop (112).toFloat(), juce::RectanglePlacement::centred);
    r.removeFromTop (10);

    g.setColour (colour::text);
    g.setFont (fonts::uiBold (26.0f));
    g.drawText ("Ikaros Studio", r.removeFromTop (34), juce::Justification::centred);

    g.setColour (colour::accent);
    g.setFont (fonts::uiSemiBold (13.0f));
    g.drawText ("Version " + juce::JUCEApplication::getInstance()->getApplicationVersion() + " (alpha)",
                r.removeFromTop (22), juce::Justification::centred);
    r.removeFromTop (14);

    g.setColour (colour::textSecondary);
    g.setFont (fonts::ui (14.0f));
    g.drawFittedText ("An easy-first music studio for Linux, Windows and macOS. Pick a sound, play it on the "
                      "on-screen or a MIDI keyboard, and build a song from regions on a timeline. Record your "
                      "voice or instruments, shape each track with simple sound knobs, effects and a mixer, "
                      "then export the result. Advanced controls stay tucked away until you ask for them.",
                      r.removeFromTop (110), juce::Justification::centredTop, 6, 1.0f);
    r.removeFromTop (8);

    g.setFont (fonts::ui (12.5f));
    g.drawFittedText ("This is the first alpha release: expect rough edges, and keep a copy of any session you care about.",
                      r.removeFromTop (40), juce::Justification::centredTop, 2, 1.0f);
}

void AboutDialog::resized()
{
    panel = getLocalBounds().withSizeKeepingCentre (juce::jmin (panelWidth, getWidth() - 32),
                                                    juce::jmin (panelHeight, getHeight() - 32));
    auto r = panel.reduced (28, 24);

    int tabsWidth = 2 * segmentInset;

    for (auto w : tabWidths)
        tabsWidth += w;

    tabsArea = r.removeFromTop (tabHeight).withSizeKeepingCentre (tabsWidth, tabHeight);
    auto t = tabsArea.reduced (segmentInset);
    int index = 0;

    for (auto* b : { &aboutTab, &creditsTab, &releaseNotesTab, &acknowledgementsTab })
        b->setBounds (t.removeFromLeft (tabWidths[index++]));

    r.removeFromTop (16);
    doneButton.setBounds (r.removeFromBottom (36).removeFromRight (96));
    r.removeFromBottom (16);

    contentArea = r;
    text.setBounds (contentArea);
}

} // namespace ikaros
