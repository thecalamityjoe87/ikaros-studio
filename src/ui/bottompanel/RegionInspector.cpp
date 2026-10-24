#include "ui/bottompanel/RegionInspector.h"
#include "engine/AudioEngine.h"
#include "engine/TrackController.h"
#include "ui/widgets/IconButton.h"
#include "ui/theme/Fonts.h"
#include "ui/theme/IkarosLookAndFeel.h"
#include <array>

namespace ikaros
{

using namespace tokens;

namespace
{
    constexpr int rowHeight = 30;
    constexpr int rowGap = 8;
    constexpr int padding = 14;
    constexpr int labelWidth = 64;

    const juce::String minusSign = juce::String::fromUTF8 ("\xe2\x88\x92");
}

//==============================================================================
// The "Audio" section: an audio region's gain, pitch, direction and Flex.
class RegionInspector::AudioControls  : public juce::Component
{
public:
    explicit AudioControls (RegionInspector& o) : owner (o)
    {
        gain.setTitle ("Region gain");
        gain.setTooltip ("The region's own volume. Double-click for 0 dB");
        gain.setRange (RegionEditor::minGainDb, RegionEditor::maxGainDb, 0.1);
        gain.setDoubleClickReturnValue (true, 0.0);
        gain.setScrollWheelEnabled (false);   // every notch would be an undo step of its own

        // A whole drag is one undo step: it starts on mouse-down, and every
        // change until mouse-up goes into it (see AudioEngine::beginUndoStep).
        gain.onDragStart = [this]
        {
            dragging = true;
            owner.engine.beginUndoStep ("Region Gain");
        };
        gain.onDragEnd = [this] { dragging = false; };
        gain.onValueChange = [this]
        {
            if (! owner.region.has_value())
                return;

            // A double-click (back to 0 dB) isn't a drag: it's a step of its own.
            if (! dragging)
                owner.engine.beginUndoStep ("Region Gain");

            owner.engine.getTracks().regions().setRegionGain (owner.region->id, (float) gain.getValue());
            repaint();
        };
        addAndMakeVisible (gain);

        for (auto* b : { &pitchDown, &pitchUp })
        {
            b->setColours (IconButton::borderedColours);
            b->setFont (fonts::uiBold (textSize::xl));
            b->setCornerRadius (size::radiusSmall);
            addAndMakeVisible (b);
        }

        pitchDown.setText (minusSign);
        pitchDown.setTooltip ("Move the region down a semitone (it keeps its length)");
        pitchUp.setTooltip ("Move the region up a semitone (it keeps its length)");
        pitchDown.onClick = [this] { transposeBy (-1); };
        pitchUp.onClick = [this] { transposeBy (1); };

        // Like the Monitoring button: green while it's on.
        reverse.setColours (IconButton::powerColours);
        reverse.setFont (fonts::uiSemiBold (textSize::sm));
        reverse.setCornerRadius (6.0f);
        reverse.setTooltip ("Play the region backwards");
        reverse.onClick = [this]
        {
            if (owner.region.has_value())
                owner.engine.getTracks().regions().setRegionReversed (owner.region->id, ! owner.region->reversed);
        };
        addAndMakeVisible (reverse);

        // Flex belongs to the track (as in GarageBand), so this switches it
        // for every region on the region's track. Amber while it's on, like
        // the track header's Flex button.
        flex.setColours ([] { return IconButton::Colours { theme().controlFill, theme().textSecondary, theme().accent, theme().textOnAccent }; });
        flex.setFont (fonts::uiSemiBold (textSize::sm));
        flex.setCornerRadius (6.0f);
        flex.onClick = [this]
        {
            if (owner.region.has_value())
                owner.engine.getTracks().flex().setFlexOn (owner.region->trackId, ! flexInfo.trackFlex);
        };
        addAndMakeVisible (flex);

        resetFlex.setColours (IconButton::borderedColours);
        resetFlex.setFont (fonts::ui (textSize::sm));
        resetFlex.setCornerRadius (size::radiusSmall);
        resetFlex.setTooltip ("Put every hit back where it was recorded");
        resetFlex.onClick = [this]
        {
            if (owner.region.has_value())
                owner.engine.getTracks().flex().resetFlex (owner.region->id);
        };
        addAndMakeVisible (resetFlex);
    }

