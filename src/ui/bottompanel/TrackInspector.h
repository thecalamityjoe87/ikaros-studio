#pragma once

#include "ui/bottompanel/PluginChainView.h"
#include "ui/theme/Tokens.h"
#include "ui/widgets/CollapsibleSection.h"

namespace ikaros
{

//==============================================================================
/**
    The left column of the bottom panel: settings for the selected track,
    grouped in collapsible sections (like GarageBand's track inspector).

      Plug-ins   the instrument and effect chain (expanded by default)
      Recording  placeholder until recording exists (collapsed)

    New sections (noise gate, sends to master reverb/echo, ...) slot in here.
*/
class TrackInspector  : public juce::Component
{
public:
    TrackInspector (AudioEngine&, Selection&);
    ~TrackInspector() override;

    void refresh();

    /** Forwarded from the Plug-ins section: show an effect's settings. */
    std::function<void (PluginId)> onOpenEffect;
    void setOpenEffect (PluginId id)    { plugins.setOpenEffect (id); }

    void paint (juce::Graphics&) override;
    void resized() override;

    // Same width as the track headers above, so the two columns line up.
    static constexpr int preferredWidth = tokens::size::trackHeaderWidth;

private:
    class RecordingPlaceholder;

    PluginChainView plugins;
    std::unique_ptr<RecordingPlaceholder> recording;

    CollapsibleSection pluginsSection;
    std::unique_ptr<CollapsibleSection> recordingSection;

    juce::Viewport viewport;
    juce::Component sectionHolder;

    void layoutSections();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TrackInspector)
};

} // namespace ikaros
