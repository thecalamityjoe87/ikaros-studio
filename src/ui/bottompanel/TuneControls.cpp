#include "ui/bottompanel/TuneControls.h"
#include "engine/AudioEngine.h"
#include "engine/TrackController.h"
#include "ui/widgets/Knob.h"
#include "ui/theme/Fonts.h"
#include "ui/theme/Tokens.h"
#include "ui/toolbar/LcdDisplay.h"

namespace ikaros
{

using namespace tokens;

namespace
{
    const juce::String dot = juce::String::fromUTF8 ("\xc2\xb7 ");
    const juce::String arrow = juce::String::fromUTF8 ("\xe2\x86\x92");

    // Sizes from the design's Tune panel.
    constexpr int knobGap = 14;
    constexpr int labelHeight = 18;
    constexpr int keysHeight = 56, whiteKeyWidth = 42;
    constexpr int readoutWidth = 196, readoutHeight = 62;
    constexpr int dropdownHeight = 28, keyDropdownWidth = 172, lockWidth = 96;
    constexpr int followWidth = 168, followHeight = 32;
    constexpr int rowGap = 10;

    // A MIDI note's name and octave, as the tuner shows them ("A", 4).
    std::pair<juce::String, int> nameOf (int note)
    {
        return { juce::MidiMessage::getMidiNoteName (note, true, false, 4), note / 12 - 1 };
    }

