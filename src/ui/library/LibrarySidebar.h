#pragma once

#include "ui/widgets/IconButton.h"
#include "ui/state/Selection.h"

namespace ikaros
{

//==============================================================================
/**
    The sound library on the left: search field, 2-column grid of category
    tiles, and the presets for the selected category. Clicking a preset assigns
    it to the selected track.

    TODO(milestone-3): replace the built-in preset table with a real sound
    library (sampler instruments, plugin presets, loops, audio files).
*/
class LibrarySidebar  : public juce::Component
{
public:
    LibrarySidebar (AudioEngine&, Selection&);
    ~LibrarySidebar() override;

    /** Re-reads the selected track's sound (to highlight the active preset). */
    void refresh();

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class CategoryTile;
    class PresetRow;

    AudioEngine& engine;
    Selection& selection;

    juce::TextEditor searchField;
    juce::OwnedArray<CategoryTile> tiles;   // OwnedArray deletes its objects when it's destroyed
    juce::Viewport presetViewport;
    juce::Component presetList;
    juce::OwnedArray<PresetRow> presetRows;

    juce::String selectedCategory { "Keyboards" };
    juce::String heading;
    juce::Rectangle<int> headingArea;

    void selectCategory (const juce::String&);
    void rebuildPresetList();
    void layoutPresetList();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LibrarySidebar)
};

} // namespace ikaros