    /** Shows the region's settings (it's an audio region). */
    void refresh (const RegionInfo& r, Family family)
    {
        // dontSendNotification: showing the region's value isn't a change to
        // make. (Not during a drag: the drag is ahead of the session.)
        if (! dragging)
            gain.setValue (r.gainDb, juce::dontSendNotification);

        IkarosLookAndFeel::setFamilyColour (gain, family);
        pitchDown.setEnabled (r.transposeSemitones > -RegionEditor::maxTransposeSemitones);
        pitchUp.setEnabled (r.transposeSemitones < RegionEditor::maxTransposeSemitones);
        reverse.setToggleState (r.reversed, juce::dontSendNotification);
        reverse.setText (r.reversed ? "Playing backwards" : "Play backwards");

        // A region that can't be flexed (a loop that follows the tempo, a
        // reversed region) says why rather than half working.
        flexInfo = owner.engine.getTracks().flex().getFlex (r.id);
        flex.setEnabled (flexInfo.available);
        flex.setToggleState (flexInfo.on, juce::dontSendNotification);
        flex.setText (flexInfo.trackFlex ? "Flex on" : "Flex off");
        flex.setTooltip (! flexInfo.available ? flexInfo.unavailableReason
                         : flexInfo.trackFlex ? juce::String ("Flex is on for this track: drag the hits in the Editor")
                                              : juce::String ("Turn on Flex for this track, to move the hits in its recordings"));
        resetFlex.setEnabled (flexInfo.on && flexInfo.hasEdits);
        repaint();
    }

    int getIdealHeight() const
    {
        return padding / 2 + 4 * (rowHeight + rowGap) + (showsKeyNote() ? 22 : 0) + padding / 2;
    }

    void paint (juce::Graphics& g) override
    {
        if (! owner.region.has_value())
            return;

        g.setFont (fonts::ui (textSize::md));
        g.setColour (theme().textSecondary);
        g.drawText ("Gain", gainRow, juce::Justification::centredLeft);
        g.drawText ("Pitch", pitchRow, juce::Justification::centredLeft);
        g.drawText ("Direction", reverseRow, juce::Justification::centredLeft);
        g.drawText ("Flex", flexRow, juce::Justification::centredLeft);

        // The gain as it is now (it follows the slider during a drag).
        const auto db = gain.getValue();
        const auto gainText = std::abs (db) < 0.05 ? juce::String ("0.0 dB")
                                                   : (db > 0.0 ? "+" : minusSign) + juce::String (std::abs (db), 1) + " dB";
        g.setColour (theme().text);
        g.setFont (fonts::mono (textSize::sm));
        g.drawText (gainText, gainValue, juce::Justification::centredRight);

        // Semitones in a little well between the buttons.
        const int pitch = owner.region->transposeSemitones;
        g.setColour (theme().wellBg);
        g.fillRoundedRectangle (pitchValue.toFloat(), size::radiusSmall);
        g.setColour (theme().text);
        g.drawText (pitch == 0 ? juce::String ("0") : (pitch > 0 ? "+" : minusSign) + juce::String (std::abs (pitch)),
                    pitchValue, juce::Justification::centred);
        g.setColour (theme().textSecondary);
        g.setFont (fonts::ui (textSize::sm));
        g.drawText ("semitones", pitchUnits, juce::Justification::centredLeft);

        if (showsKeyNote())
        {
            const int shift = owner.region->keyShiftSemitones;
            auto note = juce::String ("Follows the song key");

            if (shift != 0)
                note << " (" << (shift > 0 ? "+" : minusSign) << std::abs (shift) << ")";

            g.drawText (note, keyNote, juce::Justification::centredLeft);
        }
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (padding, padding / 2);

        auto row = [&r]
        {
            auto next = r.removeFromTop (rowHeight);
            r.removeFromTop (rowGap);
            return next;
        };

        gainRow = row();
        auto gainArea = gainRow.withTrimmedLeft (labelWidth);
        gainValue = gainArea.removeFromRight (62);
        gain.setBounds (gainArea.withTrimmedRight (6));

        pitchRow = row();
        auto pitchArea = pitchRow.withTrimmedLeft (labelWidth);
        pitchDown.setBounds (pitchArea.removeFromLeft (30).withSizeKeepingCentre (30, 26));
        pitchValue = pitchArea.removeFromLeft (44).reduced (4, 2);
        pitchUp.setBounds (pitchArea.removeFromLeft (30).withSizeKeepingCentre (30, 26));
        pitchUnits = pitchArea.withTrimmedLeft (8);

        reverseRow = row();
        reverse.setBounds (reverseRow.withTrimmedLeft (labelWidth).removeFromLeft (150).withSizeKeepingCentre (150, 26));

        flexRow = row();
        auto flexArea = flexRow.withTrimmedLeft (labelWidth);
        flex.setBounds (flexArea.removeFromLeft (80).withSizeKeepingCentre (80, 26));
        flexArea.removeFromLeft (6);
        resetFlex.setBounds (flexArea.removeFromLeft (64).withSizeKeepingCentre (64, 26));

        keyNote = r.removeFromTop (22);
    }

private:
    RegionInspector& owner;
    bool dragging = false;   // the gain slider is being dragged

