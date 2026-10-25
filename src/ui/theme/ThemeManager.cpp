#include "ui/theme/ThemeManager.h"
#include "ui/theme/IkarosLookAndFeel.h"
#include "ui/theme/Theme.h"

namespace ikaros
{

namespace
{
    /** A TextEditor keeps the colour each piece of its text was typed in,
        and the colour of its "Search..." placeholder, so after a theme change
        they're given the new colours. Walks the component and its children. */
    void recolourTextEditors (juce::Component& c)
    {
        if (auto* editor = dynamic_cast<juce::TextEditor*> (&c))
        {
            editor->applyColourToAllText (editor->findColour (juce::TextEditor::textColourId), false);

            // Every placeholder in the app is in the secondary text colour.
            if (editor->getTextToShowWhenEmpty().isNotEmpty())
                editor->setTextToShowWhenEmpty (editor->getTextToShowWhenEmpty(), theme().textSecondary);
        }

        for (auto* child : c.getChildren())
            recolourTextEditors (*child);
    }
}

ThemeManager::ThemeManager (Preferences& p, IkarosLookAndFeel& lf)
    : preferences (p), lookAndFeel (lf)
{
    preferences.addListener (this);
    juce::Desktop::getInstance().addDarkModeSettingListener (this);
    update();
}

ThemeManager::~ThemeManager()
{
    juce::Desktop::getInstance().removeDarkModeSettingListener (this);
    preferences.removeListener (this);
}

void ThemeManager::preferenceChanged (const juce::String& key)
{
    if (key.isEmpty() || key == settings::themeChoice.key)   // empty: every setting was reset
        update();
}

void ThemeManager::darkModeSettingChanged()   { update(); }

void ThemeManager::update()
{
    const int choice = preferences.get (settings::themeChoice);
    const bool matchSystem = choice != settings::ThemeChoice::light && choice != settings::ThemeChoice::dark;
    const bool dark = matchSystem ? systemAppearance.prefersDark() : choice == settings::ThemeChoice::dark;

    const auto& wanted = dark ? darkPalette() : lightPalette();

    if (&wanted == &theme())
        return;

    setActivePalette (wanted);

    // JUCE's own widgets read their colours from the LookAndFeel's table.
    lookAndFeel.applyThemeColours();

    // Then every window: the main one, plug-in windows, open menus. Our
    // components read theme() when they paint, so a repaint is all they
    // need; sendLookAndFeelChange() repaints a component and all its
    // children, and calls lookAndFeelChanged() on each, which the few that
    // keep colours of their own (e.g. the About dialog's text) use to
    // refresh them.
    auto& desktop = juce::Desktop::getInstance();

    for (int i = 0; i < desktop.getNumComponents(); ++i)
    {
        auto* window = desktop.getComponent (i);

        // A window's background colour is given when it's made.
        if (auto* resizable = dynamic_cast<juce::ResizableWindow*> (window))
            resizable->setBackgroundColour (theme().windowBg);

        recolourTextEditors (*window);   // before sendLookAndFeelChange, so the About dialog's refill wins
        window->sendLookAndFeelChange();
    }
}

} // namespace ikaros
