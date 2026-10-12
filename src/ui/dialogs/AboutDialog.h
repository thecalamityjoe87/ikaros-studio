#pragma once

#include "engine/AudioEngine.h"
#include "ui/widgets/IconButton.h"

namespace ikaros
{

//==============================================================================
/**
    Help > About Ikaros Studio: the version, then who made the sounds and the
    software the app is built on. The sound credits come from each installed
    pack's pack.json, so downloaded packs are credited as soon as they're
    installed (several of them require it: CC BY).

    Shown inside the main window over a dimmed background, like the other
    dialogs. Deletes itself when closed.
*/
class AboutDialog  : public juce::Component
{
public:
    static void show (juce::Component& parent, AudioEngine&);

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    void mouseDown (const juce::MouseEvent&) override;   // click outside the panel = close

private:
    explicit AboutDialog (AudioEngine&);

    juce::TextEditor credits;          // read-only, scrolls
    IconButton doneButton { "Done", std::nullopt, "Done" };
    juce::Rectangle<int> panel;

    void close();

    static constexpr int panelWidth = 620, panelHeight = 560;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AboutDialog)
};

} // namespace ikaros