    juce::Slider gain { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };
    IconButton pitchDown { "Pitch down", std::nullopt, "-" };
    IconButton pitchUp   { "Pitch up", std::nullopt, "+" };
    IconButton reverse   { "Reverse", std::nullopt, "Play backwards" };
    IconButton flex      { "Flex", std::nullopt, "Flex off" };
    IconButton resetFlex { "Reset Flex", std::nullopt, "Reset" };
    FlexInfo flexInfo;   // the region's, as last shown

    juce::Rectangle<int> gainRow, gainValue, pitchRow, pitchValue, pitchUnits, reverseRow, flexRow, keyNote;

    bool showsKeyNote() const   { return owner.region.has_value() && owner.region->followsKey; }

    void transposeBy (int step)
    {
        if (owner.region.has_value())
            owner.engine.getTracks().regions().setRegionTranspose (owner.region->id, owner.region->transposeSemitones + step);
    }
};

//==============================================================================
// The "Editing" section: the grid, and Quantize with its strength and swing.
class RegionInspector::EditingControls  : public juce::Component
{
public:
    explicit EditingControls (RegionInspector& o) : owner (o)
    {
        for (auto* b : { &gridButton, &quantizeButton })
        {
            b->setColours (IconButton::borderedColours);
            b->setFont (fonts::ui (textSize::sm));
            b->setCornerRadius (size::radiusSmall);
            addAndMakeVisible (b);
        }

        gridButton.onClick = [this] { owner.showGridMenu (gridButton); };
        quantizeButton.onClick = [this] { if (owner.onQuantize) owner.onQuantize(); };

        // Percentages; the editors get them as 0..1.
        setUpPercent (strength, "Quantize strength", 100.0,
                      "How far Quantize moves each note or hit towards the grid: 100% all the way, "
                      "less keeps some of the feel. Double-click for 100%");
        setUpPercent (swing, "Quantize swing", 0.0,
                      "Makes every second grid line late, for a shuffle: 100% is a triplet feel. Double-click for none");
    }

    /** Shows the region's editing settings. */
    void refresh (const RegionInfo& r)
    {
        isMidi = r.isMidi;
        gridButton.setText (gridName (owner.grid));
        gridButton.setTooltip (isMidi ? "Where notes snap to when you draw, move or resize them, and what Quantize lines them up with"
                                      : "Where the region's edges snap to when you trim them, and what Quantize lines its hits up with");

        if (isMidi)
        {
            quantizeButton.setEnabled (true);
            quantizeButton.setTooltip ("Line the selected notes (or all of them) up with the grid (Q)");
        }
        else
        {
            // Audio is quantized with Flex: a region that can't be flexed
            // (a loop that follows the tempo, a reversed region) says why.
            const auto flex = owner.engine.getTracks().flex().getFlex (r.id);
            quantizeButton.setEnabled (flex.available);
            quantizeButton.setTooltip (! flex.available ? flex.unavailableReason
                                       : flex.trackFlex ? juce::String ("Line the hits in the selection (or all of them) up with the grid (Q)")
                                                        : juce::String ("Line the hits in the selection (or all of them) up with the grid (Q). "
                                                                        "This turns on Flex for the track"));
        }

        repaint();
    }

