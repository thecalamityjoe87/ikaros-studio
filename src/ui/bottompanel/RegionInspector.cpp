#include "ui/bottompanel/RegionInspector.h"
#include "engine/AudioEngine.h"
#include "engine/TrackController.h"
#include "ui/widgets/IconButton.h"
#include "ui/theme/Fonts.h"

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

    IconButton::Colours raisedWithBorder()
    {
        auto c = IconButton::raisedColours();
        c.border = colour::border;
        return c;
    }
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
            b->setColours (raisedWithBorder());
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
        reverse.setColours ({ colour::controlFill, colour::textSecondary, colour::effectOn, colour::textOnAccent });
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
        flex.setColours ({ colour::controlFill, colour::textSecondary, colour::accent, colour::textOnAccent });
        flex.setFont (fonts::uiSemiBold (textSize::sm));
        flex.setCornerRadius (6.0f);
        flex.onClick = [this]
        {
            if (owner.region.has_value())
                owner.engine.getTracks().flex().setFlexOn (owner.region->trackId, ! flexInfo.trackFlex);
        };
        addAndMakeVisible (flex);

        resetFlex.setColours (raisedWithBorder());
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
    void refresh (const RegionInfo& r, juce::Colour trackColour)
    {
        // dontSendNotification: showing the region's value isn't a change to
        // make. (Not during a drag: the drag is ahead of the session.)
        if (! dragging)
            gain.setValue (r.gainDb, juce::dontSendNotification);

        gain.setColour (juce::Slider::trackColourId, trackColour);
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
        g.setColour (colour::textSecondary);
        g.drawText ("Gain", gainRow, juce::Justification::centredLeft);
        g.drawText ("Pitch", pitchRow, juce::Justification::centredLeft);
        g.drawText ("Direction", reverseRow, juce::Justification::centredLeft);
        g.drawText ("Flex", flexRow, juce::Justification::centredLeft);

        // The gain as it is now (it follows the slider during a drag).
        const auto db = gain.getValue();
        const auto gainText = std::abs (db) < 0.05 ? juce::String ("0.0 dB")
                                                   : (db > 0.0 ? "+" : minusSign) + juce::String (std::abs (db), 1) + " dB";
        g.setColour (colour::text);
        g.setFont (fonts::mono (textSize::sm));
        g.drawText (gainText, gainValue, juce::Justification::centredRight);

        // Semitones in a little well between the buttons.
        const int pitch = owner.region->transposeSemitones;
        g.setColour (colour::wellBg);
        g.fillRoundedRectangle (pitchValue.toFloat(), size::radiusSmall);
        g.setColour (colour::text);
        g.drawText (pitch == 0 ? juce::String ("0") : (pitch > 0 ? "+" : minusSign) + juce::String (std::abs (pitch)),
                    pitchValue, juce::Justification::centred);
        g.setColour (colour::textSecondary);
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
            b->setColours (raisedWithBorder());
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
        g.setColour (colour::textSecondary);
        g.drawText ("Grid", gridRow, juce::Justification::centredLeft);
        g.drawText ("Strength", strengthRow, juce::Justification::centredLeft);
        g.drawText ("Swing", swingRow, juce::Justification::centredLeft);

        g.setColour (colour::text);
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
        slider.setColour (juce::Slider::trackColourId, colour::accent);
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
RegionInspector::RegionInspector (AudioEngine& e) : engine (e)
{
    setTitle ("Region inspector");

    audioControls = std::make_unique<AudioControls> (*this);
    editingControls = std::make_unique<EditingControls> (*this);
    audioSection = std::make_unique<CollapsibleSection> ("Audio", *audioControls, true);
    editingSection = std::make_unique<CollapsibleSection> ("Editing", *editingControls, true);

    for (auto* section : { audioSection.get(), editingSection.get() })
    {
        section->onExpandedChanged = [this] { layoutSections(); };
        sectionHolder.addChildComponent (section);
    }

    viewport.setViewedComponent (&sectionHolder, false);
    viewport.setScrollBarsShown (true, false);
    addAndMakeVisible (viewport);
}

RegionInspector::~RegionInspector() = default;

void RegionInspector::setRegion (std::optional<RegionInfo> r, juce::Colour trackColour)
{
    region = std::move (r);
    const bool audio = region.has_value() && ! region->isMidi;

    audioSection->setVisible (audio);
    editingSection->setVisible (region.has_value());

    if (audio)
        audioControls->refresh (*region, trackColour);

    if (region.has_value())
        editingControls->refresh (*region);

    layoutSections();
    repaint();
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

    int y = 0;

    for (auto* section : { audioSection.get(), editingSection.get() })
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
    g.fillAll (colour::sidebarBg);
    g.setColour (colour::divider);
    g.fillRect (getLocalBounds().removeFromRight (1));

    if (! region.has_value())
    {
        g.setColour (colour::textSecondary);
        g.setFont (fonts::ui (textSize::md));
        g.drawFittedText ("Select a region to see its settings here.", getLocalBounds().reduced (padding),
                          juce::Justification::centred, 3);
    }
}

void RegionInspector::resized()
{
    viewport.setBounds (getLocalBounds().withTrimmedRight (1));
    layoutSections();
}

} // namespace ikaros
