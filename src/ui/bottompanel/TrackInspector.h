#pragma once

#include "ui/bottompanel/PluginChainView.h"
#include "ui/bottompanel/RecordingView.h"
#include "ui/theme/Tokens.h"
#include "ui/widgets/CollapsibleSection.h"

namespace ikaros
{

//==============================================================================
/**
    The left column of the bottom panel: settings for the selected track,
    grouped in collapsible sections (like GarageBand's track inspector).

      Recording  input, monitoring and input level (audio tracks)
      Plug-ins   the instrument and effect chain

    New sections (noise gate, sends to master reverb/echo, ...) slot in here.
*/
class TrackInspector  : public juce::Component
{
public:
    TrackInspector (AudioEngine&, Selection&);
    ~TrackInspector() override;

    void refresh();
    void update();   // ~30 Hz: the input meter

    /** Forwarded from the Plug-ins section: show an effect's settings. */
    std::function<void (PluginId)> onOpenEffect;
    void setOpenEffect (PluginId id)    { plugins.setOpenEffect (id); }

    /** Master mode: just the master channel's effects (no Recording section). */
    void setMasterMode (bool);

    void paint (juce::Graphics&) override;
    void resized() override;

    // Same width as the track headers above, so the two columns line up.
    static constexpr int preferredWidth = tokens::size::trackHeaderWidth;

private:
    RecordingView recording;
    PluginChainView plugins;

    CollapsibleSection recordingSection;
    CollapsibleSection pluginsSection;

    juce::Viewport viewport;
    juce::Component sectionHolder;

    void layoutSections();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TrackInspector)
};

} // namespace ikaros