    int getIdealHeight() const   { return padding / 2 + 4 * (rowHeight + rowGap) + padding / 2; }

    void paint (juce::Graphics& g) override
    {
        g.setFont (fonts::ui (textSize::md));
        g.setColour (theme().textSecondary);
        g.drawText ("Grid", gridRow, juce::Justification::centredLeft);
        g.drawText ("Strength", strengthRow, juce::Justification::centredLeft);
        g.drawText ("Swing", swingRow, juce::Justification::centredLeft);

        g.setColour (theme().text);
        g.setFont (fonts::mono (textSize::sm));
        g.drawText (juce::String (juce::roundToInt (strength.getValue())) + "%", strengthValue, juce::Justification::centredRight);
        g.drawText (juce::String (juce::roundToInt (swing.getValue())) + "%", swingValue, juce::Justification::centredRight);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (padding, padding / 2);

        auto row = [&r]
        {
            auto next = r.removeFromTop (rowHeight);
            r.removeFromTop (rowGap);
            return next;
        };

        gridRow = row();
        gridButton.setBounds (gridRow.withTrimmedLeft (labelWidth).removeFromLeft (110).withSizeKeepingCentre (110, 26));
        quantizeButton.setBounds (row().withTrimmedLeft (labelWidth).removeFromLeft (110).withSizeKeepingCentre (110, 26));

        auto sliderRow = [&] (juce::Slider& slider, juce::Rectangle<int>& label, juce::Rectangle<int>& value)
        {
            label = row();
            auto area = label.withTrimmedLeft (labelWidth);
            value = area.removeFromRight (44);
            slider.setBounds (area.withTrimmedRight (6));
        };

        sliderRow (strength, strengthRow, strengthValue);
        sliderRow (swing, swingRow, swingValue);
    }

private:
    RegionInspector& owner;
    bool isMidi = false;

    IconButton gridButton     { "Grid", std::nullopt, "1/16" };
    IconButton quantizeButton { "Quantize", std::nullopt, "Quantize" };
    juce::Slider strength     { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };
    juce::Slider swing        { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };
    juce::Rectangle<int> gridRow, strengthRow, strengthValue, swingRow, swingValue;