    // "A minor", "Eb major": the app's key names ("A min"), spelt out.
    juce::String longKeyName (int root, bool minor)
    {
        return getKeyName (root, minor).upToLastOccurrenceOf (" ", false, false) + (minor ? " minor" : " major");
    }
}

//==============================================================================
// A dropdown like the design's: a caption, the value, a quieter note after
// it, and a chevron ("Key  A minor · song ⌄"), in a bordered pill.
class TuneControls::Dropdown  : public juce::Button
{
public:
    explicit Dropdown (const juce::String& name) : juce::Button (name)
    {
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    void setParts (const juce::String& newCaption, const juce::String& newValue, const juce::String& newNote)
    {
        if (newCaption == caption && newValue == value && newNote == note)
            return;

        caption = newCaption;
        value = newValue;
        note = newNote;
        setTitle (caption + " " + value);
        repaint();
    }

    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        const auto colours = IconButton::borderedColours();
        const auto r = getLocalBounds().toFloat().reduced (0.5f);

        g.setColour (down ? colours.background.darker (0.1f) : over ? colours.background.brighter (0.06f) : colours.background);
        g.fillRoundedRectangle (r, size::radiusSmall);
        g.setColour (colours.border);
        g.drawRoundedRectangle (r, size::radiusSmall, 1.0f);

        auto area = getLocalBounds().reduced (12, 0);
        icons::draw (g, Icon::chevronDown, area.removeFromRight (12).toFloat(), theme().textSecondary, 12.0f);
        area.removeFromRight (6);

        auto part = [&] (const juce::String& text, juce::Font font, juce::Colour colour)
        {
            const int width = juce::GlyphArrangement::getStringWidthInt (font, text);
            g.setFont (font);
            g.setColour (colour);
            g.drawText (text, area.removeFromLeft (width + 1), juce::Justification::centredLeft, false);
            area.removeFromLeft (6);
        };

        part (caption, fonts::ui (textSize::md), theme().textSecondary);
        part (value, fonts::uiSemiBold (textSize::md), theme().text);

        if (note.isNotEmpty())
            part (note, fonts::ui (textSize::sm), theme().textSecondary);
    }

private:
    juce::String caption, value, note;
};

//==============================================================================
// One octave of keys: lit where Tune can land, dimmed where it can't.
class TuneControls::NoteKeys  : public juce::Component,
                                public juce::SettableTooltipClient
{
public:
    explicit NoteKeys (TuneControls& o) : owner (o)
    {
        setTitle ("Notes it can land on");
        setTooltip ("The notes Tune can move the voice to. Click a note to allow it or leave it out");
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    void setAllowed (int mask)
    {
        if (mask != allowed)
        {
            allowed = mask;
            repaint();
        }
    }

    static constexpr int idealWidth = whiteKeyWidth * 7;

    void paint (juce::Graphics& g) override
    {
        const auto colour = theme().family (owner.family);

        for (int w = 0; w < 7; ++w)
        {
            const int note = whiteNotes[w];
            const bool lit = isAllowed (note);
            auto key = whiteKey (w).reduced (1.0f, 0.0f);

            g.setColour (lit ? theme().pianoWhite : theme().pianoWhite.withAlpha (0.25f));
            g.fillRoundedRectangle (key.withTrimmedTop (-6.0f), size::radiusSmall);   // round only the bottom corners

            if (lit)
            {
                g.setColour (colour);
                g.fillRoundedRectangle (key.removeFromBottom (3.0f), 1.5f);
            }

            g.setColour (theme().pianoLabel.withAlpha (lit ? 1.0f : 0.5f));
            g.setFont (fonts::uiSemiBold (textSize::sm));
            g.drawText (juce::MidiMessage::getMidiNoteName (note, true, false, 4), whiteKey (w).withTrimmedBottom (4.0f).removeFromBottom (18.0f),
                        juce::Justification::centred);
        }

        for (int b = 0; b < 5; ++b)
        {
            const bool lit = isAllowed (blackNotes[b]);
            auto key = blackKey (b);

            g.setColour (lit ? theme().pianoBlack : theme().pianoBlack.withAlpha (0.35f));
            g.fillRoundedRectangle (key.withTrimmedTop (-4.0f), size::radiusXs);

            if (lit)
            {
                g.setColour (colour);
                g.fillRoundedRectangle (key.removeFromBottom (3.0f), 1.5f);
            }
        }
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (! e.mouseWasClicked())
            return;

        if (const int note = noteAt (e.position); note >= 0)
            owner.engine.getTracks().effects().toggleTuneNote (owner.trackId, owner.pluginId, note);

        owner.refresh();
    }

private:
    TuneControls& owner;
    int allowed = 0xfff;

    static constexpr int whiteNotes[7] { 0, 2, 4, 5, 7, 9, 11 };
    static constexpr int blackNotes[5] { 1, 3, 6, 8, 10 };
    static constexpr int blackAfterWhite[5] { 0, 1, 3, 4, 5 };   // each black key sits after this white key

    bool isAllowed (int note) const   { return (allowed & (1 << note)) != 0; }

    juce::Rectangle<float> whiteKey (int w) const
    {
        return { (float) (w * whiteKeyWidth), 0.0f, (float) whiteKeyWidth, (float) getHeight() };
    }

    juce::Rectangle<float> blackKey (int b) const
    {
        const float width = whiteKeyWidth * 0.58f;
        const float x = (float) ((blackAfterWhite[b] + 1) * whiteKeyWidth) - width * 0.5f;
        return { x, 0.0f, width, (float) getHeight() * 0.58f };
    }

    int noteAt (juce::Point<float> p) const
    {
        for (int b = 0; b < 5; ++b)
            if (blackKey (b).contains (p))
                return blackNotes[b];

        for (int w = 0; w < 7; ++w)
            if (whiteKey (w).contains (p))
                return whiteNotes[w];

        return -1;
    }
};

//==============================================================================
// INPUT -> TARGET, drawn like the tuner's LCD: the sung note on the left,
// the target on the right.
class TuneControls::Readout  : public juce::Component
{
public:
    Readout()
    {
        setTitle ("Input and target notes");
    }

    void setReading (const EffectsController::TuneReading& r)
    {
        const bool same = r.voiced == reading.voiced
                           && (! r.voiced || (std::lround (r.sungNote * 100.0f) == std::lround (reading.sungNote * 100.0f)
                                              && r.targetNote == reading.targetNote));

        if (! same)
        {
            reading = r;
            repaint();
        }
    }

    void paint (juce::Graphics& g) override
    {
        LcdDisplay::paintPanel (g, getLocalBounds().toFloat());

        const int rowHeight = LcdDisplay::getValueRowHeight();
        const auto inner = getLocalBounds().reduced (18, 9);
        const auto valueRow = inner.withHeight (rowHeight);
        const auto captionRow = inner.withTrimmedTop (rowHeight + 3);
        const auto nameFont = fonts::monoBold (textSize::lcd), octaveFont = fonts::monoBold (textSize::md);

        auto noteWidth = [&] (const juce::String& name, int octave)
        {
            return juce::GlyphArrangement::getStringWidthInt (nameFont, name) + 1
                   + juce::GlyphArrangement::getStringWidthInt (octaveFont, juce::String (octave));
        };

        // Sung: the nearest note and how far off it is.
        if (reading.voiced)
        {
            const int nearest = (int) std::lround (reading.sungNote);
            const auto [name, octave] = nameOf (nearest);
            const auto cents = (int) std::lround ((reading.sungNote - (float) nearest) * 100.0f);

            LcdDisplay::paintNote (g, valueRow, name, octave, theme().lcdValue);
            g.setColour (theme().lcdValue);
            g.setFont (fonts::mono (textSize::sm));
            g.drawText (cents > 0 ? "+" + juce::String (cents) : juce::String (cents),
                        valueRow.withTrimmedLeft (noteWidth (name, octave) + 6).withTrimmedBottom (3),
                        juce::Justification::bottomLeft, false);
        }
        else
        {
            LcdDisplay::paintBlank (g, valueRow, 11.0f);
        }

        // Target: right-aligned.
        if (reading.voiced && reading.targetNote >= 0)
        {
            const auto [name, octave] = nameOf (reading.targetNote);
            const int width = noteWidth (name, octave);
            LcdDisplay::paintNote (g, valueRow.withTrimmedLeft (valueRow.getWidth() - width), name, octave, theme().meterGreen);
        }
        else
        {
            LcdDisplay::paintBlank (g, valueRow.withTrimmedLeft (valueRow.getWidth() - 13), 11.0f);
        }

        g.setColour (theme().lcdLabel);
        g.setFont (fonts::ui (textSize::xl));
        g.drawText (arrow, inner, juce::Justification::centred);

        LcdDisplay::paintCaption (g, captionRow, "SUNG", juce::Justification::topLeft);
        LcdDisplay::paintCaption (g, captionRow, "SOUNDS", juce::Justification::topRight);
    }

private:
    EffectsController::TuneReading reading;
};

//==============================================================================
TuneControls::TuneControls (AudioEngine& e, TrackId t, PluginId p, Family f)
    : engine (e), trackId (t), pluginId (p), family (f)
{
    // Only the controls take the mouse: the editor's knobs and header
    // buttons underneath this component still get their clicks.
    setInterceptsMouseClicks (false, true);

    keyButton = std::make_unique<Dropdown> ("Key");
    keyButton->setTooltip ("The key Tune keeps the voice in: the song's, or one of its own");
    keyButton->onClick = [this] { showKeyMenu(); };
    addAndMakeVisible (*keyButton);

    followButton = std::make_unique<Dropdown> ("Follow MIDI");
    followButton->setTooltip ("Follow the notes of a MIDI track: while they play, they're the notes Tune moves the voice to");
    followButton->onClick = [this] { showFollowMenu(); };
    addAndMakeVisible (*followButton);

    // Outlined in amber while it's on, as in the design.
    lockButton.setColours ([]
    {
        auto c = IconButton::borderedColours();
        c.foreground = theme().textSecondary;
        c.backgroundOn = theme().accent.withAlpha (0.1f);
        c.foregroundOn = theme().accent;
        c.borderOn = theme().accent;
        return c;
    });
    lockButton.setFont (fonts::uiSemiBold (textSize::md));
    lockButton.setCornerRadius (size::radiusSmall);
    lockButton.onClick = [this]
    {
        const auto tune = engine.getTracks().effects().getTune (trackId, pluginId);
        engine.getTracks().effects().setTuneLockToKey (trackId, pluginId, ! tune.lockToKey);
        refresh();
    };
    addAndMakeVisible (lockButton);

    keys = std::make_unique<NoteKeys> (*this);
    addAndMakeVisible (*keys);
    readout = std::make_unique<Readout>();
    addAndMakeVisible (*readout);

    refresh();
    startTimerHz (15);
}

TuneControls::~TuneControls() = default;

void TuneControls::refresh()
{
    auto& tracks = engine.getTracks();
    const auto tune = tracks.effects().getTune (trackId, pluginId);

    if (! tune.exists)
        return;

    keyButton->setParts ("Key", longKeyName (tune.keyRoot, tune.keyMinor), tune.key < 0 ? dot + "song" : juce::String());
    lockButton.setToggleState (tune.lockToKey, juce::dontSendNotification);
    lockButton.setTooltip (tune.lockToKey ? "Locked to the key: Tune lands on the key's notes (click to allow all 12)"
                                          : "Tune lands on any of the 12 notes (click to lock it to the key)");

    juce::String follow ("Off");

    if (tune.followTrack != 0)
        if (auto t = tracks.getTrack (tune.followTrack))
            follow = t->name;

    followButton->setParts ("Follow MIDI", follow, {});
    keys->setAllowed (tune.allowedNotes);
}

int TuneControls::getBodyHeight()
{
    return Knob::preferredHeight + rowGap + labelHeight + 4 + keysHeight;
}

juce::Rectangle<int> TuneControls::layOut (juce::Rectangle<int> header, juce::Rectangle<int> body)
{
    // Header: Key and Lock to Key at its right.
    auto h = header.withSizeKeepingCentre (header.getWidth(), dropdownHeight);
    lockButton.setBounds (h.removeFromRight (lockWidth));
    h.removeFromRight (8);
    keyButton->setBounds (h.removeFromRight (juce::jmin (keyDropdownWidth, h.getWidth())));

    // Top: the knobs on the left, the readout (with its caption) on the right.
    auto top = body.removeFromTop (Knob::preferredHeight);
    const auto knobArea = top.removeFromLeft (Knob::preferredWidth * 3 + knobGap * 2);
    auto readoutArea = top.removeFromRight (readoutWidth).withHeight (labelHeight + 6 + readoutHeight);
    readoutLabel = readoutArea.removeFromTop (labelHeight);
    readoutArea.removeFromTop (6);
    readout->setBounds (readoutArea);
    body.removeFromTop (rowGap);

    // Bottom: the notes on the left, Follow MIDI on the right, along the bottom.
    notesLabel = body.removeFromTop (labelHeight).withWidth (NoteKeys::idealWidth + 60);
    body.removeFromTop (4);
    keys->setBounds (body.removeFromLeft (NoteKeys::idealWidth).withHeight (keysHeight));
    followButton->setBounds (body.removeFromRight (followWidth).withHeight (keysHeight).removeFromBottom (followHeight));

    repaint();
    return knobArea;
}

void TuneControls::showKeyMenu()
{
    const auto tune = engine.getTracks().effects().getTune (trackId, pluginId);
    const auto songKey = engine.getTransportState();
    juce::PopupMenu menu, major, minor;

    auto choose = [this] (int key)
    {
        return [this, key]
        {
            engine.getTracks().effects().setTuneKey (trackId, pluginId, key);
            refresh();
        };
    };

    menu.addItem (juce::PopupMenu::Item ("Song (" + longKeyName (songKey.keyRoot, songKey.keyMinor) + ")")
                      .setTicked (tune.key < 0).setAction (choose (-1)));
    menu.addSeparator();

    for (int root = 0; root < 12; ++root)
    {
        major.addItem (juce::PopupMenu::Item (longKeyName (root, false)).setTicked (tune.key == root).setAction (choose (root)));
        minor.addItem (juce::PopupMenu::Item (longKeyName (root, true)).setTicked (tune.key == root + 12).setAction (choose (root + 12)));
    }

    menu.addSubMenu ("Major", major);
    menu.addSubMenu ("Minor", minor);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (keyButton.get()));
}

void TuneControls::showFollowMenu()
{
    auto& tracks = engine.getTracks();
    const auto tune = tracks.effects().getTune (trackId, pluginId);
    juce::PopupMenu menu;

    auto choose = [this] (TrackId id)
    {
        return [this, id]
        {
            engine.getTracks().effects().setTuneFollowMidi (trackId, pluginId, id);
            refresh();
        };
    };

    menu.addItem (juce::PopupMenu::Item ("Off").setTicked (tune.followTrack == 0).setAction (choose (0)));

    // MIDI instrument tracks (a Drummer's notes aren't a melody).
    bool any = false;

    for (auto& t : tracks.getTracks())
    {
        if (t.kind != TrackKind::instrument || t.drummer)
            continue;

        if (! any)
            menu.addSeparator();

        any = true;
        menu.addItem (juce::PopupMenu::Item (t.name).setTicked (tune.followTrack == t.id).setAction (choose (t.id)));
    }

    if (! any)
        menu.addItem (juce::PopupMenu::Item ("No MIDI tracks in this session").setEnabled (false));

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (followButton.get()));
}

void TuneControls::timerCallback()
{
    readout->setReading (engine.getTracks().effects().getTuneReading (trackId, pluginId));
}

void TuneControls::paint (juce::Graphics& g)
{
    // Section captions, in small capitals.
    g.setColour (theme().textSecondary);
    g.setFont (fonts::uiSemiBold (textSize::sm).withExtraKerningFactor (0.08f));
    g.drawText ("NOTES IT CAN LAND ON", notesLabel, juce::Justification::centredLeft);
    g.drawText ("INPUT " + arrow + " TARGET", readoutLabel, juce::Justification::centredRight);
}

} // namespace ikaros
