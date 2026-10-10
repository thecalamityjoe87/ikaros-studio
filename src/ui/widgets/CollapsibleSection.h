#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace ikaros
{

//==============================================================================
/**
    A titled section with a chevron that expands/collapses its content, like
    the sections in GarageBand's track inspector.

    The section doesn't own its content component; the owner keeps it and
    calls getPreferredHeight() when laying sections out.
*/
class CollapsibleSection  : public juce::Component
{
public:
    CollapsibleSection (const juce::String& title, juce::Component& content, bool startExpanded = true);
    ~CollapsibleSection() override;   // defined in the .cpp, where Header is a complete type

    bool isExpanded() const noexcept        { return expanded; }
    void setExpanded (bool);

    /** Header height plus the content's height when expanded. */
    int getPreferredHeight() const;

    /** Called after expanding/collapsing, so the owner can re-layout. */
    std::function<void()> onExpandedChanged;

    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int headerHeight = 34;

private:
    class Header;

    juce::Component& content;
    std::unique_ptr<Header> header;
    bool expanded;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CollapsibleSection)
};

} // namespace ikaros