    void setUpPercent (juce::Slider& slider, const juce::String& title, double initial, const juce::String& tip)
    {
        slider.setTitle (title);
        slider.setTooltip (tip);
        slider.setRange (0.0, 100.0, 1.0);
        slider.setValue (initial, juce::dontSendNotification);
        slider.setDoubleClickReturnValue (true, initial);
        slider.onValueChange = [this]
        {
            if (owner.onQuantizeFeelChanged)
                owner.onQuantizeFeelChanged (strength.getValue() / 100.0, swing.getValue() / 100.0);

            repaint();
        };
        addAndMakeVisible (slider);
    }
};

//==============================================================================
// The "Pitch" section (every audio region): Pitch mode on or off (the same
// switch as the Editor's Waveform | Pitch), the song's key, Snap to Key,
// Vibrato, Note transitions, Correct All and Reset, and a line saying what
// to do next. With Pitch mode off, or on a region that can't use it, the
// controls are greyed out and the line says why.
class RegionInspector::PitchControls  : public juce::Component
{
public:
    explicit PitchControls (RegionInspector& o) : owner (o)
    {
        // Amber while it's on, like Flex's switch.
        modeButton.setColours ([] { return IconButton::Colours { theme().controlFill, theme().textSecondary, theme().accent, theme().textOnAccent }; });
        modeButton.setFont (fonts::uiSemiBold (textSize::sm));
        modeButton.setCornerRadius (6.0f);
        modeButton.onClick = [this]
        {
            if (owner.region.has_value())
                owner.engine.getTracks().pitch().setPitchMode (owner.region->id, ! modeButton.getToggleState());
        };
        addAndMakeVisible (modeButton);

        setUpSlider (snap, "Snap to Key", "Snap to Key",
                     "How far each note's centre moves to the nearest note in the key. Works on the selected notes, "
                     "or every note if none are selected");
        setUpSlider (vibrato, "Vibrato", "Vibrato",
                     "Keep each note's vibrato and wavering as sung, or flatten it out. Works on the selected notes, "
                     "or every note if none are selected");

        const std::array<std::pair<IconButton*, NoteTransitions>, 3> segments {{
            { &natural, NoteTransitions::natural }, { &tight, NoteTransitions::tight }, { &instant, NoteTransitions::instant } }};

        for (auto [button, transitions] : segments)
        {
            button->setColours (IconButton::segmentColours);
            button->setFont (fonts::ui (textSize::sm));
            button->onClick = [this, t = transitions]
            {
                if (owner.region.has_value())
                    owner.engine.getTracks().pitch().setTransitions (owner.region->id, t);
            };
            addAndMakeVisible (*button);
        }

        natural.setTooltip ("Corrected notes glide into each other, as sung (about 120 ms)");
        tight.setTooltip ("Corrected notes move quickly from one to the next (about 40 ms)");
        instant.setTooltip ("Corrected notes jump straight to the next, for a hard-tuned sound");

        for (auto* b : { &correctAll, &reset })
        {
            b->setColours (IconButton::borderedColours);
            b->setFont (fonts::uiSemiBold (textSize::sm));
            b->setCornerRadius (size::radiusSmall);
            addAndMakeVisible (b);
        }

        correctAll.setTooltip ("Use this Snap to Key and Vibrato for every note in the region");
        correctAll.onClick = [this]
        {
            if (owner.region.has_value())
                owner.engine.getTracks().pitch().correctAll (owner.region->id, (float) snap.getValue() / 100.0f,
                                                             (float) vibrato.getValue() / 100.0f);
        };

        reset.setTooltip ("Put every note back to its original pitch");
        reset.onClick = [this]
        {
            if (owner.region.has_value())
                owner.engine.getTracks().pitch().resetPitch (owner.region->id);
        };
    }

    /** Shows the region's pitch state, for the selected notes (or all). */
    void refresh (const PitchInfo& info)
    {
        keyText = getKeyName (info.keyRoot, info.keyMinor) + juce::String::fromUTF8 (" \xc2\xb7 song");

        modeButton.setEnabled (info.available);
        modeButton.setToggleState (info.on, juce::dontSendNotification);
        modeButton.setText (info.on ? "Pitch on" : "Pitch off");
        modeButton.setTooltip (! info.available ? info.unavailableReason
                               : info.on ? juce::String ("The Editor shows the region's notes: click to show its waveform again")
                                         : juce::String ("Show the region's notes in the Editor, to correct their pitch"));

        // What to do next, or why nothing can be done.
        hint = ! info.available       ? info.unavailableReason
             : ! info.on              ? juce::String ("Turn Pitch on to see the region's notes in the Editor and correct them.")
             : info.analysing         ? juce::String::fromUTF8 ("Finding the notes\xe2\x80\xa6")
             : ! info.singleVoice     ? juce::String ("This recording doesn't look like a single voice or instrument.")
             : owner.pitchSelection.empty() ? juce::String ("These work on every note. Select notes in the Editor to change just those.")
                                            : juce::String ("These work on the selected notes.");

        // The sliders show the chosen notes' value, or "Mixed" when they differ.
        std::vector<const PitchNoteInfo*> chosen;

        for (auto& n : info.notes)
            if (owner.pitchSelection.empty()
                 || std::any_of (owner.pitchSelection.begin(), owner.pitchSelection.end(),
                                 [&] (double s) { return std::abs (s - n.sourceStart) < 0.003; }))
                chosen.push_back (&n);

        auto show = [&] (juce::Slider& slider, bool& mixed, float NoteCorrection::* member)
        {
            mixed = false;

            if (chosen.empty())
                return;

            const float first = chosen.front()->correction.*member;

            for (auto* n : chosen)
                mixed = mixed || std::abs (n->correction.*member - first) > 0.005f;

            if (dragging != &slider)
                slider.setValue (first * 100.0, juce::dontSendNotification);
        };

        show (snap, snapMixed, &NoteCorrection::snap);
        show (vibrato, vibratoMixed, &NoteCorrection::flatten);

        const bool ready = info.on && info.analysed && info.singleVoice && ! info.notes.empty();

        for (auto* c : std::initializer_list<juce::Component*> { &snap, &vibrato, &correctAll, &natural, &tight, &instant })
            c->setEnabled (ready);

        reset.setEnabled (info.available && info.hasCorrections);
        natural.setToggleState (info.transitions == NoteTransitions::natural, juce::dontSendNotification);
        tight.setToggleState (info.transitions == NoteTransitions::tight, juce::dontSendNotification);
        instant.setToggleState (info.transitions == NoteTransitions::instant, juce::dontSendNotification);
        repaint();
    }

