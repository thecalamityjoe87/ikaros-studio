#pragma once

#include "ui/state/Selection.h"
#include "ui/widgets/IconButton.h"
#include "ui/widgets/Knob.h"
#include "ui/theme/Family.h"
#include "ui/bottompanel/TuneControls.h"

namespace ikaros
{

//==============================================================================
/**
    The settings for one effect, shown in the bottom panel's main area when you
    click an effect in the Plug-ins inspector: a bordered box with on/off and
    the name along its top and one knob per parameter under them, wrapping
    to the available width, and Done beside the box.

    Built-in effects have no editor of their own, so this generic one works
    for all of them. Tune is laid out like its design, with its
    settings that aren't knobs (its key, the notes it can land on, Follow
    MIDI) and its readout round its knobs (see TuneControls). Third-party plug-ins open their own window instead
    (see PluginChainView::openEditor).
*/
class PluginEditor  : public juce::Component,
                      private juce::Timer
{
public:
    PluginEditor (AudioEngine&, TrackId, PluginId, tokens::Family);

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
    std::unique_ptr<TuneControls> tuneControls;   // Tune only
    juce::Rectangle<int> box, titleArea, contentArea;

    static constexpr int padding = 18, gap = 14;
    static constexpr int boxPadding = 16;         // inside the box's border
    static constexpr int titleRowHeight = 28;     // on/off and the name (and Tune's Key and Lock to Key)
    static constexpr int powerSize = 24, titleGap = 8, doneWidth = 76;
    static constexpr const char* noSettingsText = "This effect has no settings.";

    int columnsFor (int innerWidth) const;
    int getTitleWidth() const;
    juce::Rectangle<int> getBoxBounds (int width) const;   // for the editor's width
    juce::String getTitleText() const;
    juce::Font getTitleFont() const;
    void refreshValues();
    void timerCallback() override;   // keeps knobs in sync (undo, automation)

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginEditor)
};

} // namespace ikaros
