#include "ui/preferences/PreferencesDialog.h"
#include "ui/preferences/PreferencesPages.h"
#include "ui/theme/Fonts.h"
#include "ui/theme/Tokens.h"

namespace ikaros
{

using namespace tokens;

namespace
{
    const char* const pageNames[] { "General", "Audio & MIDI", "Recording", "Editing",
                                    "Sounds & Plug-ins", "Export", "Appearance", "Advanced" };
}

const char* PreferencesDialog::getName()
{
   #if JUCE_MAC
    return "Settings";
   #else
    return "Preferences";
   #endif
}

void PreferencesDialog::show (juce::Component& parent, AudioEngine& engine, juce::ApplicationCommandManager& commands,
                              Page startPage, std::function<void()> openSoundLibrary)
{
    // Already open: show the page asked for.
    for (auto* child : parent.getChildren())
    {
        if (auto* existing = dynamic_cast<PreferencesDialog*> (child))
        {
            existing->pageList.selectRow ((int) startPage);
            existing->toFront (true);
            return;
        }
    }

    // Owned by its parent from here on; it deletes itself in close().
    auto* dialog = new PreferencesDialog (engine, commands, std::move (openSoundLibrary));
    parent.addAndMakeVisible (dialog);
    dialog->setBounds (parent.getLocalBounds());
    dialog->pageList.selectRow ((int) startPage);
    dialog->grabKeyboardFocus();
}

PreferencesDialog::PreferencesDialog (AudioEngine& e, juce::ApplicationCommandManager& c, std::function<void()> openLibrary)
    : engine (e), commands (c), openSoundLibrary (std::move (openLibrary))
{
    setTitle (getName());
    setWantsKeyboardFocus (true);
    setAlwaysOnTop (true);

    pageList.setModel (this);
    pageList.setRowHeight (38);
    pageList.setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
    pageList.setColour (juce::ListBox::outlineColourId, juce::Colours::transparentBlack);
    pageList.setTitle ("Sections");
    addAndMakeVisible (pageList);

    doneButton.setColours (IconButton::primaryColours);
    doneButton.onClick = [this] { close(); };
    addAndMakeVisible (doneButton);
}

PreferencesDialog::~PreferencesDialog()
{
    pageList.setModel (nullptr);
}

//==============================================================================
void PreferencesDialog::showPage (int index)
{
    currentPage = index;
    page.reset();

    // Buttons that close this window (or replace the page) do it a moment
    // later, so the click that asked is finished first.
    auto later = [safeThis = SafePointer<PreferencesDialog> (this)] (std::function<void (PreferencesDialog&)> action)
    {
        juce::MessageManager::callAsync ([safeThis, action]
        {
            if (safeThis != nullptr)
                action (*safeThis);
        });
    };

    switch ((Page) index)
    {
        case Page::general:        page = std::make_unique<GeneralPage> (engine); break;
        case Page::audioMidi:      page = std::make_unique<AudioMidiPage> (engine); break;
        case Page::recording:      page = std::make_unique<RecordingPage> (engine); break;
        case Page::editing:        page = std::make_unique<EditingPage> (engine); break;
        case Page::exportSong:     page = std::make_unique<ExportPage> (engine); break;
        case Page::appearance:     page = std::make_unique<AppearancePage> (engine); break;

        case Page::soundsPlugins:
            page = std::make_unique<SoundsPluginsPage> (engine, [later]
            {
                later ([] (PreferencesDialog& d)
                {
                    auto open = d.openSoundLibrary;
                    d.close();

                    if (open)
                        open();
                });
            });
            break;

        case Page::advanced:
            page = std::make_unique<AdvancedPage> (engine, commands, [later]
            {
                later ([] (PreferencesDialog& d) { d.showPage (d.currentPage); });   // the controls follow the defaults
            });
            break;
    }

    addAndMakeVisible (*page);
    page->setBounds (pageArea);
    repaint();
}

void PreferencesDialog::close()
{
    // Remove ourselves from the parent and delete on the next message-loop
    // turn: we may be inside one of our own button callbacks right now.
    setVisible (false);
    juce::Component::unfocusAllComponents();

    juce::MessageManager::callAsync ([safeThis = SafePointer<PreferencesDialog> (this)]
    {
        if (safeThis != nullptr)
            delete safeThis.getComponent();
    });
}

bool PreferencesDialog::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey)
    {
        close();
        return true;
    }

    // Tab moves between controls; everything else is swallowed, so the
    // app's shortcuts don't reach the session behind the window.
    return key.getKeyCode() != juce::KeyPress::tabKey;
}

