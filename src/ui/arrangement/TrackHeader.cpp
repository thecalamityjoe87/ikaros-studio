#include "ui/arrangement/TrackHeader.h"
#include "ui/theme/Fonts.h"
#include "ui/theme/IkarosLookAndFeel.h"
#include "ui/theme/Tokens.h"
#include "ui/theme/TrackColours.h"
#include "ui/widgets/AutomationHint.h"
#include "ui/widgets/Decibels.h"
#include "ui/dialogs/MonitoringWarning.h"
#include <map>
#include <tuple>

namespace ikaros
{

using namespace tokens;

namespace
{
    // The header's layout (see the picture in TrackHeader.h).
    constexpr int edgeWidth = 3;          // the track-colour edge on the left
    constexpr int topPadding = 10;
    constexpr int tileSize = 32;          // the icon tile
    constexpr int tileGap = 10;           // between the tile and the rows beside it
    constexpr int topRowHeight = 24;      // name, Flex, arm
    constexpr int nameHeight = 20;        // the name sits at the top of that row...
    constexpr int soundRowY = 31;         // ...right above sound · dB (measured from the top)
    constexpr int soundRowHeight = 14;
    constexpr int bottomRowY = 51;        // the buttons, volume and pan
    constexpr int bottomRowHeight = 22;
    constexpr int smallButtonWidth = 26;  // Mute, Solo, Monitor, Automation
    constexpr int smallButtonGap = 4;
    constexpr int roundSize = TrackHeader::controlColumnWidth;   // the column the arm button and the pan knob under it share
    constexpr int armSize = 24;   // the arm button: as wide as the pan knob's ring (not its whole box), so they look the same size
    constexpr int flexWidth = 38, flexHeight = 22;   // as tall as the Monitor button beside it

