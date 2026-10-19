#include "ui/toolbar/Toolbar.h"
#include "app/CommandIDs.h"
#include "engine/AudioEngine.h"
#include "engine/TrackController.h"
#include "ui/theme/Fonts.h"
#include "ui/theme/Tokens.h"
#include "ui/widgets/Decibels.h"

namespace ikaros
{

using namespace tokens;

namespace
{
    // The right-hand side, from the transport outwards.
    constexpr int speakerWidth = 20, speakerGap = 8;
    constexpr int masterWidth = 150;        // "Master", its dB readout and the slider
    constexpr int masterLabelHeight = 13;   // the "Master  −3.0 dB" row
    constexpr int statsGap = 14, statsWidth = 110;
}

Toolbar::Toolbar (AudioEngine& e, juce::ApplicationCommandManager& cm)
    : engine (e), commands (cm), transport (e)
{
    for (auto* b : { &libraryButton, &undoButton, &redoButton })
    {
        b->setColours (IconButton::borderedColours);
        addAndMakeVisible (b);
    }

    // Showing the library isn't an "on" state in the accent sense: neutral fill.
    libraryButton.setColours (IconButton::neutralToggleColours);

    // invokeDirectly runs the command synchronously, same as picking it from a menu.
    libraryButton.onClick  = [this] { commands.invokeDirectly (CommandIDs::toggleLibrary, true); };
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

        masterVolume.setTooltip ("Master volume (double-click for 0 dB)");
    }

    // The readout follows the volume live, during a drag too.
    const auto dbText = decibelText (engine.getTracks().getMasterVolumeDb());

    if (dbText != masterDbText)
    {
        masterDbText = dbText;
        repaint (masterArea.withHeight (masterLabelHeight));
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
    g.fillAll (theme().toolbarBg);
    g.setColour (theme().toolbarBorder);
    g.fillRect (getLocalBounds().removeFromBottom (1));

    icons::draw (g, Icon::speaker, speakerArea.toFloat(), theme().textSecondary, 18.0f);

    // "Master" on the left and the dB readout on the right, in one row.
    const auto labelRow = masterArea.withHeight (masterLabelHeight);
    g.setColour (theme().textSecondary);
    g.setFont (fonts::ui (textSize::xs));
    g.drawText ("Master", labelRow, juce::Justification::centredLeft);
    g.setColour (theme().text);
    g.setFont (fonts::mono (textSize::xs));
    g.drawText (masterDbText, labelRow, juce::Justification::centredRight);

    g.setColour (theme().textSecondary);
    auto s = statsArea;
    g.drawText (statsLine1, s.removeFromTop (s.getHeight() / 2), juce::Justification::bottomRight);
    g.drawText (statsLine2, s, juce::Justification::topRight);

    // "RT": a small badge under the readout while the audio thread is real-time.
    if (realtimeBadge)
    {
        const auto badge = getRealtimeBadgeArea().toFloat();
        g.setColour (theme().realtimeBadgeBg);
        g.fillRoundedRectangle (badge, size::radiusTiny);
        g.setColour (theme().realtimeBadgeText);
        g.setFont (fonts::mono (textSize::xs));
        g.drawText ("RT", badge, juce::Justification::centred);
    }
}

juce::Rectangle<int> Toolbar::getRealtimeBadgeArea() const
{
    // Right-aligned with the text, just under its second line (which sits
    // at the top of its half of statsArea, so the badge can tuck up a little).
    return { statsArea.getRight() - 22, statsArea.getBottom() - 3, 22, 14 };
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
    auto r = getLocalBounds().withTrimmedBottom (1).reduced (size::panelPadding, 0);

    // Centre the transport in the whole toolbar, not just the leftover space.
    // In a narrow window it moves off-centre (and then shrinks) rather than
    // run into the buttons on either side.
    const int gap = 12;
    const int leftWidth = 3 * 38 + 2 * 6;   // library, Undo, Redo
    const int rightWidth = speakerWidth + speakerGap + masterWidth + statsGap + statsWidth;
    const int transportWidth = juce::jmin (transport.getPreferredWidth(), r.getWidth() - leftWidth - rightWidth - 2 * gap);
    const int transportX = juce::jlimit (r.getX() + leftWidth + gap, r.getRight() - rightWidth - gap - transportWidth,
                                         r.getCentreX() - transportWidth / 2);
    transport.setBounds (r.withX (transportX).withWidth (transportWidth).withSizeKeepingCentre (transportWidth, 52));

    auto left = r.withRight (transport.getX() - gap);
    auto place = [&] (juce::Component& c, int w)
    {
        c.setBounds (left.removeFromLeft (w).withSizeKeepingCentre (w, 38));
        left.removeFromLeft (6);
    };
    place (libraryButton, 38);
    place (undoButton, 38);
    place (redoButton, 38);

    // From the right edge: stats, then the master block, then its speaker icon.
    auto right = r.withLeft (transport.getRight() + gap);
    statsArea = right.removeFromRight (statsWidth).withSizeKeepingCentre (statsWidth, 32);
    right.removeFromRight (statsGap);

    // "Master  −3.0 dB" in one row, and the level-and-volume slider under it.
    masterArea = right.removeFromRight (masterWidth).withSizeKeepingCentre (masterWidth, 34);
    masterVolume.setBounds (masterArea.withTrimmedTop (masterLabelHeight + 1)
                                      .expanded (6, 0));   // the handle overhangs the bars a little
    right.removeFromRight (speakerGap);
    speakerArea = right.removeFromRight (speakerWidth).withSizeKeepingCentre (speakerWidth, speakerWidth);
}

} // namespace ikaros
