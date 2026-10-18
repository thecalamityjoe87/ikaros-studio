#include "ui/toolbar/LcdDisplay.h"
#include "ui/theme/Fonts.h"
#include "ui/theme/IkarosLookAndFeel.h"
#include "ui/theme/Tokens.h"

namespace ikaros
{

using namespace tokens;

namespace
{
    // Rows inside the readout: the value on top, its caption under it.
    constexpr int valueRowHeight = 23;
    constexpr int fieldPaddingX = 7;

    // Column widths at the preferred size (see LcdDisplay::resized).
    constexpr int positionWidth = 76, tempoWidth = 72, timeWidth = 52, keyWidth = 80;

    constexpr double minBpm = 20.0, maxBpm = 300.0;   // the engine's limits too

    /** The contents of a popover. It's deleted when the popover closes,
        which is the one moment we can be sure it has closed. */
    class PopoverContent  : public juce::Component
    {
    public:
        ~PopoverContent() override
        {
            if (onDismissed)
                onDismissed();
        }

        std::function<void()> onDismissed;
    };

    /** Shows `content` in a CallOutBox: a small bubble with an arrow pointing
        at `target`. The box owns the content and deletes itself (and it) when
        dismissed, e.g. by clicking outside it. It's launched inside the
        window (the top-level component is its parent), so it uses our
        LookAndFeel and goes away with the window. launchAsynchronously
        doesn't block: this app runs without modal loops. */
    void showPopover (std::unique_ptr<PopoverContent> content, juce::Component& target)
    {
        // While the box is open it's modal, so `target` misses the mouse
        // leaving it and would keep its hover look: repaint it on closing.
        content->onDismissed = [safeTarget = juce::Component::SafePointer<juce::Component> (&target)]
        {
            if (safeTarget != nullptr)
                safeTarget->repaint();
        };

        if (auto* parent = target.getTopLevelComponent())
            juce::CallOutBox::launchAsynchronously (std::move (content),
                                                    parent->getLocalArea (&target, target.getLocalBounds()),
                                                    parent);
    }

    void dismissPopover (juce::Component& content)
    {
        if (auto* box = content.findParentComponentOfClass<juce::CallOutBox>())
            box->dismiss();
    }

    //==============================================================================
    /** The key picker: Major | Minor, then the twelve root notes. Picking a
        mode applies it straight away (keeping the root); picking a root
        applies it and closes the picker. */
    class KeyPicker  : public PopoverContent
    {
    public:
        KeyPicker (int rootIn, bool minorIn, std::function<void (int, bool)> onChangeIn)
            : root (rootIn), minor (minorIn), onChange (std::move (onChangeIn))
        {
            for (auto* b : { &majorButton, &minorButton })
            {
                b->setColours (IconButton::segmentColours());
                b->setCornerRadius (size::radiusSmall);
                b->setFont (fonts::uiSemiBold (textSize::sm));
                addAndMakeVisible (b);
            }

            majorButton.onClick = [this] { setMinor (false); };
            minorButton.onClick = [this] { setMinor (true); };

            for (int i = 0; i < 12; ++i)
            {
                // OwnedArray deletes the buttons when the picker goes away.
                auto* b = roots.add (new IconButton ("Key root"));
                b->setCornerRadius (size::radiusSmall);
                b->setFont (fonts::uiSemiBold (textSize::md));
                b->onClick = [this, i] { chooseRoot (i); };
                addAndMakeVisible (b);
            }

            // Opening the box gives keyboard focus to its first focusable
            // component. Taking it here keeps it off the first button, which
            // would otherwise show a focus ring as if it had been picked.
            setWantsKeyboardFocus (true);

            refresh();
            setSize (padding * 2 + columns * cellWidth + (columns - 1) * gap,
                     padding * 2 + segmentHeight + 8 + 2 * cellHeight + gap);
        }

