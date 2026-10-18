#include "ui/bottompanel/MixerView.h"
#include "ui/theme/Fonts.h"
#include "ui/theme/IkarosLookAndFeel.h"
#include "ui/theme/Tokens.h"
#include "ui/widgets/AutomationHint.h"
#include <array>
#include <tuple>

namespace ikaros
{

using namespace tokens;

namespace
{
    // Track strips: fader on the left, Pan/Reverb/Delay on the right. They
    // share the mixer's width between these sizes, so with only a few tracks
    // the strips (and their knobs) are bigger.
    constexpr int minStripWidth = 196;
    constexpr int maxStripWidth = 360;
    constexpr int faderAreaWidth = 84;       // meter and fader
    constexpr int maxKnobSize = 72;
    constexpr int captionWidth = 56;         // a caption beside a knob
    constexpr int captionHeight = 30;        // a caption under a knob
    constexpr int stripGap = 10;
    constexpr int masterStripWidth = 110;
}

//==============================================================================
class MixerView::Strip  : public juce::Component
{
public:
    Strip (AudioEngine& e, Selection& s, TrackId id)
        : engine (e), selection (s), trackId (id), meter (LevelMeter::Orientation::vertical)
    {
        addAndMakeVisible (meter);

        // One undo step per drag: start a transaction when the mouse goes down.
        fader.setRange (0.0, 1.0);
        addAndMakeVisible (fader);

        fader.onDragStart = [this] { engine.beginUndoStep ("Volume"); selection.selectTrack (trackId); };
        fader.onValueChange = [this] { engine.getTracks().setVolume (trackId, (float) fader.getValue()); };

        // Pan: a small knob, centre at 12 o'clock. Double-click puts it back in the middle.
        pan.setRange (-1.0, 1.0);
        pan.setDoubleClickReturnValue (true, 0.0);
        pan.getProperties().set ("bipolar", true);   // read by IkarosLookAndFeel::drawRotarySlider
        pan.setTooltip ("Pan: left or right (double-click to center)");
        pan.onDragStart = [this] { engine.beginUndoStep ("Pan"); selection.selectTrack (trackId); };
        pan.onValueChange = [this]
        {
            engine.getTracks().setPan (trackId, (float) pan.getValue());
            panText = describePan ((float) pan.getValue());
            repaint();
        };
        addAndMakeVisible (pan);

        // Sends to the shared Reverb and Delay buses.
        for (int bus = 0; bus < SendBuses::numBuses; ++bus)
        {
            auto& knob = sendKnobs[(size_t) bus];
            const auto busName = SendBuses::getBusName (bus);
            knob.setRange (0.0, 1.0);
            knob.setDoubleClickReturnValue (true, 0.0);
            knob.setTooltip (busName + ": how much of this track goes to the shared " + busName.toLowerCase()
                             + " (double-click for none)");
            knob.onDragStart = [this, busName] { engine.beginUndoStep (busName + " Send"); selection.selectTrack (trackId); };
            knob.onValueChange = [this, bus]
            {
                const auto level = (float) sendKnobs[(size_t) bus].getValue();
                engine.getTracks().sends().setSendLevel (trackId, bus, level);
                sendTexts[(size_t) bus] = describeSend (level);
                repaint();
            };
            addAndMakeVisible (knob);
        }

        // Automated, the controls move with the automation, and a grab only
        // moves them until they're let go (see followAutomation).
        auto guard = [this] (juce::Slider& s, juce::String target)
        {
            followAutomation (s, [this, target] { return automated.contains (target); },
                              [this, &s, target] { showAutomationHint (engine, s, trackId, target); });
        };

        guard (fader, automationTargets::volume);
        guard (pan, automationTargets::pan);

        for (int bus = 0; bus < SendBuses::numBuses; ++bus)
            guard (sendKnobs[(size_t) bus], automationTargets::send (bus));
    }

    // "C", or how far left/right, e.g. "L 30".
    static juce::String describePan (float value)
    {
        const int amount = juce::roundToInt (std::abs (value) * 100.0f);
        return amount == 0 ? juce::String ("C") : (value < 0.0f ? "L " : "R ") + juce::String (amount);
    }

