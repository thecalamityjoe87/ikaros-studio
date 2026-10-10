#include "ui/toolbar/Toolbar.h"
#include "app/CommandIDs.h"
#include "engine/AudioEngine.h"
#include "ui/theme/Fonts.h"
#include "ui/theme/Tokens.h"

namespace ikaros
{

using namespace tokens;

Toolbar::Toolbar (AudioEngine& e, juce::ApplicationCommandManager& cm)
    : engine (e), commands (cm), transport (e)
{
    auto raisedWithBorder = IconButton::raisedColours();
    raisedWithBorder.border = colour::border;

    for (auto* b : { &libraryButton, &newTrackButton, &undoButton })
    {
        b->setColours (raisedWithBorder);
        addAndMakeVisible (b);
    }

    newTrackButton.setIconSize (16.0f);

    // invokeDirectly runs the command synchronously, same as picking it from a menu.
    libraryButton.onClick  = [this] { commands.invokeDirectly (CommandIDs::toggleLibrary, true); };
    newTrackButton.onClick = [this] { commands.invokeDirectly (CommandIDs::newTrack, true); };
    undoButton.onClick     = [this] { commands.invokeDirectly (CommandIDs::undo, true); };

    addAndMakeVisible (transport);
    addAndMakeVisible (masterMeter);
}

void Toolbar::update (bool libraryVisible)
{
    libraryButton.setToggleState (libraryVisible, juce::dontSendNotification);
    undoButton.setEnabled (engine.canUndo());
    undoButton.setTooltip (juce::String ("Undo " + engine.getUndoDescription()).trimEnd());

    transport.update();
    masterMeter.setLevel (engine.getMasterLevel());

    // CPU text changes constantly; refreshing it ~3 times a second is easier to read.
    if (--statsCountdown <= 0)
    {
        statsCountdown = 10;
        auto stats = engine.getSystemStats();
        auto l1 = "CPU " + juce::String (juce::roundToInt (stats.cpuPercent)) + "%";
        auto khz = stats.sampleRate / 1000.0;
        auto khzText = std::fmod (khz, 1.0) == 0.0 ? juce::String ((int) khz) : juce::String (khz, 1);
        auto l2 = stats.sampleRate > 0
                    ? khzText + " kHz "
                        + juce::String::fromUTF8 ("\xc2\xb7 ") + juce::String (juce::roundToInt (stats.latencyMs)) + " ms"
                    : juce::String ("No audio device");

        if (l1 != statsLine1 || l2 != statsLine2)
        {
            statsLine1 = l1;
            statsLine2 = l2;
            repaint (statsArea);
        }
    }
}

void Toolbar::paint (juce::Graphics& g)
{
    g.fillAll (colour::toolbarBg);
    g.setColour (colour::divider);
    g.fillRect (getLocalBounds().removeFromBottom (1));

    g.setColour (colour::textSecondary);
    g.setFont (fonts::ui (11.0f));
    g.drawText ("Master", masterArea.withHeight (14), juce::Justification::centredLeft);

    g.setFont (fonts::mono (11.0f));
    auto s = statsArea;
    g.drawText (statsLine1, s.removeFromTop (s.getHeight() / 2), juce::Justification::bottomRight);
    g.drawText (statsLine2, s, juce::Justification::topRight);
}

void Toolbar::resized()
{
    auto r = getLocalBounds().withTrimmedBottom (1).reduced (12, 0);

    // Centre the transport in the whole toolbar, not just the leftover space.
    auto transportWidth = juce::jmin (transport.getPreferredWidth(), r.getWidth() - 2 * 300);
    transport.setBounds (r.withSizeKeepingCentre (transportWidth, 52));

    auto left = r.withRight (transport.getX() - 12);
    auto place = [&] (juce::Component& c, int w)
    {
        c.setBounds (left.removeFromLeft (w).withSizeKeepingCentre (w, 38));
        left.removeFromLeft (6);
    };
    place (libraryButton, 38);
    place (newTrackButton, 124);
    place (undoButton, 38);

    auto right = r.withLeft (transport.getRight() + 12);
    statsArea = right.removeFromRight (110).withSizeKeepingCentre (110, 32);
    right.removeFromRight (14);
    masterArea = right.removeFromRight (120).withSizeKeepingCentre (120, 30);
    masterMeter.setBounds (masterArea.withTrimmedTop (16));
}

} // namespace ikaros
