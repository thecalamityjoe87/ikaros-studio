#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace ikaros
{

//==============================================================================
/**
    The little light at the end of an input meter: red once the input has
    clipped (been too loud), until it's clicked.
*/
class ClipLight  : public juce::Component,
                   public juce::SettableTooltipClient
{
public:
    ClipLight();

    void setClipped (bool);
    bool isClipped() const noexcept     { return clipped; }

    /** Clicked while lit: the owner clears the clip (in the engine). */
    std::function<void()> onClear;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    bool clipped = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ClipLight)
};

} // namespace ikaros
