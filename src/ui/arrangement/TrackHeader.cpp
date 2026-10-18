#include "ui/arrangement/TrackHeader.h"
#include "ui/theme/Fonts.h"
#include "ui/theme/IkarosLookAndFeel.h"
#include "ui/theme/Tokens.h"
#include "ui/widgets/AutomationHint.h"
#include "ui/dialogs/MonitoringWarning.h"
#include <map>
#include <tuple>

namespace ikaros
{

using namespace tokens;

namespace
{
    constexpr int panSize = 46;
}

TrackHeader::TrackHeader (AudioEngine& e, Selection& s, const TrackInfo& t)
    : engine (e), selection (s), info (t)
{
    setFocusContainerType (juce::Component::FocusContainerType::focusContainer);

    auto setupToggle = [this] (IconButton& b, juce::Colour onColour)
    {
        b.setColours ({ colour::controlFill, colour::text, onColour, colour::textOnAccent });
        b.setCornerRadius (size::radiusSmall);
        b.setFont (fonts::uiBold (textSize::xs));
        addAndMakeVisible (b);
    };

    setupToggle (muteButton, colour::accent);   // a muted track's Mute is "on"
    setupToggle (soloButton, colour::solo);
    muteButton.setIconSize (14.0f);
    soloButton.setIconSize (14.0f);

    // Record-arm: grey dot when off, white dot on red when armed.
    armButton.setColours ({ colour::controlFill, colour::textSecondary, colour::record, colour::textOnColour });
    armButton.setCornerRadius (size::radiusSmall);
    armButton.setIconSize (12.0f);
    armButton.onClick = [this]
    {
        selection.selectTrack (info.id);
        engine.getTracks().inputs().setArmed (info.id, ! info.armed);
    };
    addAndMakeVisible (armButton);

    // Monitor (audio tracks): hear the input live. Same green as the
    // Recording inspector's Monitoring button.
    monitorButton.setColours ({ colour::controlFill, colour::textSecondary, colour::effectOn, colour::textOnAccent });
    monitorButton.setCornerRadius (size::radiusSmall);
    monitorButton.setIconSize (14.0f);
    monitorButton.onClick = [this] { setMonitoringWithWarning (engine, info.id, ! info.monitoring, this); };
    addChildComponent (monitorButton);

    // Flex (audio tracks): amber while it's on, like the automation button.
    flexButton.setColours ({ colour::controlFill, colour::textSecondary, colour::accent, colour::textOnAccent });
    flexButton.setCornerRadius (size::radiusSmall);
    flexButton.setFont (fonts::uiSemiBold (textSize::xs));
    flexButton.onClick = [this]
    {
        selection.selectTrack (info.id);
        engine.getTracks().flex().setFlexOn (info.id, ! flexOn);
    };
    addChildComponent (flexButton);

    clipLight.onClear = [this] { engine.getTracks().inputs().clearClipIndicator (info.id); };
    addChildComponent (clipLight);

    // Automation: opens the lane under the track (amber while it's open).
    automationButton.setColours ({ colour::controlFill, colour::textSecondary, colour::accent, colour::textOnAccent });
    automationButton.setCornerRadius (size::radiusSmall);
    automationButton.setIconSize (15.0f);
    automationButton.onClick = [this]
    {
        selection.selectTrack (info.id);
        engine.getTracks().automation().setLaneShown (info.id, ! info.automationShown);
    };
    addAndMakeVisible (automationButton);

    muteButton.onClick = [this] { engine.getTracks().setMute (info.id, ! info.muted); };
    soloButton.onClick = [this] { engine.getTracks().setSolo (info.id, ! info.solo); };

    volume.setVelocityModeParameters (1.0, 1, 0.0, false);
    volume.setDoubleClickReturnValue (true, TrackController::getFaderPositionForDb (0.0f));
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

    // Automated, they move with the automation, and a grab only moves them
    // until they're let go (see followAutomation).
    followAutomation (volume, [this] { return info.automated.contains (automationTargets::volume); },
                      [this] { showAutomationHint (engine, volume, info.id, automationTargets::volume); });
    followAutomation (pan, [this] { return info.automated.contains (automationTargets::pan); },
                      [this] { showAutomationHint (engine, pan, info.id, automationTargets::pan); });

    // The rename field: hidden until startRenaming(). Return keeps the new
    // name, Escape cancels, clicking elsewhere keeps it.
    nameEditor.setFont (fonts::uiBold (textSize::lg));
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
    automationButton.setTitle ((info.automationShown ? "Hide automation for " : "Show automation for ") + info.name);
    automationButton.setTooltip (info.automationShown ? "Hide automation" : "Show automation: draw how volume, pan and other settings change over time");
    monitorButton.setTitle ((info.monitoring ? "Stop monitoring " : "Monitor ") + info.name);
    monitorButton.setTooltip (info.monitoring ? "Monitoring: you hear the input live"
                                              : "Monitor: hear the input live (use headphones to avoid feedback)");
    flexButton.setTitle ((flexOn ? "Turn off Flex for " : "Turn on Flex for ") + info.name);
    flexButton.setTooltip (flexOn ? "Flex is on: drag the hits in this track's recordings in the Editor"
                                  : "Flex: move the hits in this track's recordings, stretching the audio in between");
    pan.setTitle (info.name + " pan");
    volume.setTitle (info.name + " volume");
    setTitle (info.name + " track");
}

void TrackHeader::update (const TrackInfo& t, bool isSelected)
{
    // Only redraw the header itself if something paint() draws changed. The
    // buttons and knobs redraw themselves when their own values change.
    // (std::tie bundles references to the members, so the two sets compare
    // in one go.)
    auto drawn = [] (const TrackInfo& i) { return std::tie (i.name, i.soundName, i.icon, i.soundIcon, i.category, i.colour, i.muted, i.silencedBySolo,
                                                            i.automationShown, i.automationTarget); };
    bool needsRepaint = isSelected != selected || drawn (t) != drawn (info);

    if (t.automationShown)
    {
        const auto target = engine.getTracks().automation().getTarget (t.id, t.automationTarget);
        const auto name = target ? target->name : juce::String ("Volume");
        const bool isAutomated = target && target->automated;
        needsRepaint |= name != automationName || isAutomated != automationTargetAutomated;
        automationName = name;
        automationTargetAutomated = isAutomated;
    }

    info = t;
    selected = isSelected;

    muteButton.setToggleState (t.muted, juce::dontSendNotification);
    soloButton.setToggleState (t.solo, juce::dontSendNotification);
    armButton.setToggleState (t.armed, juce::dontSendNotification);
    automationButton.setToggleState (t.automationShown, juce::dontSendNotification);
    setAutomatedLook (volume, t.automated.contains (automationTargets::volume));
    setAutomatedLook (pan, t.automated.contains (automationTargets::pan));

    // Don't fight the user: only move the slider if they aren't dragging it.
    if (! volume.isMouseButtonDown())
        volume.setValue (t.volume, juce::dontSendNotification);

    volume.setTooltip (juce::String (t.volumeDb, 1) + " dB");

    if (! pan.isMouseButtonDown())
        pan.setValue (t.pan, juce::dontSendNotification);

    const int panAmount = juce::roundToInt (std::abs (t.pan) * 100.0f);
    pan.setTooltip ("Pan: " + (panAmount == 0 ? juce::String ("center") : (t.pan < 0.0f ? "left " : "right ") + juce::String (panAmount))
                    + " (double-click to center)");
    pan.setColour (IkarosLookAndFeel::knobArcColourId, t.colour);

    const bool wasAudio = monitorButton.isVisible();
    const bool hadClipLight = clipLight.isVisible();
    monitorButton.setVisible (t.kind == TrackKind::audio);
    flexButton.setVisible (t.kind == TrackKind::audio);
    flexOn = engine.getTracks().flex().isFlexOn (t.id);   // not in TrackInfo: asked for here
    flexButton.setToggleState (flexOn, juce::dontSendNotification);
    monitorButton.setToggleState (t.monitoring, juce::dontSendNotification);
    clipLight.setVisible (t.kind == TrackKind::audio && t.armed);

    if (wasAudio != monitorButton.isVisible() || hadClipLight != clipLight.isVisible())
        resized();
    updateAccessibleNames();

    if (needsRepaint)
        repaint();
}

void TrackHeader::updateMeter()
{
    auto& tracks = engine.getTracks();

    if (clipLight.isVisible())
    {
        auto reading = tracks.inputs().getInputMeter (info.id);
        volume.setLevel (reading.level);
        clipLight.setClipped (reading.clipped);
    }
    else
    {
        volume.setLevel (tracks.getTrackLevel (info.id));
    }
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
    else if (info.automationShown && pickerArea.contains (e.getPosition()))
    {
        showAutomationMenu();
    }
}

void TrackHeader::mouseMove (const juce::MouseEvent& e)
{
    // The icon tile is a button: show it.
    const bool onButton = iconArea.contains (e.getPosition()) || (info.automationShown && pickerArea.contains (e.getPosition()));
    setMouseCursor (onButton ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
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

void TrackHeader::showAutomationMenu()
{
    // The menu's actions run after it closes, when this header may be gone
    // (the track deleted, say), so they only capture the engine and the id.
    auto& eng = engine;
    const auto id = info.id;
    auto& automation = engine.getTracks().automation();
    const auto current = info.automationTarget;

    auto makeItem = [&] (const AutomationTarget& a)
    {
        juce::PopupMenu::Item item (a.name);
        item.setTicked (a.id == current);

        // Settings with automation stand out, in the same colour as their controls.
        if (a.automated)
            item.setColour (colour::automated);

        item.setAction ([&eng, id, targetId = a.id] { eng.getTracks().automation().setLaneTarget (id, targetId); });
        return item;
    };

    juce::PopupMenu menu;
    std::map<juce::String, juce::PopupMenu> pluginMenus;   // a submenu per plug-in (they can have hundreds)
    juce::StringArray pluginOrder;
    juce::String lastGroup;

    for (auto& a : automation.getTargets (id))
    {
        if (a.group == "Mixer" || a.group == "Sound")
        {
            if (a.group != lastGroup)
                menu.addSectionHeader (a.group);

            lastGroup = a.group;
            menu.addItem (makeItem (a));
        }
        else
        {
            pluginOrder.addIfNotAlreadyThere (a.group);
            pluginMenus[a.group].addItem (makeItem (a));
        }
    }

    if (! pluginOrder.isEmpty())
    {
        menu.addSectionHeader ("Plug-ins");

        for (auto& name : pluginOrder)
            menu.addSubMenu (name, pluginMenus[name]);
    }

    const auto target = automation.getTarget (id, current);
    menu.addSeparator();
    menu.addItem (juce::PopupMenu::Item ("Delete All " + automationName + " Points")
                      .setEnabled (target && target->automated && target->editable)
                      .setAction ([&eng, id, current]
                      {
                          eng.beginUndoStep ("Delete Automation");
                          eng.getTracks().automation().setPoints (id, current, {});
                      }));
    menu.addItem (juce::PopupMenu::Item ("Hide Automation")
                      .setAction ([&eng, id] { eng.getTracks().automation().setLaneShown (id, false); }));

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this)
                                                  .withTargetScreenArea (localAreaToGlobal (pickerArea)));
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
    g.fillAll (selected ? colour::selectedBg : colour::sidebarBg);

    g.setColour (colour::divider);
    g.fillRect (r.removeFromBottom (1));

    // The selected track also gets a subtle 1px outline just inside its
    // edges (drawRect draws inside the rectangle it's given).
    if (selected)
    {
        g.setColour (colour::selectedInset);
        g.drawRect (r, 1);
    }

    // 3px colour edge on the left (over the outline's left side).
    g.setColour (info.colour);
    g.fillRect (r.removeFromLeft (3));

    // Icon tile
    g.setColour (selected ? colour::controlFill : colour::trackHeaderBg);
    g.fillRoundedRectangle (iconArea.toFloat(), size::radiusLarge);
    // The icon the user picked, or else the one the sound suggests, or else the category's.
    icons::draw (g, icons::forTrack (info.icon.isNotEmpty() ? info.icon : info.soundIcon, info.category),
                 iconArea.toFloat(), info.colour, 22.0f);

    g.setColour (info.silencedBySolo || info.muted ? colour::textSecondary : colour::text);
    g.setFont (selected ? fonts::uiBold (textSize::lg) : fonts::uiSemiBold (textSize::lg));
    g.drawText (info.name, nameArea, juce::Justification::centredLeft, true);

    g.setColour (colour::textSecondary);
    g.setFont (fonts::ui (textSize::sm));
    g.drawText (info.soundName, soundArea, juce::Justification::centredLeft, true);

    if (info.automationShown)
    {
        // The automation picker: the setting's name (amber if it has
        // automation, like its control) and a chevron.
        const auto pill = pickerArea.toFloat();
        g.setColour (colour::controlFill);
        g.fillRoundedRectangle (pill, size::radiusSmall);
        auto text = pill.reduced (8.0f, 0.0f);
        icons::draw (g, Icon::chevronDown, text.removeFromRight (12.0f), colour::textSecondary, 12.0f);

        // A dot in the curve's colour, so it's clear which curve this is.
        g.setColour (getAutomationColour (info.automationTarget));
        g.fillEllipse (text.removeFromLeft (8.0f).withSizeKeepingCentre (8.0f, 8.0f));
        text.removeFromLeft (6.0f);
        g.setColour (automationTargetAutomated ? colour::automated : colour::text);
        g.setFont (fonts::uiBold (textSize::sm));
        g.drawText (automationName, text, juce::Justification::centredLeft, true);
    }
}

void TrackHeader::resized()
{
    // Laid out from the top: when zooming makes the row taller, the
    // controls stay together and the lane gets the extra room.
    auto r = getLocalBounds().withTrimmedBottom (1).withTrimmedLeft (3).reduced (12, 6);

    iconArea = r.removeFromLeft (44).removeFromTop (44);
    r.removeFromLeft (12);

    auto top = r.removeFromTop (26);

    for (auto* b : { &armButton, &monitorButton, &soloButton, &muteButton, &automationButton })
    {
        if (b->isVisible())
        {
            b->setBounds (top.removeFromRight (26));
            top.removeFromRight (5);
        }
    }

    nameArea = top;
    nameEditor.setBounds (nameArea.expanded (2, 0));

    r.removeFromTop (4);
    soundArea = r.removeFromTop (18).withTrimmedRight (panSize);

    if (flexButton.isVisible())
    {
        flexButton.setBounds (soundArea.removeFromRight (40));
        soundArea.removeFromRight (6);
    }
    r.removeFromTop (6);

    // Pan knob at the end of the volume slider's row, centred on it. Its
    // bounds are taller than the row: the knob is drawn inset, and the extra
    // space is empty.
    auto volumeRow = r.removeFromTop (22);
    const auto volumeAndPan = volumeRow;
    pan.setBounds (volumeRow.removeFromRight (panSize).withSizeKeepingCentre (panSize, panSize));
    volumeRow.removeFromRight (6);

    if (clipLight.isVisible())
    {
        clipLight.setBounds (volumeRow.removeFromRight (8).withSizeKeepingCentre (8, 8));
        volumeRow.removeFromRight (5);
    }

    volume.setBounds (volumeRow);

    // Under them, as wide as both: the automation picker (while it shows).
    // The pan knob reaches a little below its row, hence the bigger gap.
    r.removeFromTop (10);
    pickerArea = r.removeFromTop (18).withX (volumeAndPan.getX()).withWidth (volumeAndPan.getWidth());
}

} // namespace ikaros
