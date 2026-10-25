#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace ikaros
{

//==============================================================================
/**
    The About window's Built With page: the projects Ikaros Studio is made
    from. Tracktion Engine and JUCE get cards with their logos; the rest get a
    row each with a lettered tile. Clicking a card or row opens the project's
    website.

    The license texts themselves stay in Acknowledgements and CREDITS.
*/
class AboutTechnology  : public juce::Component
{
public:
    AboutTechnology();
    ~AboutTechnology() override;   // Entry is defined in the .cpp

    int getHeightForWidth (int width) const;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class Entry;

    juce::OwnedArray<Entry> featured, others;
    juce::Rectangle<int> builtOnHeading, alsoInsideHeading, footer;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AboutTechnology)
};

} // namespace ikaros