        void paint (juce::Graphics& g) override
        {
            // The well behind the Major | Minor segments.
            g.setColour (colour::wellBg);
            g.fillRoundedRectangle (getSegmentArea().toFloat(), size::radiusXs + 2.0f);
        }

        void resized() override
        {
            auto segments = getSegmentArea().reduced (2);
            majorButton.setBounds (segments.removeFromLeft (segments.getWidth() / 2));
            minorButton.setBounds (segments);

            for (int i = 0; i < roots.size(); ++i)
                roots[i]->setBounds (padding + (i % columns) * (cellWidth + gap),
                                     padding + segmentHeight + 8 + (i / columns) * (cellHeight + gap),
                                     cellWidth, cellHeight);
        }

    private:
        static constexpr int padding = 10, gap = 4, columns = 6;
        static constexpr int cellWidth = 40, cellHeight = 28, segmentHeight = 28;

        int root;
        bool minor;
        std::function<void (int, bool)> onChange;

        IconButton majorButton { "Major", std::nullopt, "Major" };
        IconButton minorButton { "Minor", std::nullopt, "Minor" };
        juce::OwnedArray<IconButton> roots;

        juce::Rectangle<int> getSegmentArea() const
        {
            return getLocalBounds().reduced (padding).withHeight (segmentHeight);
        }

        void setMinor (bool m)
        {
            if (m == minor)
                return;

            minor = m;
            onChange (root, minor);
            refresh();
        }

        void chooseRoot (int r)
        {
            root = r;
            onChange (root, minor);
            dismissPopover (*this);
        }

        void refresh()
        {
            majorButton.setToggleState (! minor, juce::dontSendNotification);
            minorButton.setToggleState (minor, juce::dontSendNotification);

            for (int i = 0; i < roots.size(); ++i)
            {
                // The note name as the key is spelled in this mode ("Eb" in
                // major, "D#" in minor): getKeyName gives "Eb maj".
                const auto name = getKeyName (i, minor).upToFirstOccurrenceOf (" ", false, false);
                auto* b = roots[i];
                b->setText (name);
                b->setTitle (name + (minor ? " minor" : " major"));

                // The current root is marked like any selection: a lighter
                // fill with a grey border, never the accent.
                IconButton::Colours c { colour::raised, colour::textSecondary, colour::selectedBg, colour::text };
                c.border = i == root ? colour::selectedBorder : juce::Colours::transparentBlack;
                b->setColours (c);
                b->setToggleState (i == root, juce::dontSendNotification);
            }
        }
    };

    //==============================================================================
    /** The time-signature editor: beats per bar (− 4 +), then the beat value
        (2 | 4 | 8 | 16). Every change applies straight away. */
    class TimeSignatureEditor  : public PopoverContent
    {
    public:
        TimeSignatureEditor (int numIn, int denIn, std::function<void (int, int)> onChangeIn)
            : numerator (numIn), denominator (denIn), onChange (std::move (onChangeIn))
        {
            for (auto* b : { &fewerButton, &moreButton })
            {
                b->setColours (IconButton::neutralToggleColours());
                b->setCornerRadius (size::radiusSmall);
                b->setFont (fonts::uiSemiBold (textSize::lg));
                addAndMakeVisible (b);
            }

            fewerButton.onClick = [this] { setNumerator (numerator - 1); };
            moreButton.onClick  = [this] { setNumerator (numerator + 1); };

            for (int value : beatValues)
            {
                auto* b = denominatorButtons.add (new IconButton ("Beat value " + juce::String (value),
                                                                  std::nullopt, juce::String (value)));
                b->setColours (IconButton::segmentColours());
                b->setCornerRadius (size::radiusSmall);
                b->setFont (fonts::monoBold (textSize::md));
                b->onClick = [this, value] { setDenominator (value); };
                addAndMakeVisible (b);
            }

            setWantsKeyboardFocus (true);   // see KeyPicker

            refresh();
            setSize (248, padding * 2 + rowHeight * 2 + rowGap);
        }

