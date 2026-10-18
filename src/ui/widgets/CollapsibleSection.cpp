#include "ui/widgets/CollapsibleSection.h"
#include "ui/theme/Fonts.h"
#include "ui/theme/IkarosLookAndFeel.h"
#include "ui/theme/Tokens.h"
#include "ui/widgets/Icons.h"

namespace ikaros
{

using namespace tokens;

//==============================================================================
// The clickable title row. A real Button, so it's keyboard-focusable and
// screen readers announce it.
class CollapsibleSection::Header  : public juce::Button
{
public:
    Header (const juce::String& title) : juce::Button (title)
    {
        setTitle (title);
        setClickingTogglesState (true);
        setMouseClickGrabsKeyboardFocus (false);
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    void paintButton (juce::Graphics& g, bool over, bool) override
    {
        auto r = getLocalBounds().toFloat();

        if (over)
        {
            g.setColour (colour::raised.withAlpha (0.5f));
            g.fillRect (r);
        }

        auto content = r.reduced (12.0f, 0.0f);
        icons::draw (g, getToggleState() ? Icon::chevronDown : Icon::chevronRight,
                     content.removeFromRight (16.0f), colour::textSecondary, 16.0f, 2.0f);

        g.setColour (colour::text);
        g.setFont (fonts::uiSemiBold (textSize::md));
        g.drawText (getName(), content, juce::Justification::centredLeft, true);

        if (hasKeyboardFocus (false))
            IkarosLookAndFeel::drawFocusRing (g, r, size::radiusXs);
    }
};

//==============================================================================
CollapsibleSection::CollapsibleSection (const juce::String& title, juce::Component& c, bool startExpanded)
    : content (c), expanded (startExpanded)
{
    header = std::make_unique<Header> (title);
    header->setToggleState (expanded, juce::dontSendNotification);
    header->onClick = [this] { setExpanded (header->getToggleState()); };
    addAndMakeVisible (*header);

    addChildComponent (content);
    content.setVisible (expanded);
}

CollapsibleSection::~CollapsibleSection() = default;

void CollapsibleSection::setExpanded (bool shouldExpand)
{
    expanded = shouldExpand;
    header->setToggleState (expanded, juce::dontSendNotification);
    header->setTooltip (expanded ? "Collapse" : "Expand");
    content.setVisible (expanded);

    if (onExpandedChanged)
        onExpandedChanged();
}

int CollapsibleSection::getPreferredHeight() const
{
    return headerHeight + (expanded ? content.getHeight() : 0) + 1;
}

void CollapsibleSection::paint (juce::Graphics& g)
{
    // A divider under each section, like GarageBand's inspector.
    g.setColour (colour::divider.withAlpha (0.6f));
    g.fillRect (getLocalBounds().removeFromBottom (1));
}

void CollapsibleSection::resized()
{
    auto r = getLocalBounds().withTrimmedBottom (1);
    header->setBounds (r.removeFromTop (headerHeight));
    content.setBounds (r.withHeight (content.getHeight()));
}

} // namespace ikaros
