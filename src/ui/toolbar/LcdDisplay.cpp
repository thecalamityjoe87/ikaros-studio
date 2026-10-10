#include "ui/toolbar/LcdDisplay.h"
#include "ui/theme/Fonts.h"
#include "ui/theme/Tokens.h"

namespace ikaros
{

using namespace tokens;

LcdDisplay::LcdDisplay()
{
    setTitle ("Playhead position and session settings");

    bpmLabel.setFont (fonts::monoBold (15.0f));
    bpmLabel.setJustificationType (juce::Justification::centredLeft);
    bpmLabel.setBorderSize ({ 0, 0, 0, 0 });
    bpmLabel.setColour (juce::Label::textColourId, colour::text);
    bpmLabel.setColour (juce::Label::textWhenEditingColourId, colour::text);
    bpmLabel.setColour (juce::Label::backgroundWhenEditingColourId, colour::raised);
    bpmLabel.setColour (juce::Label::outlineWhenEditingColourId, colour::accent);
    bpmLabel.setEditable (true, true, false);   // single click edits, losing focus commits
    bpmLabel.setTitle ("Tempo in BPM");
    bpmLabel.setTooltip ("Tempo (click to type, or scroll)");
    bpmLabel.setMouseCursor (juce::MouseCursor::IBeamCursor);
    bpmLabel.onTextChange = [this]
    {
        auto value = bpmLabel.getText().getDoubleValue();

        if (value >= 20.0 && value <= 300.0 && onBpmChanged)
            onBpmChanged (value);
        else
            bpmLabel.setText (juce::String (bpm, 1), juce::dontSendNotification);
    };
    addAndMakeVisible (bpmLabel);

    timeSigButton.setColours ({ juce::Colours::transparentBlack, colour::text,
                                juce::Colours::transparentBlack, colour::text });
    timeSigButton.setFont (fonts::monoBold (15.0f));
    timeSigButton.setText ("4/4");
    timeSigButton.setCornerRadius (4.0f);
    timeSigButton.onClick = [this] { showTimeSignatureMenu(); };
    addAndMakeVisible (timeSigButton);
}

void LcdDisplay::setState (const TransportState& s, const juce::String& key)
{
    auto pos = juce::String (s.bar).paddedLeft ('0', 2) + "." + juce::String (s.beat);
    bool changed = pos != positionText || key != keyText;
    positionText = pos;
    keyText = key;

    if (! juce::approximatelyEqual (s.bpm, bpm))
    {
        bpm = s.bpm;

        if (! bpmLabel.isBeingEdited())
            bpmLabel.setText (juce::String (bpm, 1), juce::dontSendNotification);
    }

    if (s.timeSigNumerator != timeSigNum || s.timeSigDenominator != timeSigDen)
    {
        timeSigNum = s.timeSigNumerator;
        timeSigDen = s.timeSigDenominator;
        timeSigButton.setText (juce::String (timeSigNum) + "/" + juce::String (timeSigDen));
    }

    if (bpmLabel.getText().isEmpty())
        bpmLabel.setText (juce::String (bpm, 1), juce::dontSendNotification);

    if (changed)
        repaint();
}

void LcdDisplay::paintCaption (juce::Graphics& g, juce::Rectangle<int> area, const juce::String& text)
{
    g.setColour (colour::lcdLabel);
    g.setFont (fonts::mono (9.0f).withExtraKerningFactor (0.1f));
    g.drawText (text, area.removeFromBottom (12), juce::Justification::topLeft);
}

void LcdDisplay::paint (juce::Graphics& g)
{
    g.setColour (colour::lcdBg);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), size::radiusLarge);

    g.setColour (colour::accent);
    g.setFont (fonts::monoBold (22.0f));
    g.drawText (positionText, positionArea.withTrimmedBottom (12), juce::Justification::bottomLeft);
    paintCaption (g, positionArea, juce::String::fromUTF8 ("BAR \xc2\xb7 BEAT"));

    paintCaption (g, bpmArea, "BPM");
    paintCaption (g, timeArea, "TIME");

    g.setColour (colour::text);
    g.setFont (fonts::monoBold (15.0f));
    g.drawText (keyText, keyArea.withTrimmedBottom (12), juce::Justification::bottomLeft);
    paintCaption (g, keyArea, "KEY");
}

void LcdDisplay::resized()
{
    auto r = getLocalBounds().reduced (18, 4);
    const int gap = 18;

    positionArea = r.removeFromLeft (78);   r.removeFromLeft (gap);
    bpmArea      = r.removeFromLeft (56);   r.removeFromLeft (gap);
    timeArea     = r.removeFromLeft (40);   r.removeFromLeft (gap);
    keyArea      = r;

    bpmLabel.setBounds (bpmArea.withTrimmedBottom (12).withTrimmedTop (4));
    timeSigButton.setBounds (timeArea.withTrimmedBottom (12).withTrimmedTop (4).withWidth (40));
}

void LcdDisplay::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    // Scroll over the BPM to nudge it by 1.
    if (bpmArea.contains (e.getPosition()) && onBpmChanged)
        onBpmChanged (juce::jlimit (20.0, 300.0, std::round (bpm) + (wheel.deltaY > 0 ? 1.0 : -1.0)));
}

void LcdDisplay::showTimeSignatureMenu()
{
    const std::array<std::pair<int, int>, 5> sigs {{ { 2, 4 }, { 3, 4 }, { 4, 4 }, { 5, 4 }, { 6, 8 } }};

    juce::PopupMenu m;

    for (size_t i = 0; i < sigs.size(); ++i)
        m.addItem ((int) i + 1, juce::String (sigs[i].first) + "/" + juce::String (sigs[i].second), true,
                   sigs[i].first == timeSigNum && sigs[i].second == timeSigDen);

    // showMenuAsync: menus must not block the message loop (modal loops are
    // disabled in this app). The lambda runs when the user picks an item.
    // SafePointer becomes null if this component is deleted before then.
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&timeSigButton),
                     [safeThis = juce::Component::SafePointer<LcdDisplay> (this), sigs] (int result)
                     {
                         if (safeThis != nullptr && result > 0 && safeThis->onTimeSignatureChanged)
                             safeThis->onTimeSignatureChanged (sigs[(size_t) result - 1].first,
                                                               sigs[(size_t) result - 1].second);
                     });
}

} // namespace ikaros
