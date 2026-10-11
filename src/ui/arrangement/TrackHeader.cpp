#include "ui/arrangement/TrackHeader.h"
#include "ui/theme/Fonts.h"
#include "ui/theme/IkarosLookAndFeel.h"
#include "ui/theme/Tokens.h"

namespace ikaros
{

using namespace tokens;

namespace
{
    constexpr int panSize = 40;
}

TrackHeader::TrackHeader (AudioEngine& e, Selection& s, const TrackInfo& t)
    : engine (e), selection (s), info (t)
{
    setFocusContainerType (juce::Component::FocusContainerType::focusContainer);

    auto setupToggle = [this] (IconButton& b, juce::Colour onColour)
    {
        b.setColours ({ colour::controlFill, colour::text, onColour, colour::textOnAccent });
        b.setCornerRadius (size::radiusSmall);
        b.setFont (fonts::uiBold (11.0f));
        addAndMakeVisible (b);
    };

    setupToggle (muteButton, colour::mute);
    setupToggle (soloButton, colour::solo);
    muteButton.setIconSize (14.0f);
    soloButton.setIconSize (14.0f);

    // Record-arm: grey dot when off, white dot on red when armed.
    armButton.setColours ({ colour::controlFill, colour::textSecondary, colour::record, juce::Colours::white });
    armButton.setCornerRadius (size::radiusSmall);
    armButton.setIconSize (12.0f);
    armButton.onClick = [this]
    {
        selection.selectTrack (info.id);
        engine.getTracks().setArmed (info.id, ! info.armed);
    };
    addAndMakeVisible (armButton);

    // Monitor (audio tracks): hear the input live. Same green as the
    // Recording inspector's Monitoring button.
    monitorButton.setColours ({ colour::controlFill, colour::textSecondary, colour::effectOn, colour::textOnAccent });
    monitorButton.setCornerRadius (size::radiusSmall);
    monitorButton.setIconSize (14.0f);
    monitorButton.onClick = [this] { engine.getTracks().setMonitoring (info.id, ! info.monitoring); };
    addChildComponent (monitorButton);

    muteButton.onClick = [this] { engine.getTracks().setMute (info.id, ! info.muted); };
    soloButton.onClick = [this] { engine.getTracks().setSolo (info.id, ! info.solo); };

    volume.setRange (0.0, 1.0);
    volume.setVelocityModeParameters (1.0, 1, 0.0, false);
    volume.setColour (juce::Slider::trackColourId, colour::accent);
    // One undo step per drag: start a transaction when the mouse goes down.
    volume.onDragStart = [this] { engine.beginUndoStep ("Volume"); selection.selectTrack (info.id); };
    volume.onValueChange = [this] { engine.getTracks().setVolume (info.id, (float) volume.getValue()); };
    addAndMakeVisible (volume);

    // Pan: a small knob, centre at 12 o'clock, like the Mixer's. Double-click centres it.
    pan.setRange (-1.0, 1.0);
    pan.setDoubleClickReturnValue (true, 0.0);
    pan.getProperties().set ("bipolar", true);   // read by IkarosLookAndFeel::drawRotarySlider
    pan.onDragStart = [this] { engine.beginUndoStep ("Pan"); selection.selectTrack (info.id); };
    pan.onValueChange = [this] { engine.getTracks().setPan (info.id, (float) pan.getValue()); };
    addAndMakeVisible (pan);

    // The rename field: hidden until startRenaming(). Return keeps the new
    // name, Escape cancels, clicking elsewhere keeps it.
    nameEditor.setFont (fonts::uiBold (14.0f));
    nameEditor.setIndents (4, 2);
    nameEditor.setJustification (juce::Justification::centredLeft);
    nameEditor.setTitle ("Track name");
    nameEditor.onReturnKey = [this] { finishRenaming (true); };
    nameEditor.onEscapeKey = [this] { finishRenaming (false); };
    nameEditor.onFocusLost = [this] { finishRenaming (true); };
    addChildComponent (nameEditor);

    update (t, false);
}

void TrackHeader::updateAccessibleNames()
{
    muteButton.setTitle ("Mute " + info.name);
    muteButton.setTooltip ("Mute " + info.name);
    soloButton.setTitle ("Solo " + info.name);
    soloButton.setTooltip ("Solo " + info.name);
    armButton.setTitle ((info.armed ? "Disarm " : "Arm ") + info.name + " for recording");
    armButton.setTooltip (info.armed ? "Armed: Record records here" : "Arm for recording");
    monitorButton.setTitle ((info.monitoring ? "Stop monitoring " : "Monitor ") + info.name);
    monitorButton.setTooltip (info.monitoring ? "Monitoring: you hear the input live"
                                              : "Monitor: hear the input live (use headphones to avoid feedback)");
    pan.setTitle (info.name + " pan");
    volume.setTitle (info.name + " volume");
    setTitle (info.name + " track");
}

void TrackHeader::update (const TrackInfo& t, bool isSelected)
{
    info = t;
    selected = isSelected;

    muteButton.setToggleState (t.muted, juce::dontSendNotification);
    soloButton.setToggleState (t.solo, juce::dontSendNotification);
    armButton.setToggleState (t.armed, juce::dontSendNotification);

    // Don't fight the user: only move the slider if they aren't dragging it.
    if (! volume.isMouseButtonDown())
        volume.setValue (t.volume, juce::dontSendNotification);

    volume.setTooltip (juce::String (t.volumeDb, 1) + " dB");
    volume.setColour (juce::Slider::trackColourId, t.colour);

    if (! pan.isMouseButtonDown())
        pan.setValue (t.pan, juce::dontSendNotification);

    const int panAmount = juce::roundToInt (std::abs (t.pan) * 100.0f);
    pan.setTooltip ("Pan: " + (panAmount == 0 ? juce::String ("center") : (t.pan < 0.0f ? "left " : "right ") + juce::String (panAmount))
                    + " (double-click to center)");
    pan.setColour (IkarosLookAndFeel::knobArcColourId, t.colour);

    const bool wasAudio = monitorButton.isVisible();
    monitorButton.setVisible (t.kind == TrackKind::audio);
    monitorButton.setToggleState (t.monitoring, juce::dontSendNotification);

    if (wasAudio != monitorButton.isVisible())
        resized();
    updateAccessibleNames();
    repaint();
}

void TrackHeader::mouseDown (const juce::MouseEvent& e)
{
    selection.selectTrack (info.id);

    if (e.mods.isPopupMenu())
    {
        if (onShowMenu)
            onShowMenu();
    }
    else if (iconArea.contains (e.getPosition()))
    {
        showIconMenu();
    }
}

void TrackHeader::mouseMove (const juce::MouseEvent& e)
{
    // The icon tile is a button: show it.
    setMouseCursor (iconArea.contains (e.getPosition()) ? juce::MouseCursor::PointingHandCursor
                                                        : juce::MouseCursor::NormalCursor);
}

void TrackHeader::showIconMenu()
{
    createIconMenu (engine, info.id).showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this)
                                                                               .withTargetScreenArea (localAreaToGlobal (iconArea)));
}