        void paint (juce::Graphics& g) override
        {
            g.setColour (colour::textSecondary);
            g.setFont (fonts::ui (textSize::sm));
            g.drawText ("Beats per bar", getRow (0).withWidth (labelWidth), juce::Justification::centredLeft);
            g.drawText ("Beat value", getRow (1).withWidth (labelWidth), juce::Justification::centredLeft);

            g.setColour (colour::text);
            g.setFont (fonts::monoBold (textSize::xl));
            g.drawText (juce::String (numerator), numberArea, juce::Justification::centred);

            g.setColour (colour::wellBg);
            g.fillRoundedRectangle (getRow (1).withTrimmedLeft (labelWidth).toFloat(), size::radiusXs + 2.0f);
        }

        void resized() override
        {
            auto beats = getRow (0).withTrimmedLeft (labelWidth);
            fewerButton.setBounds (beats.removeFromLeft (rowHeight));
            moreButton.setBounds (beats.removeFromRight (rowHeight));
            numberArea = beats;

            auto values = getRow (1).withTrimmedLeft (labelWidth).reduced (2);
            const int w = values.getWidth() / denominatorButtons.size();

            for (auto* b : denominatorButtons)
                b->setBounds (values.removeFromLeft (w));
        }

    private:
        static constexpr int padding = 12, rowHeight = 28, rowGap = 10, labelWidth = 96;
        static constexpr int maxNumerator = 16;
        static constexpr int beatValues[] { 2, 4, 8, 16 };

        int numerator, denominator;
        std::function<void (int, int)> onChange;

        IconButton fewerButton { "Fewer beats", std::nullopt, juce::String::fromUTF8 ("\xe2\x88\x92") };   // "−"
        IconButton moreButton  { "More beats", std::nullopt, "+" };
        juce::OwnedArray<IconButton> denominatorButtons;
        juce::Rectangle<int> numberArea;

        juce::Rectangle<int> getRow (int index) const
        {
            return getLocalBounds().reduced (padding).withHeight (rowHeight)
                                   .translated (0, index * (rowHeight + rowGap));
        }

        void setNumerator (int n)
        {
            numerator = juce::jlimit (1, maxNumerator, n);
            onChange (numerator, denominator);
            refresh();
        }

        void setDenominator (int d)
        {
            denominator = d;
            onChange (numerator, denominator);
            refresh();
        }

        void refresh()
        {
            fewerButton.setEnabled (numerator > 1);
            moreButton.setEnabled (numerator < maxNumerator);

            for (int i = 0; i < denominatorButtons.size(); ++i)
                denominatorButtons[i]->setToggleState (beatValues[i] == denominator, juce::dontSendNotification);

            repaint();
        }
    };
}

//==============================================================================
LcdDisplay::Field::Field (const juce::String& accessibleName, const juce::String& captionText, bool chevron)
    : caption (captionText), showChevron (chevron)
{
    setTitle (accessibleName);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);

    // Repaint automatically when the mouse enters or leaves, for the hover state.
    setRepaintsOnMouseActivity (true);

    // Reachable with Tab, but a click doesn't take keyboard focus, so Space
    // still plays and stops after clicking a field (as with IconButton).
    setWantsKeyboardFocus (true);
    setMouseClickGrabsKeyboardFocus (false);

    if (showChevron)
        trailingWidth = 12;
}

void LcdDisplay::Field::setValueText (const juce::String& text)
{
    if (text != valueText)
    {
        valueText = text;
        repaint();
    }
}

juce::Rectangle<int> LcdDisplay::Field::getValueArea() const
{
    return getLocalBounds().reduced (fieldPaddingX, 0).withHeight (valueRowHeight).withTrimmedRight (trailingWidth);
}

