#include "ui/toolbar/Toolbar.h"
#include "app/CommandIDs.h"
#include "engine/AudioEngine.h"
#include "engine/TrackController.h"
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

    for (auto* b : { &libraryButton, &newTrackButton, &undoButton, &redoButton })
    {
        b->setColours (raisedWithBorder);
        addAndMakeVisible (b);
    }

    newTrackButton.setIconSize (16.0f);

    // invokeDirectly runs the command synchronously, same as picking it from a menu.
    libraryButton.onClick  = [this] { commands.invokeDirectly (CommandIDs::toggleLibrary, true); };
    newTrackButton.onClick = [this] { if (onNewTrackRequested) onNewTrackRequested (&newTrackButton); };
    undoButton.onClick     = [this] { commands.invokeDirectly (CommandIDs::undo, true); };
    redoButton.onClick     = [this] { commands.invokeDirectly (CommandIDs::redo, true); };

    addAndMakeVisible (transport);

    // The master volume, its track a level meter, like GarageBand's. It's
    // the same setting as the Master page's knob and the Mixer's master
    // fader. Double-click goes back to 0 dB; one undo step per drag.
    masterVolume.setTitle ("Master volume");
    masterVolume.setVelocityModeParameters (1.0, 1, 0.0, false);
    masterVolume.setDoubleClickReturnValue (true, TrackController::getFaderPositionForDb (0.0f));
    masterVolume.onDragStart = [this] { engine.beginUndoStep ("Master Volume"); };
    masterVolume.onValueChange = [this] { engine.getTracks().setMasterVolume ((float) masterVolume.getValue()); };
    addAndMakeVisible (masterVolume);
}

void Toolbar::update (bool libraryVisible)
{
    libraryButton.setToggleState (libraryVisible, juce::dontSendNotification);
    undoButton.setEnabled (engine.canUndo());
    undoButton.setTooltip (juce::String ("Undo " + engine.getUndoDescription()).trimEnd());
    redoButton.setEnabled (engine.canRedo());
    redoButton.setTooltip (juce::String ("Redo " + engine.getRedoDescription()).trimEnd());

    transport.update();
    masterVolume.setLevel (engine.getMasterLevel());

    // Follow changes made elsewhere (the Mixer, undo), but don't fight a drag.
    if (! masterVolume.isMouseButtonDown())
    {
        auto& tc = engine.getTracks();
        masterVolume.setValue (tc.getMasterVolume(), juce::dontSendNotification);

        const auto db = tc.getMasterVolumeDb();
        masterVolume.setTooltip ("Master volume: " + (db <= -99.0f ? juce::String ("off") : juce::String (db, 1) + " dB")
                                 + " (double-click for 0 dB)");
    }

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

        // optional::value_or: the value if there is one, otherwise the fallback.
        const bool known = stats.sampleRate > 0 && stats.realtimeAudio.has_value();
        const bool rt = known && stats.realtimeAudio.value_or (false);

        if (l1 != statsLine1 || l2 != statsLine2 || rt != realtimeBadge || known != realtimeKnown)
        {
            statsLine1 = l1;
            statsLine2 = l2;
            realtimeBadge = rt;
            realtimeKnown = known;
            repaint (statsArea.getUnion (getRealtimeBadgeArea()));
        }
    }
}

void Toolbar::paint (juce::Graphics& g)
{
    g.fillAll (colour::toolbarBg);
    g.setColour (colour::divider);
    g.fillRect (getLocalBounds().removeFromBottom (1));

    g.setColour (colour::textSecondary);
    g.setFont (fonts::ui (textSize::xs));
    g.drawText ("Master", masterArea.withHeight (13), juce::Justification::centredLeft);

    g.setFont (fonts::mono (textSize::xs));
    auto s = statsArea;
    g.drawText (statsLine1, s.removeFromTop (s.getHeight() / 2), juce::Justification::bottomRight);
    g.drawText (statsLine2, s, juce::Justification::topRight);

    // "RT": a small badge under the readout while the audio thread is real-time.
    if (realtimeBadge)
    {
        const auto badge = getRealtimeBadgeArea().toFloat();
        g.setColour (colour::meterGreen.withAlpha (0.18f));
        g.fillRoundedRectangle (badge, size::radiusTiny);
        g.setColour (colour::meterGreen);
        g.setFont (fonts::mono (textSize::badge));
        g.drawText ("RT", badge, juce::Justification::centred);
    }
}

juce::Rectangle<int> Toolbar::getRealtimeBadgeArea() const
{
    // Right-aligned with the text, just under its second line (which sits
    // at the top of its half of statsArea, so the badge can tuck up a little).
    return { statsArea.getRight() - 20, statsArea.getBottom() - 2, 20, 12 };
}

juce::String Toolbar::getTooltip()
{
    if (! statsArea.getUnion (getRealtimeBadgeArea()).contains (getMouseXYRelative()))
        return {};

    juce::String tip ("CPU: how much of the time between audio blocks it takes to make the sound.");

    if (realtimeKnown)
        tip << "\n\n" << (realtimeBadge
                ? "RT: real-time audio is on. Sound comes first on this computer, so small buffer sizes play cleanly."
                : "Real-time audio is off: other programs can hold up the sound, so small buffer sizes may crackle. "
                  "(Ikaros asks the system's RealtimeKit service for it.)");

    return tip;
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
    place (redoButton, 38);

    auto right = r.withLeft (transport.getRight() + 12);
    statsArea = right.removeFromRight (110).withSizeKeepingCentre (110, 32);
    right.removeFromRight (14);
    // "Master", and the level-and-volume slider under it.
    masterArea = right.removeFromRight (120).withSizeKeepingCentre (120, 34);
    masterVolume.setBounds (masterArea.withTrimmedTop (14).expanded (6, 0));   // the handle overhangs the bars a little
}

} // namespace ikaros