    static juce::String describeSend (float level)
    {
        return juce::String (juce::roundToInt (level * 100.0f)) + "%";
    }

    static juce::String describeDb (float db)
    {
        return db <= -99.0f ? juce::String::fromUTF8 ("\xe2\x88\x92\xe2\x88\x9e dB")   // minus infinity
                            : juce::String (db, 1) + " dB";
    }

    // What paint() draws as text and colour, to tell whether a repaint is due.
    // (The fader and knobs redraw themselves when their values change.)
    auto drawnState() const   { return std::make_tuple (name, colour, selected, dbText, panText, sendTexts); }

    void update (const TrackInfo& t, bool isSelected)
    {
        const auto before = drawnState();
        automated = t.automated;
        setAutomatedLook (fader, automated.contains (automationTargets::volume));
        setAutomatedLook (pan, automated.contains (automationTargets::pan));

        for (int bus = 0; bus < SendBuses::numBuses; ++bus)
            setAutomatedLook (sendKnobs[(size_t) bus], automated.contains (automationTargets::send (bus)));

        name = t.name;
        colour = t.colour;
        selected = isSelected;
        dbText = describeDb (t.volumeDb);
        panText = describePan (t.pan);
        fader.setTitle (t.name + " volume");
        pan.setTitle (t.name + " pan");
        pan.setColour (IkarosLookAndFeel::knobArcColourId, t.colour);

        const float sends[] { t.reverbSend, t.delaySend };

        for (int bus = 0; bus < SendBuses::numBuses; ++bus)
        {
            auto& knob = sendKnobs[(size_t) bus];
            knob.setTitle (t.name + " " + SendBuses::getBusName (bus).toLowerCase() + " send");
            knob.setColour (IkarosLookAndFeel::knobArcColourId, t.colour);

            if (! knob.isMouseButtonDown())
                knob.setValue (sends[bus], juce::dontSendNotification);

            sendTexts[(size_t) bus] = describeSend ((float) knob.getValue());
        }

        // Don't fight the user: only move the controls if they aren't dragging them.
        if (! fader.isMouseButtonDown())
            fader.setValue (t.volume, juce::dontSendNotification);

        if (! pan.isMouseButtonDown())
            pan.setValue (t.pan, juce::dontSendNotification);

        if (drawnState() != before)
            repaint();
    }

    void setLevel (StereoLevel l)   { meter.setLevel (l); }