    // The text a slider's value bubble shows while it's dragged.
    juce::String panText (double pan)
    {
        const int amount = juce::roundToInt (std::abs (pan) * 100.0);
        return amount == 0 ? juce::String ("C") : (pan < 0.0 ? "L " : "R ") + juce::String (amount);
    }
}

TrackHeader::TrackHeader (AudioEngine& e, Selection& s, const TrackInfo& t)
    : engine (e), selection (s), info (t)
{
    setFocusContainerType (juce::Component::FocusContainerType::focusContainer);

    // The small square buttons: grey with a muted icon, filled when on.
    // (borderOn = the fill colour: no grey ring around an "on" button.)
    auto setupToggle = [this] (IconButton& b, juce::Colour onColour, juce::Colour onIconColour)
    {
        b.setColours ({ colour::buttonFill, colour::buttonIcon, onColour, onIconColour, colour::buttonBorder,
                        std::nullopt, std::nullopt, onColour });
        b.setCornerRadius (size::radiusSmall);
        b.setIconSize (14.0f);
        addAndMakeVisible (b);
    };

    setupToggle (muteButton, colour::accent, colour::textOnAccent);   // a muted track's Mute is "on"
    setupToggle (soloButton, colour::solo, colour::textOnAccent);
    setupToggle (automationButton, colour::accent, colour::textOnAccent);   // while the track's automation shows
    setupToggle (monitorButton, colour::effectOn, colour::textOnAccent);    // same green as the Recording inspector's
    monitorButton.setVisible (false);   // audio tracks only (see update)

    muteButton.setTooltip ("Mute");
    soloButton.setTooltip ("Solo");
    monitorButton.setTooltip ("Input monitoring");
    automationButton.setTooltip ("Automation");
    armButton.setTooltip ("Record arm");
    flexButton.setTooltip ("Flex Time");

    // Record-arm: a round button with the same grey fill and border as the
    // small buttons beside it, and a grey dot. Armed, it fills red with a
    // white dot and keeps the border. Half its size as the corner radius
    // makes the rounded rectangle a circle.
    armButton.setColours ({ colour::buttonFill, colour::buttonIcon, colour::record, colour::textOnColour,
                            colour::buttonBorder });
    armButton.setCornerRadius (armSize * 0.5f);
    armButton.setIconSize (10.0f);
    armButton.onClick = [this]
    {
        selection.selectTrack (info.id);
        engine.getTracks().inputs().setArmed (info.id, ! info.armed);
    };
    addAndMakeVisible (armButton);

    monitorButton.onClick = [this] { setMonitoringWithWarning (engine, info.id, ! info.monitoring, this); };

    // Flex (audio tracks): amber while it's on.
    flexButton.setColours ({ colour::buttonFill, colour::buttonIcon, colour::accent, colour::textOnAccent, colour::buttonBorder,
                             std::nullopt, std::nullopt, colour::accent });
    flexButton.setCornerRadius (size::radiusXs);
    flexButton.setFont (fonts::uiSemiBold (textSize::xs));
    flexButton.onClick = [this]
    {
        selection.selectTrack (info.id);
        engine.getTracks().flex().setFlexOn (info.id, ! flexOn);
    };
    addChildComponent (flexButton);

    clipLight.onClear = [this] { engine.getTracks().inputs().clearClipIndicator (info.id); };
    addChildComponent (clipLight);

    // Automation: opens the lane under the track.
    automationButton.onClick = [this]
    {
        selection.selectTrack (info.id);
        engine.getTracks().automation().setLaneShown (info.id, ! info.automationShown);
    };

    muteButton.onClick = [this] { engine.getTracks().setMute (info.id, ! info.muted); };
    soloButton.onClick = [this] { engine.getTracks().setSolo (info.id, ! info.solo); };

    volume.setVelocityModeParameters (1.0, 1, 0.0, false);
    volume.setDoubleClickReturnValue (true, TrackController::getFaderPositionForDb (0.0f));
    // One undo step per drag: start a transaction when the mouse goes down.
    volume.onDragStart = [this] { engine.beginUndoStep ("Volume"); selection.selectTrack (info.id); };
    volume.onValueChange = [this] { engine.getTracks().setVolume (info.id, (float) volume.getValue()); };
    // What the value bubble shows while it's dragged (see parentHierarchyChanged).
    volume.textFromValueFunction = [] (double v) { return decibelText (TrackController::getDbForFaderPosition ((float) v)); };
    addAndMakeVisible (volume);

    // Pan: a small knob, centre at 12 o'clock. Double-click centres it.
    pan.setRange (-1.0, 1.0);
    pan.setDoubleClickReturnValue (true, 0.0);
    pan.getProperties().set ("bipolar", true);   // read by IkarosLookAndFeel::drawRotarySlider
    pan.getProperties().set ("compact", true);   // the small knob style
    pan.onDragStart = [this] { engine.beginUndoStep ("Pan"); selection.selectTrack (info.id); };
    pan.onValueChange = [this] { engine.getTracks().setPan (info.id, (float) pan.getValue()); };
    pan.textFromValueFunction = [] (double v) { return panText (v); };
    addAndMakeVisible (pan);

    // Automated, they move with the automation, and a grab only moves them
    // until they're let go (see followAutomation).
    followAutomation (volume, [this] { return info.automated.contains (automationTargets::volume); },
                      [this] { showAutomationHint (engine, volume, info.id, automationTargets::volume); });
    followAutomation (pan, [this] { return info.automated.contains (automationTargets::pan); },
                      [this] { showAutomationHint (engine, pan, info.id, automationTargets::pan); });

    // The rename field: hidden until startRenaming(). Return keeps the new
    // name, Escape cancels, clicking elsewhere keeps it.
    nameEditor.setFont (fonts::uiBold (textSize::md));
    nameEditor.setIndents (4, 2);
    nameEditor.setJustification (juce::Justification::centredLeft);
    nameEditor.setTitle ("Track name");
    nameEditor.onReturnKey = [this] { finishRenaming (true); };
    nameEditor.onEscapeKey = [this] { finishRenaming (false); };
    nameEditor.onFocusLost = [this] { finishRenaming (true); };
    addChildComponent (nameEditor);

    update (t, false);
}

void TrackHeader::parentHierarchyChanged()
{
    // While volume or pan is dragged, a bubble beside it shows the value.
    // JUCE puts the bubble inside the component given here (the window's
    // top-level component), or else in a window of its own, which some Linux
    // window managers draw badly. The header only knows its window once it's
    // been added to one, which is why this is done here and not in the
    // constructor.
    auto* top = getTopLevelComponent();
    const bool inWindow = top != this;
    volume.setPopupDisplayEnabled (inWindow, false, inWindow ? top : nullptr);
    pan.setPopupDisplayEnabled (inWindow, false, inWindow ? top : nullptr);
}

void TrackHeader::updateAccessibleNames()
{
    // The tooltips are short ("Mute"); what a screen reader says names the track.
    muteButton.setTitle ((info.muted ? "Unmute " : "Mute ") + info.name);
    soloButton.setTitle ((info.solo ? "Unsolo " : "Solo ") + info.name);
    armButton.setTitle ((info.armed ? "Disarm " : "Arm ") + info.name + " for recording");
    automationButton.setTitle ((info.automationShown ? "Hide automation for " : "Show automation for ") + info.name);
    monitorButton.setTitle ((info.monitoring ? "Stop monitoring " : "Monitor ") + info.name);
    flexButton.setTitle ((flexOn ? "Turn off Flex for " : "Turn on Flex for ") + info.name);
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
    auto drawn = [] (const TrackInfo& i) { return std::tie (i.name, i.soundName, i.volumeDb, i.icon, i.soundIcon, i.category, i.kind,
                                                            i.muted, i.silencedBySolo, i.automationShown, i.automationTarget); };
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
    muteButton.setIcon (t.muted ? Icon::mute : Icon::speaker);
    soloButton.setToggleState (t.solo, juce::dontSendNotification);
    armButton.setToggleState (t.armed, juce::dontSendNotification);
    automationButton.setToggleState (t.automationShown, juce::dontSendNotification);
    setAutomatedLook (volume, t.automated.contains (automationTargets::volume));
    setAutomatedLook (pan, t.automated.contains (automationTargets::pan));

    // Don't fight the user: only move the slider if they aren't dragging it.
    if (! volume.isMouseButtonDown())
        volume.setValue (t.volume, juce::dontSendNotification);

    volume.setTooltip ("Volume: " + decibelText (t.volumeDb) + " (double-click for 0 dB)");

    if (! pan.isMouseButtonDown())
        pan.setValue (t.pan, juce::dontSendNotification);

    const int panAmount = juce::roundToInt (std::abs (t.pan) * 100.0f);
    pan.setTooltip ("Pan: " + (panAmount == 0 ? juce::String ("center") : (t.pan < 0.0f ? "left " : "right ") + juce::String (panAmount))
                    + " (double-click to center)");
    pan.setColour (IkarosLookAndFeel::knobArcColourId, colour::forTrack (t));

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
                          .setImage (icons::createDrawable (choice.icon, colour::forTrack (*track)))
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
    // Unselected headers are see-through: the column's background shows.
    auto r = getLocalBounds();

    g.setColour (colour::trackRowLine);
    g.fillRect (r.removeFromBottom (1));

    if (selected)
    {
        // The selected track: a lighter background and a subtle 1px outline
        // just inside its edges (drawRect draws inside the rectangle it's given).
        g.setColour (colour::selectedBg);
        g.fillRect (r);
        g.setColour (colour::selectedInset);
        g.drawRect (r, 1);
    }

    // The colour edge on the left (over the outline's left side).
    const auto trackColour = colour::forTrack (info);
    g.setColour (trackColour);
    g.fillRect (r.removeFromLeft (edgeWidth));

    // Icon tile: tinted with the track colour. The icon the user picked, or
    // else the one the sound suggests, or else the category's.
    g.setColour (trackColour.withAlpha (colour::trackTileAlpha));
    g.fillRoundedRectangle (iconArea.toFloat(), size::radiusMedium);
    icons::draw (g, icons::forTrack (info.icon.isNotEmpty() ? info.icon : info.soundIcon, info.category),
                 iconArea.toFloat(), trackColour, 18.0f);

    // The name, cut short with "..." if it doesn't fit.
    g.setColour (info.silencedBySolo || info.muted ? colour::textSecondary : colour::text);
    g.setFont (selected ? fonts::uiBold (textSize::md) : fonts::uiSemiBold (textSize::md));
    g.drawText (info.name, nameArea, juce::Justification::centredLeft, true);

    // "Sound name · −3.0 dB": the sound gives way (with "...") if there
    // isn't room, the volume always shows.
    {
        const auto nameFont = fonts::ui (textSize::xs);
        const auto dbFont = fonts::mono (textSize::xs);
        const auto db = decibelText (info.volumeDb);
        const juce::String separator (juce::String::fromUTF8 (" \xc2\xb7 "));   // " · "

        auto row = soundArea.toFloat();
        const float dbWidth = juce::GlyphArrangement::getStringWidth (dbFont, db);
        const float separatorWidth = juce::GlyphArrangement::getStringWidth (nameFont, separator);
        const float soundWidth = juce::jmin (juce::GlyphArrangement::getStringWidth (nameFont, info.soundName) + 1.0f,
                                             row.getWidth() - dbWidth - separatorWidth);

        g.setColour (colour::textSecondary);
        g.setFont (nameFont);
        g.drawText (info.soundName, row.removeFromLeft (juce::jmax (0.0f, soundWidth)), juce::Justification::centredLeft, true);
        g.drawText (separator, row.removeFromLeft (separatorWidth), juce::Justification::centredLeft, false);
        g.setFont (dbFont);
        g.drawText (db, row, juce::Justification::centredLeft, false);
    }

    if (info.automationShown)
    {
        // The automation picker: the setting's name (amber if it has
        // automation, like its control) and a chevron.
        const auto pill = pickerArea.toFloat();
        g.setColour (colour::buttonFill);
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
    // Laid out from the top: when zooming (or Tall Tracks) makes the row
    // taller, the controls stay together and the lane gets the extra room.
    // Left and right edges have the column's padding, so the tile lines up
    // with "TRACKS" above, and the arm button and pan knob sit in one column
    // with the tracks bar's "+" and the Sections row's flag button.
    const int padding = size::panelPadding;
    const int right = getWidth() - padding;

    iconArea = { padding, topPadding, tileSize, tileSize };
    const int left = iconArea.getRight() + tileGap;   // the three rows start here

    // Top row: name ... [Flex] [~] (arm). Flex and Monitor are audio tracks
    // only; they sit up here so the volume slider below is as long on
    // every track.
    auto top = juce::Rectangle<int> (left, topPadding, right - left, topRowHeight);
    armButton.setBounds (top.removeFromRight (roundSize).withSizeKeepingCentre (armSize, armSize));   // centred over the pan knob
    top.removeFromRight (8);

    if (monitorButton.isVisible())
    {
        monitorButton.setBounds (top.removeFromRight (smallButtonWidth).withSizeKeepingCentre (smallButtonWidth, bottomRowHeight));
        top.removeFromRight (smallButtonGap);
    }

    if (flexButton.isVisible())
    {
        flexButton.setBounds (top.removeFromRight (flexWidth).withSizeKeepingCentre (flexWidth, flexHeight));
        top.removeFromRight (8);
    }

    // The name and the sound line under it read as one block beside the
    // icon tile, so the name sits high in its row.
    nameArea = top.withHeight (nameHeight);
    nameEditor.setBounds (nameArea.expanded (2, 0));

    // Middle row: sound · dB. It stops short of the pan knob, which is
    // taller than the bottom row and reaches up beside it.
    soundArea = { left, soundRowY, right - left - roundSize - 8, soundRowHeight };

    // Bottom row: [M][S][⟋] ─── volume ───  (pan)
    auto bottom = juce::Rectangle<int> (left, bottomRowY, right - left, bottomRowHeight);
    const auto bottomRow = bottom;

    for (auto* b : { &muteButton, &soloButton, &automationButton })
    {
        b->setBounds (bottom.removeFromLeft (smallButtonWidth));
        bottom.removeFromLeft (smallButtonGap);
    }

    // The pan knob shares the arm button's column, centred under it. Its
    // ring is as wide as the arm button; the box around it is the column.
    pan.setBounds (bottom.removeFromRight (roundSize).withSizeKeepingCentre (roundSize, roundSize));
    bottom.removeFromRight (16);

    if (clipLight.isVisible())
        clipLight.setBounds (juce::Rectangle<int> (bottom.getRight() + 4, bottom.getY(), 8, bottom.getHeight()).withSizeKeepingCentre (8, 8));

    bottom.removeFromLeft (12 - smallButtonGap);
    volume.setBounds (bottom);

    // Under the bottom row, as wide as the rows: the automation picker
    // (while it shows).
    pickerArea = bottomRow.withY (bottomRow.getBottom() + 4).withHeight (18);
}

} // namespace ikaros