juce::PopupMenu TrackHeader::createIconMenu (AudioEngine& engine, TrackId id)
{
    juce::PopupMenu menu;
    const auto track = engine.getTracks().getTrack (id);

    if (! track)
        return menu;

    const auto current = track->icon;

    menu.addItem (juce::PopupMenu::Item ("Automatic (follows the sound)")
                      .setImage (icons::createDrawable (icons::forCategory (track->category), colour::textSecondary))
                      .setTicked (current.isEmpty())
                      .setAction ([&engine, id] { engine.getTracks().setTrackIcon (id, {}); }));
    menu.addSeparator();

    for (auto& choice : icons::getTrackIconChoices())
    {
        auto key = choice.key;
        menu.addItem (juce::PopupMenu::Item (choice.name)
                          .setImage (icons::createDrawable (choice.icon, track->colour))
                          .setTicked (current == key)
                          .setAction ([&engine, id, key] { engine.getTracks().setTrackIcon (id, key); }));
    }

    return menu;
}

void TrackHeader::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (nameArea.contains (e.getPosition()))
        startRenaming();
}

void TrackHeader::startRenaming()
{
    renaming = true;
    nameEditor.setText (info.name, false);
    nameEditor.setVisible (true);
    nameEditor.grabKeyboardFocus();
    nameEditor.selectAll();
}

void TrackHeader::finishRenaming (bool keepNewName)
{
    // Hiding the field takes its focus away, which calls onFocusLost and so
    // this function again; the flag makes that second call do nothing.
    if (! std::exchange (renaming, false))
        return;

    // Let go of focus first: hiding a focused field would pass focus on to
    // the Mute button, which would then swallow the Space key.
    nameEditor.giveAwayKeyboardFocus();
    nameEditor.setVisible (false);

    if (keepNewName)
        engine.getTracks().renameTrack (info.id, nameEditor.getText());
}

void TrackHeader::paint (juce::Graphics& g)
{
    auto r = getLocalBounds();
    g.fillAll (selected ? colour::selectedRow : colour::sidebarBg);

    g.setColour (colour::divider);
    g.fillRect (r.removeFromBottom (1));

    // 3px colour edge on the left.
    g.setColour (info.colour);
    g.fillRect (r.removeFromLeft (3));

    // Icon tile
    g.setColour (selected ? colour::controlFill : juce::Colour (0xff1f232a));
    g.fillRoundedRectangle (iconArea.toFloat(), size::radiusLarge);
    icons::draw (g, icons::forTrack (info.icon, info.category), iconArea.toFloat(), info.colour, 22.0f);

    g.setColour (info.silencedBySolo || info.muted ? colour::textSecondary : colour::text);
    g.setFont (fonts::uiBold (14.0f));
    g.drawText (info.name, nameArea, juce::Justification::centredLeft, true);

    g.setColour (colour::textSecondary);
    g.setFont (fonts::ui (12.0f));
    g.drawText (info.soundName, soundArea, juce::Justification::centredLeft, true);
}

void TrackHeader::resized()
{
    auto r = getLocalBounds().withTrimmedBottom (1).withTrimmedLeft (3).reduced (10, 8);

    iconArea = r.removeFromLeft (40).removeFromTop (40);
    r.removeFromLeft (10);

    auto top = r.removeFromTop (22);
    armButton.setBounds (top.removeFromRight (24));
    top.removeFromRight (4);

    if (monitorButton.isVisible())
    {
        monitorButton.setBounds (top.removeFromRight (24));
        top.removeFromRight (4);
    }

    soloButton.setBounds (top.removeFromRight (24));
    top.removeFromRight (4);
    muteButton.setBounds (top.removeFromRight (24));
    top.removeFromRight (4);
    nameArea = top;
    nameEditor.setBounds (nameArea.expanded (2, 0));

    r.removeFromTop (2);
    soundArea = r.removeFromTop (16).withTrimmedRight (panSize);
    r.removeFromTop (2);

    // Pan knob at the end of the volume slider's row, centred on it. Its
    // bounds are taller than the row: the knob is drawn inset (about 26px
    // across), and the extra space is empty.
    auto volumeRow = r.removeFromTop (16);
    pan.setBounds (volumeRow.removeFromRight (panSize).withSizeKeepingCentre (panSize, panSize));
    volumeRow.removeFromRight (4);
    volume.setBounds (volumeRow);
}

} // namespace ikaros