//==============================================================================
int PreferencesDialog::getNumRows()
{
    return juce::numElementsInArray (pageNames);
}

juce::String PreferencesDialog::getNameForRow (int row)
{
    return juce::isPositiveAndBelow (row, getNumRows()) ? pageNames[row] : "";
}

void PreferencesDialog::paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected)
{
    if (! juce::isPositiveAndBelow (row, getNumRows()))
        return;

    auto r = juce::Rectangle<float> (0.0f, 2.0f, (float) width, (float) height - 4.0f);

    if (selected)
    {
        g.setColour (theme().selectedRow);
        g.fillRoundedRectangle (r, size::radiusSmall);
        g.setColour (theme().highlight);
        g.fillRoundedRectangle (r.withWidth (3.0f).reduced (0.0f, 8.0f), 1.5f);
    }

    g.setColour (selected ? theme().text : theme().textMenu);
    g.setFont (selected ? fonts::uiSemiBold (textSize::lgPlus) : fonts::ui (textSize::lgPlus));
    g.drawText (pageNames[row], 16, 0, width - 24, height, juce::Justification::centredLeft);
}

void PreferencesDialog::selectedRowsChanged (int row)
{
    if (juce::isPositiveAndBelow (row, getNumRows()) && row != currentPage)
        showPage (row);
}

//==============================================================================
void PreferencesDialog::paint (juce::Graphics& g)
{
    // Dim the app behind the window.
    g.fillAll (theme().scrim);

    theme().dialogShadow.drawForRectangle (g, panel);
    g.setColour (theme().dialogBg);
    g.fillRoundedRectangle (panel.toFloat(), size::radiusPill);

    // The list of sections: darker, with the panel's rounded corners on its left.
    juce::Path sidebarShape;
    sidebarShape.addRoundedRectangle ((float) sidebar.getX(), (float) sidebar.getY(), (float) sidebar.getWidth(),
                                      (float) sidebar.getHeight(), 14.0f, 14.0f, true, false, true, false);
    g.setColour (theme().dialogSidebarBg);
    g.fillPath (sidebarShape);
    g.setColour (theme().border);
    g.fillRect (sidebar.getRight() - 1, sidebar.getY(), 1, sidebar.getHeight());

    g.setColour (theme().border);
    g.drawRoundedRectangle (panel.toFloat().reduced (0.5f), size::radiusPill, 1.0f);

    g.setColour (theme().text);
    g.setFont (fonts::uiBold (textSize::title));
    g.drawText (getName(), sidebar.getX() + 26, sidebar.getY() + 22, sidebar.getWidth() - 40, 30, juce::Justification::centredLeft);

    if (juce::isPositiveAndBelow (currentPage, getNumRows()))
    {
        g.setFont (fonts::uiBold (textSize::display));
        g.drawText (pageNames[currentPage], titleArea, juce::Justification::centredLeft);
    }
}

void PreferencesDialog::resized()
{
    const int width = juce::jmin (panelWidth, getWidth() - 40);
    const int height = juce::jmin (panelHeight, getHeight() - 40);
    panel = getLocalBounds().withSizeKeepingCentre (width, height);

    auto r = panel;
    sidebar = r.removeFromLeft (sidebarWidth);
    pageList.setBounds (sidebar.withTrimmedTop (68).reduced (10, 0).withTrimmedBottom (14));

    auto content = r.reduced (padding, 24);
    doneButton.setBounds (content.removeFromBottom (36).removeFromRight (110));
    content.removeFromBottom (12);
    titleArea = content.removeFromTop (30);
    content.removeFromTop (14);
    pageArea = content;

    if (page != nullptr)
        page->setBounds (pageArea);
}

} // namespace ikaros
