#include "ui/bottompanel/MixerView.h"
#include "ui/theme/Fonts.h"
#include "ui/theme/IkarosLookAndFeel.h"
#include "ui/theme/Tokens.h"

namespace ikaros
{

using namespace tokens;

namespace
{
    constexpr int stripWidth = 110;
    constexpr int stripGap = 10;
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

        if (isMaster())
        {
            fader.setTitle ("Master volume");
            fader.onDragStart = [this] { engine.beginUndoStep ("Master Volume"); };
            fader.onValueChange = [this] { engine.getTracks().setMasterVolume ((float) fader.getValue()); };
            return;
        }

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
    }

    // "C", or how far left/right, e.g. "L 30".
    static juce::String describePan (float value)
    {
        const int amount = juce::roundToInt (std::abs (value) * 100.0f);
        return amount == 0 ? juce::String ("C") : (value < 0.0f ? "L " : "R ") + juce::String (amount);
    }

    static juce::String describeDb (float db)
    {
        return db <= -99.0f ? juce::String::fromUTF8 ("\xe2\x88\x92\xe2\x88\x9e dB")   // minus infinity
                            : juce::String (db, 1) + " dB";
    }

    bool isMaster() const   { return trackId == 0; }

    void update (const TrackInfo& t, bool isSelected)
    {
        name = t.name;
        colour = t.colour;
        selected = isSelected;
        dbText = describeDb (t.volumeDb);
        panText = describePan (t.pan);
        fader.setTitle (t.name + " volume");
        pan.setTitle (t.name + " pan");
        pan.setColour (IkarosLookAndFeel::knobArcColourId, t.colour);

        // Don't fight the user: only move the controls if they aren't dragging them.
        if (! fader.isMouseButtonDown())
            fader.setValue (t.volume, juce::dontSendNotification);

        if (! pan.isMouseButtonDown())
            pan.setValue (t.pan, juce::dontSendNotification);

        repaint();
    }

    void updateMaster (float volume, float volumeDb)
    {
        name = "Master";
        colour = colour::accent;
        dbText = describeDb (volumeDb);

        if (! fader.isMouseButtonDown())
            fader.setValue (volume, juce::dontSendNotification);

        repaint();
    }

    void setLevel (StereoLevel l)   { meter.setLevel (l); }

    void mouseDown (const juce::MouseEvent&) override
    {
        if (! isMaster())
            selection.selectTrack (trackId);
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (selected ? colour::selectedRow : colour::cardBg);
        g.fillRoundedRectangle (r, 12.0f);

        // Coloured top edge, as in the design (inset 3px shadow).
        juce::Graphics::ScopedSaveState save (g);
        juce::Path shape;
        shape.addRoundedRectangle (r, 12.0f);
        g.reduceClipRegion (shape);
        g.setColour (colour);
        g.fillRect (r.removeFromTop (3.0f));

        auto area = getLocalBounds().reduced (10);
        g.setColour (colour::text);
        g.setFont (fonts::uiBold (13.0f));
        g.drawText (name, area.removeFromTop (18), juce::Justification::centred, true);

        g.setColour (colour::textSecondary);
        g.setFont (fonts::mono (12.0f));
        g.drawText (dbText, area.removeFromBottom (16), juce::Justification::centred);

        if (! isMaster())
        {
            g.setFont (fonts::mono (10.0f));
            g.drawText (panText, panTextArea, juce::Justification::centred);
        }
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (10);
        area.removeFromTop (22);
        area.removeFromBottom (24);

        if (! isMaster())
        {
            // Pan knob under the name, with its readout below it.
            auto panRow = area.removeFromTop (52);
            panTextArea = panRow.removeFromBottom (12);
            pan.setBounds (panRow.withSizeKeepingCentre (40, 40));
            area.removeFromTop (4);
        }
        else
        {
            area.removeFromTop (4);
        }

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
    juce::String name, dbText, panText { "C" };
    juce::Rectangle<int> panTextArea;
    juce::Colour colour;
    bool selected = false;
};

//==============================================================================
MixerView::MixerView (AudioEngine& e, Selection& s) : engine (e), selection (s)
{
    setTitle ("Mixer");
    masterStrip = std::make_unique<Strip> (engine, selection, 0);
    stripHolder.addAndMakeVisible (*masterStrip);

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
    masterStrip->updateMaster (engine.getTracks().getMasterVolume(), engine.getTracks().getMasterVolumeDb());
    layoutStrips();
}

void MixerView::updateMeters()
{
    if (! isShowing())
        return;

    for (auto& [id, strip] : strips)
        strip->setLevel (engine.getTracks().getTrackLevel (id));

    masterStrip->setLevel (engine.getMasterLevel());
}

void MixerView::layoutStrips()
{
    auto h = juce::jmax (120, viewport.getMaximumVisibleHeight());
    int x = 0;

    for (auto id : order)
    {
        strips[id]->setBounds (x, 0, stripWidth, h);
        x += stripWidth + stripGap;
    }

    x += stripGap;
    masterStrip->setBounds (x, 0, stripWidth, h);
    x += stripWidth;

    stripHolder.setSize (juce::jmax (x, viewport.getMaximumVisibleWidth()), h);
}

void MixerView::resized()
{
    viewport.setBounds (getLocalBounds().reduced (18, 14));
    layoutStrips();
}

} // namespace ikaros