void LcdDisplay::Field::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat();

    // A shade lighter than the readout; lighter still under the mouse.
    auto bg = colour::field;

    if (isMouseOverOrDragging (true))
        bg = bg.overlaidWith (colour::tintLight.withAlpha (0.06f));

    g.setColour (bg);
    g.fillRoundedRectangle (r, size::radiusSmall);

    const auto valueArea = getValueArea();
    g.setColour (colour::text);
    g.setFont (fonts::monoBold (textSize::xl));
    g.drawText (valueText, valueArea, juce::Justification::bottomLeft, false);

    if (showChevron)
        icons::draw (g, Icon::chevronDown,
                     valueArea.withX (valueArea.getRight()).withWidth (trailingWidth).toFloat().translated (0.0f, 2.0f),
                     colour::textSecondary, 12.0f, 2.0f);

    g.setColour (colour::lcdLabel);
    g.setFont (fonts::mono (textSize::caption).withExtraKerningFactor (0.1f));
    g.drawText (caption, getLocalBounds().reduced (fieldPaddingX, 0).withTrimmedTop (valueRowHeight).withTrimmedBottom (1),
                juce::Justification::centredLeft);

    if (hasKeyboardFocus (false))
        IkarosLookAndFeel::drawFocusRing (g, r, size::radiusSmall);
}

void LcdDisplay::Field::mouseUp (const juce::MouseEvent& e)
{
    // mouseWasClicked: released without having been dragged.
    if (e.mouseWasClicked() && getLocalBounds().contains (e.getPosition()) && onClick)
        onClick();
}

bool LcdDisplay::Field::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::returnKey && onClick)
    {
        onClick();
        return true;
    }

    return false;
}

//==============================================================================
LcdDisplay::TempoField::TempoField (LcdDisplay& o)
    : Field ("Tempo in BPM", "BPM", false), owner (o)
{
    trailingWidth = 12;   // the steppers
    setTooltip ("Tempo at the playhead: click to type, drag up or down, or scroll. "
                "Tempo changes are in the tempo lane.");

    editor.setFont (fonts::monoBold (textSize::xl));
    editor.setJustificationType (juce::Justification::bottomLeft);
    editor.setBorderSize ({ 0, 0, 0, 0 });
    editor.setColour (juce::Label::textColourId, colour::text);
    editor.setTitle ("Tempo in BPM");

    // The label itself ignores the mouse, so clicks and drags reach this
    // field; its text box (a child, shown while typing) still gets them.
    editor.setInterceptsMouseClicks (false, true);

    editor.onTextChange = [this]
    {
        const auto value = editor.getText().getDoubleValue();

        if (value >= minBpm && value <= maxBpm && owner.onBpmChanged)
            owner.onBpmChanged (value);
        else
            editor.setText (juce::String (bpm, 1), juce::dontSendNotification);
    };

    editor.setText (juce::String (bpm, 1), juce::dontSendNotification);
    addAndMakeVisible (editor);
}

void LcdDisplay::TempoField::setBpm (double newBpm)
{
    bpm = newBpm;

    if (! editor.isBeingEdited())
        editor.setText (juce::String (bpm, 1), juce::dontSendNotification);
}

juce::Rectangle<int> LcdDisplay::TempoField::getStepperArea (Part part) const
{
    auto column = getLocalBounds().withTrimmedRight (3).removeFromRight (trailingWidth + 2).reduced (0, 4);
    return part == Part::stepUp ? column.removeFromTop (column.getHeight() / 2) : column.withTrimmedTop (column.getHeight() / 2);
}

LcdDisplay::TempoField::Part LcdDisplay::TempoField::getPartAt (juce::Point<int> p) const
{
    if (getStepperArea (Part::stepUp).contains (p))
        return Part::stepUp;

    if (getStepperArea (Part::stepDown).contains (p))
        return Part::stepDown;

    return Part::value;
}

