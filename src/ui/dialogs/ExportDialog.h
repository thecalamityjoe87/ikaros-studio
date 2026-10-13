#pragma once

#include "engine/AudioEngine.h"
#include "engine/SongExporter.h"
#include "ui/widgets/IconButton.h"

namespace ikaros
{

//==============================================================================
/**
    File > Export Song / Export Stems: pick what to export (the whole song or
    the cycle range), the format and quality, and normalizing; then a file
    name. The export runs in the background (see SongExporter) while this
    shows its progress, with Cancel, then offers to show the files.

    Drawn inside the main window over a dimmed background, like the New
    Track dialog. It covers the window while exporting, so the session can't
    change halfway through. The choices are remembered for next time.
    Deletes itself when closed.
*/
class ExportDialog  : public juce::Component,
                      private juce::Timer
{
public:
    /** Opens the dialog over `parent`, with "one file per track" ticked or not. */
    static void show (juce::Component& parent, AudioEngine&, bool stems);

    ~ExportDialog() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    ExportDialog (AudioEngine&, bool stems);

    enum class Stage { options, exporting, done };

    AudioEngine& engine;
    SongExporter& exporter;
    Stage stage = Stage::options;

    juce::ComboBox rangeBox, formatBox, qualityBox;
    juce::ToggleButton normalizeToggle { "Normalize (as loud as it can be without clipping)" };
    juce::ToggleButton stemsToggle { "One file per track (stems)" };

    double progress = 0.0;                 // what the progress bar shows (it reads this variable)
    juce::ProgressBar progressBar { progress };

    IconButton cancelButton { "Cancel", std::nullopt, "Cancel" };
    IconButton exportButton { "Export", std::nullopt, "Export..." };
    IconButton revealButton { "Show in Folder", std::nullopt, "Show in Folder" };

    std::unique_ptr<juce::FileChooser> fileChooser;
    ExportResult result;
    juce::String message;                  // under the title: what's being exported, or how it went
    juce::Rectangle<int> panel, rows;

    ExportFormat getFormat() const;
    void updateQualityChoices();
    void updateMessage();
    int getMessageHeight() const;
    void chooseDestination();
    void startExport (const juce::File& destination);
    void exportFinished (const ExportResult&);
    void showStage (Stage);
    void close();

    void timerCallback() override;

    static constexpr int panelWidth = 520;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ExportDialog)
};

} // namespace ikaros
