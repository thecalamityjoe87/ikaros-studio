#pragma once

#include "engine/Preferences.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace ikaros
{

//==============================================================================
/**
    Preferences > Appearance > Theme: three cards in a row (Light, Dark,
    Match system), each with a small picture of the app in that theme.
    Picking one saves the setting, and ThemeManager switches the app at once.
*/
class ThemePicker  : public juce::Component
{
public:
    explicit ThemePicker (Preferences&);
    ~ThemePicker() override;

    void resized() override;

    static constexpr int preferredHeight = 112;

private:
    class Card;

    Preferences& preferences;
    juce::OwnedArray<Card> cards;   // in the order of settings::ThemeChoice

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ThemePicker)
};

} // namespace ikaros
