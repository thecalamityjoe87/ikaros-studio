#include "ui/arrangement/TrackHeader.h"
#include "ui/theme/Fonts.h"
#include "ui/theme/Tokens.h"

namespace ikaros
{

using namespace tokens;

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

    muteButton.onClick = [this] { engine.getTracks().setMute (info.id, ! info.muted); };
    soloButton.onClick = [this] { engine.getTracks().setSolo (info.id, ! info.solo); };

    volume.setRange (0.0, 1.0);
    volume.setVelocityModeParameters (1.0, 1, 0.0, false);
    volume.setColour (juce::Slider::trackColourId, colour::accent);
    // One undo step per drag: start a transaction when the mouse goes down.
    volume.onDragStart = [this] { engine.beginUndoStep ("Volume"); selection.selectTrack (info.id); };
    volume.onValueChange = [this] { engine.getTracks().setVolume (info.id, (float) volume.getValue()); };
    addAndMakeVisible (volume);

    update (t, false);
}

void TrackHeader::updateAccessibleNames()
{
    muteButton.setTitle ("Mute " + info.name);
    muteButton.setTooltip ("Mute " + info.name);
    soloButton.setTitle ("Solo " + info.name);
    soloButton.setTooltip ("Solo " + info.name);
    volume.setTitle (info.name + " volume");
    setTitle (info.name + " track");
}

void TrackHeader::update (const TrackInfo& t, bool isSelected)
{
    info = t;
    selected = isSelected;

    muteButton.setToggleState (t.muted, juce::dontSendNotification);
    soloButton.setToggleState (t.solo, juce::dontSendNotification);

    // Don't fight the user: only move the slider if they aren't dragging it.
    if (! volume.isMouseButtonDown())
        volume.setValue (t.volume, juce::dontSendNotification);

    volume.setTooltip (juce::String (t.volumeDb, 1) + " dB");
    volume.setColour (juce::Slider::trackColourId, t.colour);
    updateAccessibleNames();
    repaint();
}

void TrackHeader::mouseDown (const juce::MouseEvent&)
{
    selection.selectTrack (info.id);
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
    icons::draw (g, icons::forCategory (info.category), iconArea.toFloat(), info.colour, 22.0f);

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
    soloButton.setBounds (top.removeFromRight (24));
    top.removeFromRight (4);
    muteButton.setBounds (top.removeFromRight (24));
    top.removeFromRight (4);
    nameArea = top;

    r.removeFromTop (2);
    soundArea = r.removeFromTop (16);
    r.removeFromTop (2);
    volume.setBounds (r.removeFromTop (16));
}

} // namespace ikaros