    void mouseDown (const juce::MouseEvent&) override
    {
        selection.selectTrack (trackId);
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (selected ? colour::selectedRow : colour::cardBg);
        g.fillRoundedRectangle (r, size::radiusCard);

        // Coloured top edge, as in the design (inset 3px shadow).
        juce::Graphics::ScopedSaveState save (g);
        juce::Path shape;
        shape.addRoundedRectangle (r, size::radiusCard);
        g.reduceClipRegion (shape);
        g.setColour (colour);
        g.fillRect (r.removeFromTop (3.0f));

        auto area = getLocalBounds().reduced (10);
        g.setColour (colour::text);
        g.setFont (fonts::uiBold (textSize::md));
        g.drawText (name, area.removeFromTop (18), juce::Justification::centred, true);

        g.setColour (colour::textSecondary);
        g.setFont (fonts::mono (textSize::sm));
        g.drawText (dbText, area.removeFromBottom (16), juce::Justification::centred);

        // Each knob's caption: its name, and its setting underneath.
        const juce::String names[] { "Pan", SendBuses::getBusName (SendBuses::reverb), SendBuses::getBusName (SendBuses::delay) };
        const juce::String values[] { panText, sendTexts[0], sendTexts[1] };

        const auto justification = captionsBelow ? juce::Justification::centred : juce::Justification::centredLeft;

        for (int i = 0; i < 3; ++i)
        {
            auto caption = captionAreas[(size_t) i];
            g.setColour (colour::textSecondary);
            g.setFont (fonts::ui (textSize::xs));
            g.drawText (names[i], caption.removeFromTop (14), justification);
            g.setColour (colour::text);
            g.setFont (fonts::mono (textSize::sm));
            g.drawText (values[i], caption, justification);
        }

    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (10);
        area.removeFromTop (22);
        area.removeFromBottom (24);

        // Pan, Reverb and Delay go right of the fader, either stacked in a
        // column with captions beside them, or side by side with captions
        // underneath: whichever lets the knobs be bigger. A tall panel
        // favors the column, a wide strip the row.
        auto column = area.removeFromRight (area.getWidth() - faderAreaWidth);
        juce::Slider* knobs[] { &pan, &sendKnobs[0], &sendKnobs[1] };

        const int stackedSize = juce::jmin (column.getHeight() / 3 - 4, column.getWidth() - captionWidth - 4);
        const int acrossSize  = juce::jmin (column.getWidth() / 3 - 8, column.getHeight() - captionHeight);
        captionsBelow = acrossSize > stackedSize;
        const int size = juce::jlimit (16, maxKnobSize, juce::jmax (stackedSize, acrossSize));

        if (captionsBelow)
        {
            auto row = column.withSizeKeepingCentre (column.getWidth(), size + captionHeight);
            const int cellWidth = row.getWidth() / 3;

            for (int i = 0; i < 3; ++i)
            {
                auto cell = row.removeFromLeft (cellWidth);
                knobs[i]->setBounds (cell.removeFromTop (size).withSizeKeepingCentre (size, size));
                captionAreas[(size_t) i] = cell;
            }
        }
        else
        {
            const int slot = column.getHeight() / 3;

            for (int i = 0; i < 3; ++i)
            {
                auto cell = column.withY (column.getY() + i * slot).withHeight (slot);
                knobs[i]->setBounds (cell.removeFromLeft (size).withSizeKeepingCentre (size, size));
                cell.removeFromLeft (4);
                captionAreas[(size_t) i] = cell.withSizeKeepingCentre (cell.getWidth(), 28);
            }
        }

        area.removeFromTop (4);

        auto meterArea = area.withSizeKeepingCentre (64, area.getHeight());
        meter.setBounds (meterArea.removeFromLeft (24));
        meterArea.removeFromLeft (6);
        fader.setBounds (meterArea.withTrimmedTop (-7).withTrimmedBottom (-7));
    }

private:
    AudioEngine& engine;
    Selection& selection;
    const TrackId trackId;   // 0 = master
    LevelMeter meter;
    juce::Slider fader { juce::Slider::LinearVertical, juce::Slider::NoTextBox };
    juce::Slider pan { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox };
    std::array<juce::Slider, SendBuses::numBuses> sendKnobs { juce::Slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox },
                                                              juce::Slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox } };
    juce::String name, dbText, panText { "C" };
    std::array<juce::Rectangle<int>, 3> captionAreas;   // pan, reverb, delay
    bool captionsBelow = false;                         // knobs side by side, captions under them
    std::array<juce::String, SendBuses::numBuses> sendTexts { "0%", "0%" };
    juce::Colour colour;
    bool selected = false;
    juce::StringArray automated;                        // what the track's automation moves (TrackInfo::automated)
};

//==============================================================================
// The master channel's strip: everything mixed together. Just its meter,
// fader and level; it has no pan or sends.
class MixerView::MasterStrip  : public juce::Component
{
public:
    explicit MasterStrip (AudioEngine& e) : engine (e), meter (LevelMeter::Orientation::vertical)
    {
        addAndMakeVisible (meter);

        fader.setTitle ("Master volume");
        fader.setRange (0.0, 1.0);
        fader.onDragStart = [this] { engine.beginUndoStep ("Master Volume"); };   // one undo step per drag
        fader.onValueChange = [this] { engine.getTracks().setMasterVolume ((float) fader.getValue()); };
        addAndMakeVisible (fader);
    }

    void refresh()
    {
        auto& tc = engine.getTracks();
        const auto db = tc.getMasterVolumeDb();
        const auto text = db <= -99.0f ? juce::String::fromUTF8 ("\xe2\x88\x92\xe2\x88\x9e dB")   // minus infinity
                                       : juce::String (db, 1) + " dB";

        // Don't fight the user: only move the fader if they aren't dragging it.
        if (! fader.isMouseButtonDown())
            fader.setValue (tc.getMasterVolume(), juce::dontSendNotification);

        if (text != dbText)
        {
            dbText = text;
            repaint();
        }
    }

