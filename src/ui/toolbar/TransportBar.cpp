#include "ui/toolbar/TransportBar.h"
#include "ui/widgets/PaintProbe.h"
#include "engine/AudioEngine.h"
#include "engine/TempoMap.h"
#include "engine/Tuner.h"
#include "ui/theme/Fonts.h"
#include "ui/theme/Tokens.h"

namespace ikaros
{

using namespace tokens;

namespace
{
    constexpr int buttonSize = 42;
    constexpr int playWidth = 54;
    constexpr int countInWidth = 96;   // timer icon + "Count-in"
    constexpr int gap = 6;
    constexpr int padding = 5;
}

TransportBar::TransportBar (AudioEngine& e) : engine (e)
{
    for (auto* b : { &toStartButton, &recordButton, &loopButton, &tunerButton, &metronomeButton, &countInButton })
    {
        b->setCornerRadius (size::radiusLarge);
        addAndMakeVisible (b);
    }

    // Neutral while stopped, amber while playing. The button never toggles
    // itself on click; update() sets its toggle state from the transport.
    playButton.setColours ([]
    {
        auto c = IconButton::accentColours();
        c.background = theme().neutralActive;
        c.foreground = theme().text;
        c.backgroundOn = theme().accent;
        c.border = theme().raisedBorder;
        c.borderOn = theme().accentBorder;
        return c;
    });
    playButton.setCornerRadius (size::radiusLarge);
    playButton.setIconSize (20.0f);
    addAndMakeVisible (playButton);

    // Red dot normally; lit red with a white dot while recording.
    recordButton.setColours ([] { return IconButton::Colours { theme().raised, theme().record, theme().record, theme().textOnColour }; });
    recordButton.setIconSize (16.0f);
    recordButton.setTooltip ("Record (R)");

    // Toggle buttons: setClickingTogglesState flips the toggle on click, and
    // the engine is updated from onClick. update() keeps them in sync if the
    // state changes elsewhere (menu, keyboard shortcut, loading a session).
    loopButton.setClickingTogglesState (true);
    tunerButton.setClickingTogglesState (true);
    metronomeButton.setClickingTogglesState (true);
    countInButton.setClickingTogglesState (true);

    // Clicks before recording starts. It turns on the length last chosen in
    // the Mix menu (1 bar to begin with). Like the other toggles it's only
    // filled with the accent while it's on.
    countInButton.setFont (fonts::uiSemiBold (textSize::md));
    countInButton.setIconSize (16.0f);
    countInButton.setTooltip ("Count-in: clicks before recording starts (1 or 2 bars: Mix > Count-in)");

    toStartButton.onClick   = [this] { engine.goToStart(); };
    playButton.onClick      = [this] { engine.togglePlay(); };
    recordButton.onClick    = [this] { engine.toggleRecord(); };
    loopButton.onClick      = [this] { engine.setLooping (loopButton.getToggleState()); };
    metronomeButton.onClick = [this] { engine.setMetronomeEnabled (metronomeButton.getToggleState()); };
    countInButton.onClick   = [this] { engine.setCountInEnabled (countInButton.getToggleState()); };

    // The tuner turns the LCD into a tuner for the selected track's input
    // (update() switches the LCD over). Escape or a second click turns it off.
    tunerButton.setTooltip ("Tuner: tune the selected track's input (Esc to close)");
    tunerButton.onClick = [this] { engine.getTuner().setActive (tunerButton.getToggleState()); };
    lcd.onTunerMuteClicked = [this]
    {
        auto& prefs = engine.getPreferences();
        prefs.set (settings::muteWhileTuning, ! prefs.get (settings::muteWhileTuning));
    };

    lcd.onBpmChanged = [this] (double bpm) { engine.setBpm (bpm); };

    // Dragging the BPM is one undo step, like dragging in the tempo lane:
    // start the step once, then move the tempo in effect at the playhead.
    lcd.onBpmDragStarted = [this]
    {
        bpmDragIndex = engine.getTempoMap().indexOfTempoAt (engine.getTransportState().positionBeats);
        engine.beginUndoStep ("Tempo");
    };
    lcd.onBpmDragged = [this] (double bpm)
    {
        const auto tempos = engine.getTempoMap().getTempoChanges();

        if (bpmDragIndex < (int) tempos.size())
            engine.setTempoChange (bpmDragIndex, tempos[(size_t) bpmDragIndex].beat, bpm);
    };
    lcd.onTimeSignatureChanged = [this] (int num, int den) { engine.setTimeSignature (num, den); };
    lcd.onKeyChanged = [this] (int root, bool minor) { engine.setKey (root, minor); };
    addAndMakeVisible (lcd);

    update();
}

int TransportBar::getPreferredWidth() const
{
    return padding * 2 + buttonSize * 5 + playWidth + countInWidth + LcdDisplay::preferredWidth + gap * 7 + 4;
}

void TransportBar::update()
{
    auto state = engine.getTransportState();

    if (state.playing != wasPlaying)
    {
        wasPlaying = state.playing;
        playButton.setIcon (state.playing ? Icon::pause : Icon::play);
        playButton.setTitle (state.playing ? "Pause" : "Play");
        playButton.setTooltip (state.playing ? "Pause (Space)" : "Play (Space)");
        playButton.setToggleState (state.playing, juce::dontSendNotification);
    }

    recordButton.setToggleState (state.recording, juce::dontSendNotification);
    recordButton.setTitle (state.recording ? "Stop recording" : "Record");
    loopButton.setToggleState (state.looping, juce::dontSendNotification);
    metronomeButton.setToggleState (state.metronome, juce::dontSendNotification);
    countInButton.setToggleState (state.countIn, juce::dontSendNotification);
    lcd.setState (state, engine.getKeyName());

    auto& tuner = engine.getTuner();
    tunerButton.setToggleState (tuner.isActive(), juce::dontSendNotification);
    lcd.setTunerShown (tuner.isActive());

    if (tuner.isActive())
        lcd.setTunerState (tuner.getReading(), tuner.hasInput(), engine.getPreferences().get (settings::muteWhileTuning));
}

void TransportBar::paint (juce::Graphics& g)
{
    IKAROS_PERF_PAINT ("TransportBar");
    auto r = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (theme().transportWell);
    g.fillRoundedRectangle (r, size::radiusPill);
    g.setColour (theme().transportWellBorder);
    g.drawRoundedRectangle (r, size::radiusPill, 1.0f);
}

void TransportBar::resized()
{
    auto r = getLocalBounds().reduced (padding);

    // Short of room (a narrow window), Count-in drops its label and keeps
    // just the icon; the tooltip still says what it is.
    const bool compact = getWidth() < getPreferredWidth();
    countInButton.setText (compact ? juce::String() : juce::String ("Count-in"));
    const int countIn = compact ? buttonSize : countInWidth;

    auto place = [&] (juce::Component& c, int w)
    {
        c.setBounds (r.removeFromLeft (w).withSizeKeepingCentre (w, buttonSize));
        r.removeFromLeft (gap);
    };

    place (toStartButton, buttonSize);
    place (playButton, playWidth);
    place (recordButton, buttonSize);
    place (loopButton, buttonSize);
    r.removeFromLeft (4);
    // The LCD takes what's left after the gap behind it, the metronome, its
    // gap and Count-in, so Count-in keeps the same inset as Go to start.
    place (lcd, juce::jmax (120, r.getWidth() - gap - (buttonSize + gap) * 2 - countIn));
    place (tunerButton, buttonSize);
    place (metronomeButton, buttonSize);
    place (countInButton, countIn);
}

} // namespace ikaros