void LcdDisplay::TempoField::paint (juce::Graphics& g)
{
    Field::paint (g);   // the field and caption; the label draws the number

    for (auto part : { Part::stepUp, Part::stepDown })
    {
        const bool hot = part == hoveredPart && isMouseOver();
        icons::draw (g, part == Part::stepUp ? Icon::chevronUp : Icon::chevronDown,
                     getStepperArea (part).toFloat(), hot ? colour::text : colour::textSecondary, 11.0f, 2.0f);
    }
}

void LcdDisplay::TempoField::resized()
{
    editor.setBounds (getValueArea());
}

void LcdDisplay::TempoField::mouseMove (const juce::MouseEvent& e)
{
    const auto part = getPartAt (e.getPosition());

    if (part != hoveredPart)
    {
        hoveredPart = part;
        repaint();
    }
}

void LcdDisplay::TempoField::mouseExit (const juce::MouseEvent&)
{
    hoveredPart = Part::value;
    repaint();
}

void LcdDisplay::TempoField::mouseDown (const juce::MouseEvent& e)
{
    pressedPart = getPartAt (e.getPosition());
    dragStartBpm = bpm;
    dragging = false;

    if (pressedPart == Part::stepUp)
        nudge (1.0);
    else if (pressedPart == Part::stepDown)
        nudge (-1.0);
}

void LcdDisplay::TempoField::mouseDrag (const juce::MouseEvent& e)
{
    if (pressedPart != Part::value || editor.isBeingEdited())
        return;

    // Wait for a deliberate drag, so a click still opens the text box.
    if (! dragging && std::abs (e.getDistanceFromDragStartY()) < 3)
        return;

    if (! dragging)
    {
        dragging = true;

        if (owner.onBpmDragStarted)
            owner.onBpmDragStarted();
    }

    // Up is faster: 1 BPM per 3 pixels, or 0.1 BPM with Shift held.
    const bool fine = e.mods.isShiftDown();
    const double steps = -e.getDistanceFromDragStartY() / 3.0;
    auto newBpm = fine ? std::round ((dragStartBpm + steps * 0.1) * 10.0) / 10.0
                       : std::round (dragStartBpm + steps);
    newBpm = juce::jlimit (minBpm, maxBpm, newBpm);

    if (! juce::approximatelyEqual (newBpm, bpm))
    {
        setBpm (newBpm);

        if (owner.onBpmDragged)
            owner.onBpmDragged (newBpm);
    }
}

void LcdDisplay::TempoField::mouseUp (const juce::MouseEvent& e)
{
    if (dragging)
    {
        dragging = false;
        return;
    }

    if (pressedPart == Part::value && e.mouseWasClicked() && ! editor.isBeingEdited())
        editor.showEditor();
}

void LcdDisplay::TempoField::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
{
    if (! juce::approximatelyEqual (wheel.deltaY, 0.0f))
        nudge (wheel.deltaY > 0 ? 1.0 : -1.0);
}

bool LcdDisplay::TempoField::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::upKey || key == juce::KeyPress::downKey)
    {
        nudge (key == juce::KeyPress::upKey ? 1.0 : -1.0);
        return true;
    }

    if (key == juce::KeyPress::returnKey)
    {
        editor.showEditor();
        return true;
    }

    return false;
}

void LcdDisplay::TempoField::nudge (double amount)
{
    // Whole BPM steps: 120.5 goes to 121 or 119, then on by ones.
    const auto newBpm = juce::jlimit (minBpm, maxBpm, std::round (bpm) + amount);

    if (owner.onBpmChanged)
        owner.onBpmChanged (newBpm);
}

//==============================================================================
LcdDisplay::LcdDisplay()
{
    setTitle ("Playhead position and session settings");

    timeSigField.setTooltip ("Time signature at the playhead (click to change)");
    timeSigField.setValueText ("4/4");
    timeSigField.onClick = [this] { showTimeSignatureEditor(); };

    keyField.setTooltip ("Key (click to change)");
    keyField.setValueText ("C maj");
    keyField.onClick = [this] { showKeyPicker(); };

    for (auto* c : std::initializer_list<juce::Component*> { &tempoField, &timeSigField, &keyField })
        addAndMakeVisible (c);
}

