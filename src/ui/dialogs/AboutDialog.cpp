#include "ui/dialogs/AboutDialog.h"
#include "engine/SoundLibrary.h"
#include "ui/theme/Fonts.h"
#include "ui/theme/Tokens.h"

namespace ikaros
{

using namespace tokens;

//==============================================================================
void AboutDialog::show (juce::Component& parent, AudioEngine& engine)
{
    // Owned by its parent from here on; it deletes itself in close().
    auto* dialog = new AboutDialog (engine);
    parent.addAndMakeVisible (dialog);
    dialog->setBounds (parent.getLocalBounds());
    dialog->grabKeyboardFocus();
}

AboutDialog::AboutDialog (AudioEngine& engine)
{
    setTitle ("About Ikaros Studio");
    setWantsKeyboardFocus (true);
    setAlwaysOnTop (true);

    credits.setMultiLine (true);
    credits.setReadOnly (true);
    credits.setCaretVisible (false);
    credits.setScrollbarsShown (true);
    credits.setColour (juce::TextEditor::backgroundColourId, colour::wellBg);
    credits.setColour (juce::TextEditor::outlineColourId, colour::borderSoft);
    credits.setColour (juce::TextEditor::focusedOutlineColourId, colour::borderSoft);
    credits.setColour (juce::TextEditor::textColourId, colour::textSecondary);
    credits.setBorder ({ 12, 14, 12, 14 });
    credits.setTitle ("Credits");

    // TextEditor keeps the colour and font set when text is inserted, so
    // headings and body text are added in turn.
    auto heading = [this] (const juce::String& text)
    {
        credits.setFont (fonts::uiBold (14.0f));
        credits.setColour (juce::TextEditor::textColourId, colour::text);
        credits.insertTextAtCaret (text + "\n");
    };

    auto body = [this] (const juce::String& text)
    {
        credits.setFont (fonts::ui (13.0f));
        credits.setColour (juce::TextEditor::textColourId, colour::textSecondary);
        credits.insertTextAtCaret (text + "\n");
    };

    heading ("Sounds");

    for (auto& pack : engine.getSounds().getPacks())
    {
        juce::String text = pack.name + (pack.bundled ? " (comes with the app)" : juce::String()) + "\n";

        // What, who, license; then where it's from.
        for (auto& c : pack.credits)
        {
            juce::StringArray parts { c.what, c.who, c.license };
            parts.removeEmptyStrings();
            text << "    " << parts.joinIntoString (juce::String::fromUTF8 (" \xc2\xb7 ")) << "\n"
                 << (c.url.isNotEmpty() ? "    " + c.url + "\n" : juce::String());
        }

        if (pack.credits.empty() && pack.license.isNotEmpty())
            text << "    " << pack.license << "\n";

        body (text);
    }

    heading ("Software");
    body ("Tracktion Engine and JUCE (GPLv3)\n    https://github.com/Tracktion/tracktion_engine\n\n"
          "sfizz, the SFZ sample player (BSD 2-Clause), with the libraries it includes\n    https://sfz.tools/sfizz\n\n"
          "miniz (MIT)\n    https://github.com/richgel999/miniz\n\n"
          "Figtree and JetBrains Mono fonts (SIL Open Font License 1.1)\n\n"
          "Some icons from Lucide (ISC)\n    https://lucide.dev\n\n"
          "The full license texts are in the CREDITS file that comes with Ikaros Studio.");

    credits.setCaretPosition (0);   // show the top
    addAndMakeVisible (credits);

    doneButton.setColours (IconButton::accentColours());
    doneButton.onClick = [this] { close(); };
    addAndMakeVisible (doneButton);
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

    auto r = panel.reduced (28, 24);
    g.setColour (colour::text);
    g.setFont (fonts::uiBold (18.0f));
    g.drawText ("Ikaros Studio", r.removeFromTop (26), juce::Justification::centredLeft);

    g.setColour (colour::textSecondary);
    g.setFont (fonts::ui (13.0f));
    g.drawText ("Version " + juce::JUCEApplication::getInstance()->getApplicationVersion()
                    + juce::String::fromUTF8 (" \xc2\xb7 An easy-first music studio"),
                r.removeFromTop (22), juce::Justification::centredLeft);
}

void AboutDialog::resized()
{
    panel = getLocalBounds().withSizeKeepingCentre (juce::jmin (panelWidth, getWidth() - 32),
                                                    juce::jmin (panelHeight, getHeight() - 32));
    auto r = panel.reduced (28, 24);
    r.removeFromTop (26 + 22 + 16);

    doneButton.setBounds (r.removeFromBottom (36).removeFromRight (96));
    r.removeFromBottom (16);
    credits.setBounds (r);
}

} // namespace ikaros
