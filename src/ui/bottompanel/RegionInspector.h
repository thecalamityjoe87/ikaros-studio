#pragma once

#include "engine/EngineTypes.h"
#include "ui/bottompanel/EditorGrid.h"
#include "ui/theme/Tokens.h"
#include "ui/widgets/CollapsibleSection.h"
#include <array>
#include <optional>

namespace ikaros
{

class AudioEngine;

//==============================================================================
/**
    The left column of the bottom panel while the Editor shows a region and
    Region is picked (Track | Region): the region's own settings, like
    GarageBand's region inspector. Grouped in collapsible sections, as in the
    track inspector:

      Audio region   Gain, Pitch, Reverse (and whether it follows the key), Flex
      Editing        Grid, Quantize, and Quantize's strength and swing
      Pitch          audio regions: Pitch mode on/off (the same switch as the
                     Editor's Waveform | Pitch), the song's key, Snap to Key,
                     Vibrato, Note transitions, Correct All and Reset (see
                     PitchEditor). They're greyed out while Pitch mode is
                     off. The sliders work on the notes selected in the
                     Editor, or on every note if none are.

    In Pitch mode the Pitch section opens, and Audio and Editing fold up to
    their headers (they keep their places, and can still be opened). Leaving
    Pitch mode opens or closes them as they were.

    The audio settings go straight to the session (RegionEditor), each as an
    undo step. The grid and Quantize belong to the editors, so they're
    passed on through onGridChanged, onQuantize and onQuantizeFeelChanged.
    (The grid, strength and swing are how you edit, not part of the song:
    they aren't saved with the session, and changing them isn't an undo step.)
*/
class RegionInspector  : public juce::Component
{
public:
    explicit RegionInspector (AudioEngine&);
    ~RegionInspector() override;   // defined in the .cpp, where the sections' contents are complete types

    void setRegion (std::optional<RegionInfo>, tokens::Family);

    EditorGrid getGrid() const noexcept     { return grid; }

    /** The grid was changed: the owner passes it on to the editors. */
    std::function<void (EditorGrid)> onGridChanged;

    /** Quantize was clicked: the editor showing the region quantizes it. */
    std::function<void()> onQuantize;

    /** Quantize's strength or swing was changed (each 0..1, see QuantizeSettings). */
    std::function<void (double strength, double swing)> onQuantizeFeelChanged;

    /** The notes selected in the audio editor's Pitch mode (by their start
        in the recording; empty = none), so the Pitch section's sliders
        work on them. */
    void setPitchSelection (std::vector<double> noteStarts);

    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int preferredWidth = tokens::size::trackHeaderWidth;

private:
    class AudioControls;
    class EditingControls;
    class PitchControls;

    AudioEngine& engine;
    std::optional<RegionInfo> region;
    EditorGrid grid = EditorGrid::sixteenth;

    // unique_ptr: the contents are only declared here (defined in the .cpp).
    // The sections refer to them, so they're made after them, and C++
    // destroys members in reverse order: the sections go first.
    std::unique_ptr<AudioControls> audioControls;
    std::unique_ptr<EditingControls> editingControls;
    std::unique_ptr<PitchControls> pitchControls;
    std::unique_ptr<CollapsibleSection> audioSection, editingSection, pitchSection;
    std::vector<double> pitchSelection;

    // In Pitch mode the others fold up; how they were before, to put them
    // back afterwards.
    bool pitchLayout = false;
    std::array<bool, 3> savedExpanded { true, true, true };   // Audio, Editing, Pitch

    juce::Viewport viewport;
    juce::Component sectionHolder;

    void showGridMenu (juce::Component& target);
    void layoutSections();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RegionInspector)
};

} // namespace ikaros