    int getIdealHeight() const   { return padding / 2 + 7 * (rowHeight + rowGap) + hintHeight + padding / 2; }

    void paint (juce::Graphics& g) override
    {
        g.setFont (fonts::ui (textSize::sm));
        g.setColour (theme().textSecondary);
        g.drawFittedText (hint, hintArea, juce::Justification::topLeft, 3, 1.0f);

        // Greyed out like the controls while they can't be used.
        const auto labels = snap.isEnabled() ? theme().textSecondary : theme().textSecondary.withMultipliedAlpha (0.5f);
        g.setFont (fonts::ui (textSize::md));
        g.setColour (theme().textSecondary);
        g.drawText ("Pitch", modeRow, juce::Justification::centredLeft);
        g.drawText ("Key", keyRow, juce::Justification::centredLeft);
        g.setColour (labels);
        g.drawText ("Snap to Key", snapRow, juce::Justification::centredLeft);
        g.drawText ("Vibrato", vibratoRow, juce::Justification::centredLeft);
        g.drawText ("Note transitions", transitionsRow, juce::Justification::centredLeft);

        g.setColour (theme().text);
        g.drawText (keyText, keyRow.withTrimmedLeft (pitchLabelWidth), juce::Justification::centredLeft);

        g.setColour (snap.isEnabled() ? theme().text : theme().textSecondary.withMultipliedAlpha (0.5f));
        g.setFont (fonts::mono (textSize::sm));
        g.drawText (snapMixed ? juce::String ("Mixed") : juce::String (juce::roundToInt (snap.getValue())) + "%",
                    snapValue, juce::Justification::centredRight);

        const int v = juce::roundToInt (vibrato.getValue());
        g.drawText (vibratoMixed ? juce::String ("Mixed") : v == 0 ? juce::String ("Keep") : v == 100 ? juce::String ("Flatten")
                                                                    : juce::String (v) + "%",
                    vibratoValue, juce::Justification::centredRight);

        // The transitions switch's well, behind its segments.
        g.setColour (theme().segmentWell);
        g.fillRoundedRectangle (transitionsArea.toFloat(), size::radius);
        g.setColour (theme().segmentWellBorder);
        g.drawRoundedRectangle (transitionsArea.toFloat().reduced (0.5f), size::radius, 1.0f);

        for (auto* segment : { &natural, &tight, &instant })
            IconButton::drawSegmentShadow (g, *segment);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (padding, padding / 2);

        auto row = [&r]
        {
            auto next = r.removeFromTop (rowHeight);
            r.removeFromTop (rowGap);
            return next;
        };

        modeRow = row();
        modeButton.setBounds (modeRow.withTrimmedLeft (pitchLabelWidth).removeFromLeft (90).withSizeKeepingCentre (90, 26));
        hintArea = r.removeFromTop (hintHeight);
        r.removeFromTop (rowGap);
        keyRow = row();

        auto sliderRow = [&] (juce::Slider& slider, juce::Rectangle<int>& label, juce::Rectangle<int>& value)
        {
            label = row();
            auto area = label.withTrimmedLeft (pitchLabelWidth);
            value = area.removeFromRight (54);
            slider.setBounds (area.withTrimmedRight (6));
        };

        sliderRow (snap, snapRow, snapValue);
        sliderRow (vibrato, vibratoRow, vibratoValue);

        // Note transitions: its name, then the switch across the width.
        transitionsRow = row();
        transitionsArea = row();
        auto segments = transitionsArea.reduced (2);
        const int segmentWidth = segments.getWidth() / 3;
        natural.setBounds (segments.removeFromLeft (segmentWidth));
        tight.setBounds (segments.removeFromLeft (segmentWidth));
        instant.setBounds (segments);

        auto buttons = row();
        reset.setBounds (buttons.removeFromRight (72));
        buttons.removeFromRight (8);
        correctAll.setBounds (buttons);
    }

private:
    RegionInspector& owner;