void LcdDisplay::setState (const TransportState& s, const juce::String& key)
{
    // During a count-in, count down the beats left before recording starts: -4, -3...
    auto pos = s.countingIn ? "-" + juce::String ((int) std::ceil (s.recordStartBeats - s.positionBeats - 0.001))
                            : juce::String (s.bar).paddedLeft ('0', 2) + "." + juce::String (s.beat);

    if (pos != positionText)
    {
        positionText = pos;
        repaint (positionArea);
    }

    keyRoot = s.keyRoot;
    keyMinor = s.keyMinor;
    keyField.setValueText (key);

    if (! juce::approximatelyEqual (s.bpm, bpm))
    {
        bpm = s.bpm;
        tempoField.setBpm (bpm);
    }

    timeSigNum = s.timeSigNumerator;
    timeSigDen = s.timeSigDenominator;
    timeSigField.setValueText (juce::String (timeSigNum) + "/" + juce::String (timeSigDen));
}

void LcdDisplay::paint (juce::Graphics& g)
{
    g.setColour (colour::lcdBg);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), size::radiusLarge);

    // The position isn't editable, so it has no field: same rows as the fields.
    g.setColour (colour::text);
    g.setFont (fonts::monoBold (textSize::lcd));
    g.drawText (positionText, positionArea.withHeight (valueRowHeight), juce::Justification::bottomLeft, false);

    g.setColour (colour::lcdLabel);
    g.setFont (fonts::mono (textSize::caption).withExtraKerningFactor (0.1f));
    g.drawText (juce::String::fromUTF8 ("BAR \xc2\xb7 BEAT"),
                positionArea.withTrimmedTop (valueRowHeight).withTrimmedBottom (1), juce::Justification::centredLeft);
}

void LcdDisplay::resized()
{
    auto r = getLocalBounds().reduced (12, 3);

    // At the preferred width there's 12px after the position and 6px between
    // fields. In a narrow window the gaps shrink first, then the position.
    const int content = positionWidth + tempoWidth + timeWidth + keyWidth;
    const int spare = r.getWidth() - content;
    const int bigGap = juce::jlimit (0, 12, spare / 3);
    const int smallGap = juce::jlimit (0, 6, spare / 6);
    const int position = positionWidth - juce::jmax (0, -spare);

    positionArea = r.removeFromLeft (position);
    r.removeFromLeft (bigGap);
    tempoField.setBounds (r.removeFromLeft (tempoWidth));
    r.removeFromLeft (smallGap);
    timeSigField.setBounds (r.removeFromLeft (timeWidth));
    r.removeFromLeft (smallGap);
    keyField.setBounds (r.removeFromLeft (keyWidth));
}

void LcdDisplay::showTimeSignatureEditor()
{
    // Changes the time signature in effect at the playhead (see AudioEngine::setTimeSignature).
    // SafePointer becomes null if this component is deleted while the editor is open.
    auto editor = std::make_unique<TimeSignatureEditor> (timeSigNum, timeSigDen,
        [safeThis = juce::Component::SafePointer<LcdDisplay> (this)] (int num, int den)
        {
            if (safeThis != nullptr && safeThis->onTimeSignatureChanged)
                safeThis->onTimeSignatureChanged (num, den);
        });

    showPopover (std::move (editor), timeSigField);
}

void LcdDisplay::showKeyPicker()
{
    auto picker = std::make_unique<KeyPicker> (keyRoot, keyMinor,
        [safeThis = juce::Component::SafePointer<LcdDisplay> (this)] (int root, bool minor)
        {
            if (safeThis != nullptr && safeThis->onKeyChanged)
                safeThis->onKeyChanged (root, minor);
        });

    showPopover (std::move (picker), keyField);
}

} // namespace ikaros
