#pragma once

#include "ui/state/Selection.h"
#include "ui/widgets/IconButton.h"
#include "ui/widgets/Knob.h"

namespace ikaros
{

//==============================================================================
/**
    The settings for one effect, shown in the bottom panel's main area when you
    click an effect in the Plug-ins inspector: a header (name, on/off, Done)
    and one knob per parameter, wrapping to the available width.

    Built-in effects have no editor of their own, so this generic one works
    for all of them. TODO(milestone-3): open the plug-in's own window for
    third-party VST3/AU effects.
*/
class PluginEditor  : public juce::Component,
                      private juce::Timer
{
public:
    PluginEditor (AudioEngine&, TrackId, PluginId, juce::Colour accent);

    TrackId getTrackId() const noexcept     { return trackId; }
    PluginId getPluginId() const noexcept   { return pluginId; }

    /** Height needed to show every knob at the given width. */
    int getIdealHeight (int width) const;

    /** "Done" was clicked, or the effect no longer exists (e.g. after Undo). */
    std::function<void()> onClose;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    AudioEngine& engine;
    const TrackId trackId;
    const PluginId pluginId;
    PluginSlot slot;

    IconButton powerButton { "Turn effect on or off", Icon::power };
    IconButton doneButton { "Done", std::nullopt, "Done" };
    juce::OwnedArray<Knob> knobs;

    static constexpr int padding = 18, headerHeight = 40, gap = 14;

    int columnsFor (int width) const;
    void refreshValues();
    void timerCallback() override;   // keeps knobs in sync (undo, automation)

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginEditor)
};

} // namespace ikaros