    juce::Slider snap    { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };
    juce::Slider vibrato { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };
    juce::Slider* dragging = nullptr;   // the slider being dragged (it's ahead of the session)
    bool snapMixed = false, vibratoMixed = false;
    IconButton natural { "Natural transitions", std::nullopt, "Natural" };
    IconButton tight   { "Tight transitions", std::nullopt, "Tight" };
    IconButton instant { "Instant transitions", std::nullopt, "Instant" };
    IconButton correctAll { "Correct All", std::nullopt, "Correct All" };
    IconButton reset { "Reset Pitch", std::nullopt, "Reset" };
    IconButton modeButton { "Pitch mode", std::nullopt, "Pitch off" };
    juce::String keyText, hint;
    juce::Rectangle<int> modeRow, hintArea;
    static constexpr int hintHeight = 34;

    juce::Rectangle<int> keyRow, snapRow, snapValue, vibratoRow, vibratoValue, transitionsRow, transitionsArea;

    // Wider than the other sections' labels: "Snap to Key" is longer.
    static constexpr int pitchLabelWidth = 86;

    void setUpSlider (juce::Slider& slider, const juce::String& title, const juce::String& undoName, const juce::String& tip)
    {
        slider.setTitle (title);
        slider.setTooltip (tip);
        slider.setRange (0.0, 100.0, 1.0);
        slider.setDoubleClickReturnValue (true, 0.0);
        slider.setScrollWheelEnabled (false);   // every notch would be an undo step of its own

        // A whole drag is one undo step (as with the gain slider): it starts
        // on mouse-down, and every change until mouse-up goes into it.
        slider.onDragStart = [this, &slider, undoName]
        {
            dragging = &slider;
            owner.engine.beginUndoStep (undoName);
        };
        slider.onDragEnd = [this] { dragging = nullptr; };
        slider.onValueChange = [this, &slider, undoName]
        {
            if (! owner.region.has_value())
                return;

            if (dragging != &slider)
                owner.engine.beginUndoStep (undoName);   // a double-click: a step of its own

            auto& pitch = owner.engine.getTracks().pitch();
            const auto amount = (float) slider.getValue() / 100.0f;

            if (&slider == &snap)
                pitch.setSnap (owner.region->id, owner.pitchSelection, amount);
            else
                pitch.setFlatten (owner.region->id, owner.pitchSelection, amount);

            repaint();
        };
        addAndMakeVisible (slider);
    }
};

//==============================================================================
RegionInspector::RegionInspector (AudioEngine& e) : engine (e)
{
    setTitle ("Region inspector");

    audioControls = std::make_unique<AudioControls> (*this);
    editingControls = std::make_unique<EditingControls> (*this);
    pitchControls = std::make_unique<PitchControls> (*this);
    audioSection = std::make_unique<CollapsibleSection> ("Audio", *audioControls, true);
    editingSection = std::make_unique<CollapsibleSection> ("Editing", *editingControls, true);
    pitchSection = std::make_unique<CollapsibleSection> ("Pitch", *pitchControls, true);

    for (auto* section : { audioSection.get(), editingSection.get(), pitchSection.get() })
    {
        section->onExpandedChanged = [this] { layoutSections(); };
        sectionHolder.addChildComponent (section);
    }

    viewport.setViewedComponent (&sectionHolder, false);
    viewport.setScrollBarsShown (true, false);
    addAndMakeVisible (viewport);
}

RegionInspector::~RegionInspector() = default;

void RegionInspector::setRegion (std::optional<RegionInfo> r, Family family)
{
    region = std::move (r);
    const bool audio = region.has_value() && ! region->isMidi;

    // A Drummer region's notes aren't edited by hand, so it has no grid
    // or Quantize (its settings are in the Drummer editor beside this).
    const bool drummer = region.has_value() && region->isDrummer;

    audioSection->setVisible (audio);
    editingSection->setVisible (region.has_value() && ! drummer);

    if (audio)
        audioControls->refresh (*region, family);

    if (region.has_value() && ! drummer)
        editingControls->refresh (*region);

    // The Pitch section, for every audio region (greyed out while Pitch
    // mode is off, with a line saying how to turn it on).
    pitchSection->setVisible (audio);
    const auto pitch = audio ? engine.getTracks().pitch().getPitch (region->id) : PitchInfo();

    if (audio)
        pitchControls->refresh (pitch);

    // In Pitch mode the Pitch section opens and the others fold up (still
    // there to open, in their usual order); leaving it puts them back as they were.
    if (const bool on = audio && pitch.on; on != pitchLayout)
    {
        pitchLayout = on;

        if (on)
        {
            savedExpanded = { audioSection->isExpanded(), editingSection->isExpanded(), pitchSection->isExpanded() };
            audioSection->setExpanded (false);
            editingSection->setExpanded (false);
            pitchSection->setExpanded (true);
        }
        else
        {
            audioSection->setExpanded (savedExpanded[0]);
            editingSection->setExpanded (savedExpanded[1]);
            pitchSection->setExpanded (savedExpanded[2]);
        }
    }

    layoutSections();
    repaint();
}

void RegionInspector::setPitchSelection (std::vector<double> noteStarts)
{
    pitchSelection = std::move (noteStarts);

    if (region.has_value() && ! region->isMidi && pitchSection->isVisible())
        pitchControls->refresh (engine.getTracks().pitch().getPitch (region->id));
}

void RegionInspector::showGridMenu (juce::Component& target)
{
    juce::PopupMenu menu;
    menu.addSectionHeader ("Grid");

    auto add = [&] (const juce::String& name, EditorGrid value)
    {
        menu.addItem (juce::PopupMenu::Item (name).setTicked (grid == value).setAction ([this, value]
        {
            grid = value;

            if (region.has_value())
                editingControls->refresh (*region);

            if (onGridChanged)
                onGridChanged (grid);
        }));
    };

    add ("Bar", EditorGrid::bar);
    add ("1/4 (beat)", EditorGrid::quarter);
    add ("1/8", EditorGrid::eighth);
    add ("1/16", EditorGrid::sixteenth);
    add ("1/32", EditorGrid::thirtySecond);
    add ("Off", EditorGrid::off);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&target));
}

