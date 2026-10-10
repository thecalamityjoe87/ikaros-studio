#include "ui/bottompanel/TrackInspector.h"
#include "ui/theme/Fonts.h"
#include "ui/theme/Tokens.h"

namespace ikaros
{

using namespace tokens;

//==============================================================================
// TODO(milestone-2): input, record level and monitoring, once recording exists.
class TrackInspector::RecordingPlaceholder  : public juce::Component
{
public:
    RecordingPlaceholder()
    {
        setSize (TrackInspector::preferredWidth, 52);
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (colour::textSecondary);
        g.setFont (fonts::ui (12.0f));
        g.drawFittedText ("Input, level and monitoring will appear here once recording is available.",
                          getLocalBounds().reduced (12, 6), juce::Justification::topLeft, 3, 1.0f);
    }
};

//==============================================================================
TrackInspector::TrackInspector (AudioEngine& e, Selection& s)
    : plugins (e, s),
      recording (std::make_unique<RecordingPlaceholder>()),
      pluginsSection ("Plug-ins", plugins, true)
{
    setTitle ("Track inspector");

    recordingSection = std::make_unique<CollapsibleSection> ("Recording", *recording, false);

    for (auto* section : { &pluginsSection, recordingSection.get() })
    {
        section->onExpandedChanged = [this] { layoutSections(); };
        sectionHolder.addAndMakeVisible (section);
    }

    plugins.onHeightChanged = [this] { layoutSections(); };
    plugins.onOpenEffect = [this] (PluginId id) { if (onOpenEffect) onOpenEffect (id); };

    viewport.setViewedComponent (&sectionHolder, false);
    viewport.setScrollBarsShown (true, false);
    addAndMakeVisible (viewport);
}

TrackInspector::~TrackInspector() = default;

void TrackInspector::refresh()
{
    plugins.refresh();
}

void TrackInspector::layoutSections()
{
    const int width = viewport.getMaximumVisibleWidth();

    // Contents decide their own height; sections stack them with headers.
    plugins.setSize (width, plugins.getIdealHeight());
    recording->setSize (width, recording->getHeight());

    int y = 0;

    for (auto* section : { &pluginsSection, recordingSection.get() })
    {
        section->setBounds (0, y, width, section->getPreferredHeight());
        y += section->getHeight();
    }

    sectionHolder.setSize (width, juce::jmax (y, viewport.getMaximumVisibleHeight()));
}

void TrackInspector::paint (juce::Graphics& g)
{
    g.fillAll (colour::sidebarBg);
    g.setColour (colour::divider);
    g.fillRect (getLocalBounds().removeFromRight (1));
}

void TrackInspector::resized()
{
    viewport.setBounds (getLocalBounds().withTrimmedRight (1));
    layoutSections();
}

} // namespace ikaros
