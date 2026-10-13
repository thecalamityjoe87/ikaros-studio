#pragma once

#include "engine/AudioEngine.h"
#include "ui/widgets/IconButton.h"

namespace ikaros
{

//==============================================================================
/**
    One page of the Preferences window. A page is a column of rows, each a
    label with its control (and a line of explanation under it), laid out
    top to bottom; a page taller than the window scrolls.

    Subclasses make their controls as members, add them with addRow() and
    friends in their constructor, and write the setting in each control's
    callback (settings apply the moment they change).
*/
class PreferencesPage  : public juce::Component
{
public:
    PreferencesPage();
    ~PreferencesPage() override;

    void resized() override;

protected:
    /** A heading that starts a group of rows. */
    void addHeading (const juce::String& text);

    /** "Label   [control]", with an optional line of explanation under the
        control. A control width of 0 fills the row. A control taller than a
        normal row (give it its height first) makes the row taller. */
    void addRow (const juce::String& label, juce::Component& control, int controlWidth = 240,
                 const juce::String& note = {});

    /** A paragraph of explanation. */
    void addNote (const juce::String& text);

    /** A control as wide as the page, with its height (which it may change
        later: call layoutRows() then). */
    void addWide (juce::Component& control);

    /** Lays the rows out again, e.g. after a control changed its height. */
    void layoutRows();

    /** Gives a note's text a new value (e.g. "Restart needed") and lays out again. */
    void setNote (juce::Component& control, const juce::String& note);

    /** A small button with text, in the style the dialogs use. */
    static void styleButton (IconButton&);

    /** Fills a drop-down with choices (item IDs are index + 1) and selects one. */
    static void fillChoices (juce::ComboBox&, std::initializer_list<const char*> choices, int selectedIndex);

    static constexpr int labelWidth = 190, rowHeight = 28;

private:
    struct Row
    {
        enum class Kind { heading, labelled, note, wide };

        Kind kind = Kind::labelled;
        juce::String text, note;       // the label (or the heading, or the paragraph), and the line under the control
        juce::Component* control = nullptr;
        int controlWidth = 0;
        juce::Rectangle<int> textArea, noteArea;   // set by layoutRows()
    };

    class Content;

    std::vector<Row> rows;
    std::unique_ptr<Content> content;
    juce::Viewport viewport;
    bool laying = false;   // inside layoutRows()

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PreferencesPage)
};

} // namespace ikaros