void RegionInspector::layoutSections()
{
    const int width = viewport.getMaximumVisibleWidth();

    // Contents decide their own height; sections stack them with headers.
    audioControls->setSize (width, audioControls->getIdealHeight());
    editingControls->setSize (width, editingControls->getIdealHeight());
    pitchControls->setSize (width, pitchControls->getIdealHeight());

    int y = 0;

    for (auto* section : { audioSection.get(), editingSection.get(), pitchSection.get() })
    {
        if (! section->isVisible())
            continue;

        section->setBounds (0, y, width, section->getPreferredHeight());
        y += section->getHeight();
    }

    sectionHolder.setSize (width, juce::jmax (y, viewport.getMaximumVisibleHeight()));
}

void RegionInspector::paint (juce::Graphics& g)
{
    g.fillAll (theme().inspectorBg);
    g.setColour (theme().divider);
    g.fillRect (getLocalBounds().removeFromRight (1));

    if (! region.has_value())
    {
        g.setColour (theme().textSecondary);
        g.setFont (fonts::ui (textSize::md));
        g.drawFittedText ("Select a region to see its settings here.", getLocalBounds().reduced (padding),
                          juce::Justification::centred, 3);
    }
    else if (region->isDrummer)
    {
        g.setColour (theme().textSecondary);
        g.setFont (fonts::ui (textSize::md));
        g.drawFittedText ("The Drummer plays this region. To change its notes yourself, right-click it and "
                          "choose Convert to MIDI Region.", getLocalBounds().reduced (padding),
                          juce::Justification::centred, 4);
    }
}

void RegionInspector::resized()
{
    viewport.setBounds (getLocalBounds().withTrimmedRight (1));
    layoutSections();
}

} // namespace ikaros
