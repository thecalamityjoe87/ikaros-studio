#pragma once

#include "engine/EngineTypes.h"
#include "ui/theme/Family.h"
#include "ui/widgets/IconButton.h"

namespace ikaros
{

class AudioEngine;

//==============================================================================
/**
    Tune's settings that aren't knobs, and its readout, laid out like the
    design's Tune panel. PluginEditor shows them with Tune's knobs, in its
    bordered box:

        ╭───────────────────────────────────────────────────────────────╮
        │ ⏻ TUNE                     [Key A minor · song ⌄] [Lock to Key] │  Done
        │ (Correction) (Speed) (Fine Tune)                 INPUT → TARGET │
        │                                            ┌ A4 -18   →    A4 ┐ │
        │                                            └ SUNG      SOUNDS ┘ │
        │ NOTES IT CAN LAND ON                                            │
        │ ┌C┬D┬E┬F┬G┬A┬B┐                             [Follow MIDI Off ⌄] │
        ╰───────────────────────────────────────────────────────────────╯

    The keys are one octave, drawn like the on-screen keyboard's: the notes
    Tune can land on are lit (with the track's colour along the bottom), the
    others dimmed; click one to switch it. Key and Follow MIDI open menus;
    Lock to Key is a toggle, amber while it's on. The readout is drawn like
    the transport's tuner LCD: the note being sung (amber, with how many
    cents it's off) and the note it's moved to (green), or dashes when
    there's no clear pitch.

    It covers the whole effect view but only its controls take the mouse,
    so the editor's own header buttons and knobs underneath still work.
    Every change is one undo step (EffectsController's Tune functions). The
    readout is polled (the audio thread writes it to atomics, see TunePlugin).
*/
class TuneControls  : public juce::Component,
                      private juce::Timer
{
public:
    TuneControls (AudioEngine&, TrackId, PluginId, tokens::Family);
    ~TuneControls() override;

    /** Re-reads the settings (after an edit or an undo). */
    void refresh();

    /** Places everything: `header` is the free part of the box's top row,
        after the name (Key and Lock to Key go at its right), `body` the
        part under it. Returns where the three knobs go. */
    juce::Rectangle<int> layOut (juce::Rectangle<int> header, juce::Rectangle<int> body);

    /** The panel's width (the editor's box, border included) and the
        height of its body (under the top row). */
    static constexpr int panelWidth = 560;
    static int getBodyHeight();

    void paint (juce::Graphics&) override;

private:
    class NoteKeys;
    class Readout;
    class Dropdown;

    AudioEngine& engine;
    const TrackId trackId;
    const PluginId pluginId;
    const tokens::Family family;

    std::unique_ptr<Dropdown> keyButton, followButton;
    IconButton lockButton { "Lock to Key", std::nullopt, "Lock to Key" };
    std::unique_ptr<NoteKeys> keys;
    std::unique_ptr<Readout> readout;

    juce::Rectangle<int> notesLabel, readoutLabel;

    void showKeyMenu();
    void showFollowMenu();
    void timerCallback() override;   // the readout

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TuneControls)
};

} // namespace ikaros
