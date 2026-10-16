#include "ui/preferences/AudioSettingsPanel.h"
#include "ui/theme/Fonts.h"
#include "ui/theme/Tokens.h"

namespace ikaros
{

using namespace tokens;

AudioSettingsPanel::AudioSettingsPanel (AudioEngine& e) : engine (e)
{
    selector = engine.createAudioSettingsComponent();
    addAndMakeVisible (*selector);

    status = engine.getAudioDeviceStatus();
    setSize (selector->getWidth(), selector->getHeight() + infoHeight);

    // Twice a second is plenty to follow a change of buffer size or device.
    startTimer (500);
}

void AudioSettingsPanel::timerCallback()
{
    auto s = engine.getAudioDeviceStatus();

    if (s.running != status.running || s.deviceName != status.deviceName || s.bufferSize != status.bufferSize
         || s.sampleRate != status.sampleRate || s.dropouts != status.dropouts
         || std::abs (s.monitoringLatencyMs - status.monitoringLatencyMs) > 0.05)
    {
        status = s;
        repaint (infoArea);
    }
}

void AudioSettingsPanel::paint (juce::Graphics& g)
{
    auto r = infoArea;
    g.setColour (colour::textSecondary.withAlpha (0.25f));
    g.fillRect (r.removeFromTop (1));
    r.removeFromTop (10);

    if (! status.running)
    {
        g.setColour (colour::textSecondary);
        g.setFont (fonts::ui (13.0f));
        g.drawFittedText ("The audio device isn't running. Choose one above.", r, juce::Justification::topLeft, 2);
        return;
    }

    auto ms = [] (double v) { return juce::String (juce::roundToInt (v)) + " ms"; };

    g.setColour (colour::text);
    g.setFont (fonts::uiSemiBold (14.0f));
    g.drawText ("Monitoring delay (latency): " + ms (status.monitoringLatencyMs),
                r.removeFromTop (20), juce::Justification::centredLeft);

    // The figures come from the audio driver; on Linux they're its estimate.
    juce::String details = "How late you hear yourself while monitoring. A smaller buffer size lowers it; "
                           "a larger one avoids dropouts (clicks and gaps). Recordings are lined up "
                           "with the music automatically.";

    if (status.dropouts > 0)
        details << "\nDropouts since the device started: " << status.dropouts << ".";

    g.setColour (colour::textSecondary);
    g.setFont (fonts::ui (12.5f));
    g.drawFittedText (details, r.withTrimmedTop (4), juce::Justification::topLeft, 4, 1.0f);
}

void AudioSettingsPanel::resized()
{
    auto r = getLocalBounds();
    infoArea = r.removeFromBottom (infoHeight).reduced (16, 0).withTrimmedBottom (8);

    // Only the width is ours to give: the picker works out its own height (from
    // its height, as it happens, so handing that back to it would never settle).
    selector->setBounds (r.withHeight (selector->getHeight()));
}

// The picker's height follows what it shows (another device has other settings).
void AudioSettingsPanel::childBoundsChanged (juce::Component* child)
{
    if (child == selector.get() && getHeight() != selector->getHeight() + infoHeight)
        setSize (getWidth(), selector->getHeight() + infoHeight);
}

} // namespace ikaros
