#include "ui/toolbar/TransportBar.h"
#include "engine/AudioEngine.h"
#include "ui/theme/Fonts.h"
#include "ui/theme/Tokens.h"

namespace ikaros
{

using namespace tokens;

namespace
{
    constexpr int buttonSize = 42;
    constexpr int playWidth = 54;
    constexpr int gap = 6;
    constexpr int padding = 5;
}

TransportBar::TransportBar (AudioEngine& e) : engine (e)
{
    for (auto* b : { &toStartButton, &recordButton, &loopButton, &metronomeButton, &countInButton })
    {
        b->setCornerRadius (size::radiusLarge);
        addAndMakeVisible (b);
    }

    playButton.setColours (IconButton::accentColours());
    playButton.setCornerRadius (size::radiusLarge);
    playButton.setIconSize (20.0f);
    addAndMakeVisible (playButton);

    // Red dot normally; lit red with a white dot while recording.
    recordButton.setColours ({ colour::raised, colour::record, colour::record, colour::textOnColour });
    recordButton.setIconSize (16.0f);
    recordButton.setTooltip ("Record (R)");

    // Toggle buttons: setClickingTogglesState flips the toggle on click, and
    // the engine is updated from onClick. update() keeps them in sync if the
    // state changes elsewhere (menu, keyboard shortcut, loading a session).
    loopButton.setClickingTogglesState (true);
    metronomeButton.setClickingTogglesState (true);
    countInButton.setClickingTogglesState (true);

    // "1234", like a count-in: clicks before recording starts. It turns on
    // the length last chosen in the Mix menu (1 bar to begin with).
    countInButton.setFont (fonts::monoBold (textSize::xs));
    countInButton.setTooltip ("Count-in: clicks before recording starts (1 or 2 bars: Mix > Count-in)");

    toStartButton.onClick   = [this] { engine.goToStart(); };
    playButton.onClick      = [this] { engine.togglePlay(); };
    recordButton.onClick    = [this] { engine.toggleRecord(); };
    loopButton.onClick      = [this] { engine.setLooping (loopButton.getToggleState()); };
    metronomeButton.onClick = [this] { engine.setMetronomeEnabled (metronomeButton.getToggleState()); };
    countInButton.onClick   = [this] { engine.setCountInEnabled (countInButton.getToggleState()); };

    lcd.onBpmChanged = [this] (double bpm) { engine.setBpm (bpm); };
    lcd.onTimeSignatureChanged = [this] (int num, int den) { engine.setTimeSignature (num, den); };
    lcd.onKeyChanged = [this] (int root, bool minor) { engine.setKey (root, minor); };
    addAndMakeVisible (lcd);

    update();
}

int TransportBar::getPreferredWidth() const
{
    return padding * 2 + buttonSize * 5 + playWidth + LcdDisplay::preferredWidth + gap * 6 + 4;
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
    }

    recordButton.setToggleState (state.recording, juce::dontSendNotification);
    recordButton.setTitle (state.recording ? "Stop recording" : "Record");
    loopButton.setToggleState (state.looping, juce::dontSendNotification);
    metronomeButton.setToggleState (state.metronome, juce::dontSendNotification);
    countInButton.setToggleState (state.countIn, juce::dontSendNotification);
    lcd.setState (state, engine.getKeyName());
}

void TransportBar::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (colour::wellBg);
    g.fillRoundedRectangle (r, size::radiusPill);
    g.setColour (colour::borderSoft);
    g.drawRoundedRectangle (r, size::radiusPill, 1.0f);
}

void TransportBar::resized()
{
    auto r = getLocalBounds().reduced (padding);

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
    place (lcd, juce::jmax (120, r.getWidth() - (buttonSize + gap) * 2));
    place (metronomeButton, buttonSize);
    place (countInButton, buttonSize);
}

} // namespace ikaros
