#include "ui/preferences/PreferencesPage.h"
#include "ui/theme/Fonts.h"
#include "ui/theme/Tokens.h"

namespace ikaros
{

using namespace tokens;

namespace
{
    constexpr int rowGap = 14, headingHeight = 24, noteGap = 4, rightMargin = 8;

    const juce::Font noteFont() { return fonts::ui (12.5f); }

    // How tall some text is when wrapped to a width.
    int measureHeight (const juce::String& text, const juce::Font& font, int width)
    {
        juce::AttributedString s;
        s.append (text, font);
        s.setWordWrap (juce::AttributedString::byWord);

        juce::TextLayout layout;
        layout.createLayout (s, (float) width);
        return (int) std::ceil (layout.getHeight());
    }
}

//==============================================================================
// What scrolls: the labels and notes are drawn here, and the controls live here.
class PreferencesPage::Content  : public juce::Component
{
public:
    explicit Content (PreferencesPage& p) : owner (p) {}

    void paint (juce::Graphics& g) override
    {
        for (auto& row : owner.rows)
        {
            switch (row.kind)
            {
                case Row::Kind::heading:
                    g.setColour (colour::text);
                    g.setFont (fonts::uiBold (15.0f));
                    g.drawText (row.text, row.textArea, juce::Justification::centredLeft);
                    g.setColour (colour::borderSoft);
                    g.fillRect (row.textArea.getX(), row.textArea.getBottom(), row.textArea.getWidth(), 1);
                    break;

                case Row::Kind::labelled:
                    g.setColour (colour::text);
                    g.setFont (fonts::ui (13.5f));
                    g.drawText (row.text, row.textArea, juce::Justification::centredLeft);
                    break;

                case Row::Kind::note:
                    g.setColour (colour::textSecondary);
                    g.setFont (noteFont());
                    g.drawFittedText (row.text, row.textArea, juce::Justification::topLeft, 20, 1.0f);
                    break;

                case Row::Kind::wide:
                    break;
            }

            if (row.note.isNotEmpty())
            {
                g.setColour (colour::textSecondary);
                g.setFont (noteFont());
                g.drawFittedText (row.note, row.noteArea, juce::Justification::topLeft, 20, 1.0f);
            }
        }
    }

    // A control changed its own size (the audio picker does when the device does).
    void childBoundsChanged (juce::Component*) override   { owner.layoutRows(); }

private:
    PreferencesPage& owner;
};

//==============================================================================
PreferencesPage::PreferencesPage()
    : content (std::make_unique<Content> (*this))
{
    viewport.setViewedComponent (content.get(), false);   // false: we own the content
    viewport.setScrollBarsShown (true, false);
    addAndMakeVisible (viewport);
}

PreferencesPage::~PreferencesPage() = default;

void PreferencesPage::addHeading (const juce::String& text)
{
    rows.push_back ({ .kind = Row::Kind::heading, .text = text });
}

void PreferencesPage::addRow (const juce::String& label, juce::Component& control, int controlWidth, const juce::String& note)
{
    content->addAndMakeVisible (control);
    rows.push_back ({ .kind = Row::Kind::labelled, .text = label, .note = note, .control = &control, .controlWidth = controlWidth });
}

void PreferencesPage::addNote (const juce::String& text)
{
    rows.push_back ({ .kind = Row::Kind::note, .text = text });
}

void PreferencesPage::addWide (juce::Component& control)
{
    content->addAndMakeVisible (control);
    rows.push_back ({ .kind = Row::Kind::wide, .control = &control });
}

void PreferencesPage::setNote (juce::Component& control, const juce::String& note)
{
    for (auto& row : rows)
        if (row.control == &control)
            row.note = note;

    layoutRows();
}

void PreferencesPage::styleButton (IconButton& b)
{
    auto colours = IconButton::raisedColours();
    colours.border = colour::border;
    b.setColours (colours);
    b.setFont (fonts::ui (13.0f));
}

void PreferencesPage::fillChoices (juce::ComboBox& box, std::initializer_list<const char*> choices, int selectedIndex)
{
    box.clear (juce::dontSendNotification);
    int id = 1;

    for (auto* choice : choices)
        box.addItem (choice, id++);

    box.setSelectedItemIndex (juce::jlimit (0, (int) choices.size() - 1, selectedIndex), juce::dontSendNotification);
}

//==============================================================================
void PreferencesPage::resized()
{
    viewport.setBounds (getLocalBounds());
    layoutRows();
}

void PreferencesPage::layoutRows()
{
    // Setting a control's bounds tells the content, which would lay out again.
    if (laying)
        return;

    const juce::ScopedValueSetter<bool> guard (laying, true);

    // The scroll bar's room is always kept, so showing it never changes the layout.
    const int width = juce::jmax (260, getWidth() - viewport.getScrollBarThickness() - rightMargin);
    const int controlX = labelWidth;
    int y = 0;
    bool first = true;

    for (auto& row : rows)
    {
        switch (row.kind)
        {
            case Row::Kind::heading:
                y += first ? 0 : 18;
                row.textArea = { 0, y, width, headingHeight };
                y += headingHeight + 12;
                break;

            case Row::Kind::labelled:
            {
                const int available = width - controlX;
                const int controlWidth = row.controlWidth > 0 ? juce::jmin (row.controlWidth, available) : available;
                const int height = juce::jmax (rowHeight, row.control->getHeight());
                row.control->setBounds (controlX, y, controlWidth, height);
                row.textArea = { 0, y, labelWidth - 12, rowHeight };
                y += height;

                if (row.note.isNotEmpty())
                {
                    const int noteHeight = measureHeight (row.note, noteFont(), available);
                    row.noteArea = { controlX, y + noteGap, available, noteHeight };
                    y += noteGap + noteHeight;
                }

                y += rowGap;
                break;
            }

            case Row::Kind::note:
            {
                const int noteHeight = measureHeight (row.text, noteFont(), width);
                row.textArea = { 0, y, width, noteHeight };
                y += noteHeight + rowGap;
                break;
            }

            case Row::Kind::wide:
                row.control->setBounds (0, y, width, row.control->getHeight());
                y += row.control->getHeight() + rowGap;
                break;
        }

        first = false;
    }

    content->setSize (width, juce::jmax (y, 1));
    content->repaint();
}

} // namespace ikaros
