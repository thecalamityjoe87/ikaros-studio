#include "ui/bottompanel/TrackInspector.h"
#include "ui/theme/Fonts.h"
#include "ui/theme/Tokens.h"

namespace ikaros
{

using namespace tokens;

//==============================================================================
TrackInspector::TrackInspector (AudioEngine& e, Selection& s)
    : recording (e, s),
      plugins (e, s),
      recordingSection ("Recording", recording, true),
      pluginsSection ("Plug-ins", plugins, true)
{
    setTitle ("Track inspector");

    for (auto* section : { &recordingSection, &pluginsSection })
    {
        section->onExpandedChanged = [this] { layoutSections(); };
        sectionHolder.addAndMakeVisible (section);
    }

    recording.onHeightChanged = [this] { layoutSections(); };
    plugins.onHeightChanged = [this] { layoutSections(); };
    plugins.onOpenEffect = [this] (PluginId id) { if (onOpenEffect) onOpenEffect (id); };

    viewport.setViewedComponent (&sectionHolder, false);
    viewport.setScrollBarsShown (true, false);
    addAndMakeVisible (viewport);
}

TrackInspector::~TrackInspector() = default;

void TrackInspector::refresh()
{
    recording.refresh();
    plugins.refresh();
}

void TrackInspector::setMasterMode (bool master)
{
    recordingSection.setVisible (! master);
    plugins.setMasterMode (master);
    layoutSections();
}

void TrackInspector::update()
{
    recording.updateMeter();
}

void TrackInspector::layoutSections()
{
    const int width = viewport.getMaximumVisibleWidth();

    // Contents decide their own height; sections stack them with headers.
    recording.setSize (width, recording.getIdealHeight());
    plugins.setSize (width, plugins.getIdealHeight());

    int y = 0;

    for (auto* section : { &recordingSection, &pluginsSection })
    {
        if (! section->isVisible())
            continue;

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
