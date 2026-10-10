#pragma once

#include "ui/state/Selection.h"
#include "ui/widgets/IconButton.h"

namespace ikaros
{

//==============================================================================
/**
    The "Plug-ins" section of the track inspector: the track's instrument and
    its chain of effects, in processing order (top to bottom).

      - the power button turns an effect on/off (bypass)
      - clicking an effect opens its settings in the bottom panel's main area
      - the ⋯ button offers Replace and Remove
      - drag an effect up/down to reorder the chain
      - "+ Add effect" adds a built-in effect at the end
*/
class PluginChainView  : public juce::Component
{
public:
    PluginChainView (AudioEngine&, Selection&);
    ~PluginChainView() override;

    /** Re-reads the selected track's plug-ins (model or selection changed). */
    void refresh();

    /** The height this view needs to show everything at its current width. */
    int getIdealHeight() const;

    /** Called when the ideal height changes, so the owner can re-layout. */
    std::function<void()> onHeightChanged;

    /** The user asked to see an effect's settings (clicked it, or just added it). */
    std::function<void (PluginId)> onOpenEffect;

    /** Highlights the effect whose settings are showing (0 = none). */
    void setOpenEffect (PluginId);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class EffectRow;
    class AddRow;
    friend class EffectRow;

    AudioEngine& engine;
    Selection& selection;

    TrackId trackId = 0;
    std::optional<PluginSlot> instrument;
    std::vector<PluginSlot> chain;
    juce::Colour trackColour;

    juce::OwnedArray<EffectRow> rows;
    std::unique_ptr<AddRow> addRow;

    // An effect that was just added; its settings open once its row exists.
    PluginId openWhenShown = 0;
    PluginId openEffectId = 0;   // the effect currently shown in the main area

    // Drag-to-reorder state
    int dragFromIndex = -1, dropIndex = -1;

    juce::Rectangle<int> hintArea, instrumentLabelArea, instrumentArea, effectsLabelArea;

    // Called by EffectRow
    void openEditor (int rowIndex);
    void showRowMenu (int rowIndex);
    void dragRow (int rowIndex, int yInThisComponent);
    void endDrag();

    void showAddMenu();
    static juce::PopupMenu buildEffectMenu (std::function<void (const EffectType&)> onChosen);

    static constexpr int rowHeight = 34, rowGap = 4, labelHeight = 18;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginChainView)
};

} // namespace ikaros