    void updateMeter()      { meter.setLevel (engine.getMasterLevel()); }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (colour::cardBg);
        g.fillRoundedRectangle (r, size::radiusCard);

        // An accent top edge, like the track strips' coloured one.
        juce::Graphics::ScopedSaveState save (g);
        juce::Path shape;
        shape.addRoundedRectangle (r, size::radiusCard);
        g.reduceClipRegion (shape);
        g.setColour (colour::accent);
        g.fillRect (r.removeFromTop (3.0f));

        auto area = getLocalBounds().reduced (10);
        g.setColour (colour::text);
        g.setFont (fonts::uiBold (textSize::md));
        g.drawText ("Master", area.removeFromTop (18), juce::Justification::centred, true);

        g.setColour (colour::textSecondary);
        g.setFont (fonts::mono (textSize::sm));
        g.drawText (dbText, area.removeFromBottom (16), juce::Justification::centred);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (10);
        area.removeFromTop (22);
        area.removeFromBottom (24);

        auto meterArea = area.withSizeKeepingCentre (64, area.getHeight());
        meter.setBounds (meterArea.removeFromLeft (24));
        meterArea.removeFromLeft (6);
        fader.setBounds (meterArea.withTrimmedTop (-7).withTrimmedBottom (-7));
    }

private:
    AudioEngine& engine;
    LevelMeter meter;
    juce::Slider fader { juce::Slider::LinearVertical, juce::Slider::NoTextBox };
    juce::String dbText;
};

//==============================================================================
MixerView::MixerView (AudioEngine& e, Selection& s) : engine (e), selection (s)
{
    setTitle ("Mixer");

    masterStrip = std::make_unique<MasterStrip> (engine);
    addAndMakeVisible (*masterStrip);

    viewport.setViewedComponent (&stripHolder, false);
    viewport.setScrollBarsShown (false, true);
    addAndMakeVisible (viewport);
}

MixerView::~MixerView() = default;

void MixerView::refresh()
{
    auto tracks = engine.getTracks().getTracks();
    std::map<TrackId, std::unique_ptr<Strip>> next;
    order.clear();

    for (auto& t : tracks)
    {
        auto strip = std::move (strips[t.id]);

        if (strip == nullptr)
        {
            strip = std::make_unique<Strip> (engine, selection, t.id);
            stripHolder.addAndMakeVisible (*strip);
        }

        strip->update (t, t.id == selection.getTrack());
        order.push_back (t.id);
        next[t.id] = std::move (strip);
    }

    strips = std::move (next);
    masterStrip->refresh();
    layoutStrips();
}

void MixerView::updateMeters()
{
    if (! isShowing())
        return;

    for (auto& [id, strip] : strips)
        strip->setLevel (engine.getTracks().getTrackLevel (id));

    masterStrip->updateMeter();
}

void MixerView::layoutStrips()
{
    auto h = juce::jmax (120, viewport.getMaximumVisibleHeight());
    const int numStrips = juce::jmax (1, (int) order.size());
    const int spare = viewport.getMaximumVisibleWidth() - stripGap * (numStrips - 1);
    const int stripWidth = juce::jlimit (minStripWidth, maxStripWidth, spare / numStrips);
    int x = 0;

    for (auto id : order)
    {
        strips[id]->setBounds (x, 0, stripWidth, h);
        x += stripWidth + stripGap;
    }

    stripHolder.setSize (juce::jmax (x - stripGap, viewport.getMaximumVisibleWidth()), h);
}

void MixerView::resized()
{
    // The master strip stays put at the right; the tracks scroll beside it.
    auto r = getLocalBounds().reduced (18, 14);
    masterStrip->setBounds (r.removeFromRight (masterStripWidth));
    r.removeFromRight (stripGap * 2);
    viewport.setBounds (r);
    layoutStrips();
}

} // namespace ikaros
