#include "ui/dialogs/ExportDialog.h"
#include "ui/theme/Fonts.h"
#include "ui/theme/Tokens.h"

namespace ikaros
{

using namespace tokens;

namespace
{
    // The formats, in the order the Format menu lists them.
    const ExportFormat formats[] { ExportFormat::wav, ExportFormat::mp3, ExportFormat::flac, ExportFormat::ogg };
    const char* const formatNames[] { "WAV", "MP3 (small, plays everywhere)", "FLAC (lossless, smaller)", "OGG Vorbis (small)" };

    // Whether to export just the cycle range isn't a preference (it depends
    // on the session), but it's remembered too: app state, not part of a session.
    const juce::String cycleSetting ("exportCycle");

    constexpr int labelWidth = 64, rowHeight = 30, rowGap = 12;

    constexpr int padX = 28, padY = 24, titleHeight = 26, lineHeight = 18;

    juce::String formatDuration (double seconds)
    {
        const auto total = juce::roundToInt (seconds);
        return juce::String (total / 60) + ":" + juce::String (total % 60).paddedLeft ('0', 2);
    }
}

void ExportDialog::show (juce::Component& parent, AudioEngine& engine, bool stems)
{
    // Owned by its parent from here on; it deletes itself in close().
    auto* dialog = new ExportDialog (engine, stems);
    parent.addAndMakeVisible (dialog);
    dialog->setBounds (parent.getLocalBounds());
    dialog->exportButton.grabKeyboardFocus();
}

ExportDialog::ExportDialog (AudioEngine& e, bool stems)
    : engine (e), exporter (e.getExporter())
{
    setTitle ("Export");
    setWantsKeyboardFocus (true);
    setAlwaysOnTop (true);

    // What to export.
    const auto transport = engine.getTransportState();
    const auto tempo = engine.getTempoMap();   // bars can differ in length
    const auto barsFrom = tempo.nearestBar (transport.loopStartBeats) + 1;
    const auto barsTo = juce::jmax (barsFrom, tempo.nearestBar (transport.loopEndBeats));
    rangeBox.addItem ("The whole song", 1);
    rangeBox.addItem ("Just the cycle range (bars " + juce::String (barsFrom) + " to " + juce::String (barsTo) + ")", 2);
    rangeBox.setItemEnabled (2, exporter.hasCycleRange());
    rangeBox.setSelectedId (exporter.hasCycleRange() && (bool) engine.getAppSetting (cycleSetting, false) ? 2 : 1,
                            juce::dontSendNotification);
    rangeBox.setTitle ("Export");
    rangeBox.onChange = [this] { updateMessage(); };

    // Format and quality.
    for (int i = 0; i < juce::numElementsInArray (formatNames); ++i)
        formatBox.addItem (formatNames[i], i + 1);

    const auto savedFormat = engine.getPreferences().get (settings::exportFormat);
    formatBox.setSelectedItemIndex (0, juce::dontSendNotification);

    for (int i = 0; i < juce::numElementsInArray (formats); ++i)
        if (SongExporter::getFileExtension (formats[i]) == savedFormat)
            formatBox.setSelectedItemIndex (i, juce::dontSendNotification);

    formatBox.setTitle ("Format");
    formatBox.onChange = [this] { updateQualityChoices(); };
    qualityBox.setTitle ("Quality");
    updateQualityChoices();

    normalizeToggle.setToggleState (engine.getPreferences().get (settings::exportNormalize), juce::dontSendNotification);
    stemsToggle.setToggleState (stems, juce::dontSendNotification);
    stemsToggle.setTooltip ("Each track in its own file, all the same length, for mixing somewhere else. "
                            "Muted tracks are left out.");
    stemsToggle.onClick = [this] { updateMessage(); };

    for (auto* c : std::initializer_list<juce::Component*> { &rangeBox, &formatBox, &qualityBox, &normalizeToggle, &stemsToggle })
        addAndMakeVisible (c);

    addChildComponent (progressBar);
    addChildComponent (revealButton);

    auto cancelColours = IconButton::raisedColours();
    cancelColours.border = colour::border;
    cancelButton.setColours (cancelColours);
    cancelButton.onClick = [this]
    {
        if (stage == Stage::exporting)
            exporter.cancel();      // exportFinished() closes the dialog
        else
            close();
    };
    addAndMakeVisible (cancelButton);

    revealButton.setColours (cancelColours);
    revealButton.onClick = [this]
    {
        // Stems: open their folder. One file: show it in its folder.
        if (! result.files.isEmpty())
            (stemsToggle.getToggleState() ? result.files[0].getParentDirectory() : result.files[0]).revealToUser();
    };

    exportButton.setColours (IconButton::primaryColours());
    exportButton.onClick = [this]
    {
        if (stage == Stage::done)
            close();
        else
            chooseDestination();
    };
    addAndMakeVisible (exportButton);

    updateMessage();
}

ExportDialog::~ExportDialog()
{
    // Normally finished by now; if the window closes first, stop it.
    if (stage == Stage::exporting)
        exporter.cancelAndWait();
}

//==============================================================================
ExportFormat ExportDialog::getFormat() const
{
    return formats[juce::jlimit (0, juce::numElementsInArray (formats) - 1, formatBox.getSelectedItemIndex())];
}

void ExportDialog::updateQualityChoices()
{
    const auto format = getFormat();
    const auto choices = SongExporter::getQualityChoices (format);

    // Defaults: 24-bit for WAV and FLAC, 192 kbps for OGG, 320 kbps for MP3.
    const bool bitRate = format == ExportFormat::ogg || format == ExportFormat::mp3;
    const auto extension = SongExporter::getFileExtension (format);
    const auto preferred = engine.getPreferences().get (settings::exportQuality (extension.toRawUTF8()));
    qualityBox.clear (juce::dontSendNotification);

    for (size_t i = 0; i < choices.size(); ++i)
    {
        auto text = SongExporter::describeQuality (format, choices[i]);

        if (! bitRate)
            text << (choices[i] == 16 ? " (CD quality)" : " (studio quality)");

        qualityBox.addItem (text, choices[i]);   // the item ID is the setting itself
    }

    const bool known = std::find (choices.begin(), choices.end(), preferred) != choices.end();
    qualityBox.setSelectedId (known ? preferred : choices.back(), juce::dontSendNotification);
}

void ExportDialog::updateMessage()
{
    if (stemsToggle.getToggleState())
    {
        const auto names = exporter.getStemNames();
        message = names.isEmpty() ? juce::String ("There are no tracks to export: every track is empty or muted.")
                                  : juce::String (names.size()) + (names.size() == 1 ? " track" : " tracks")
                                        + ", one file each: " + names.joinIntoString (", ");
    }
    else if (rangeBox.getSelectedId() == 2)
    {
        const auto t = engine.getTransportState();
        message = "\"" + engine.getSessionName() + "\", "
                  + formatDuration ((t.loopEndBeats - t.loopStartBeats) * 60.0 / t.bpm) + " of it";
    }
    else
    {
        message = "\"" + engine.getSessionName() + "\", "
                  + formatDuration (exporter.getSongEndBeats() * 60.0 / engine.getTransportState().bpm)
                  + " plus any reverb or delay tail";
    }

    exportButton.setEnabled (exporter.getSongEndBeats() > 0.0
                             && (! stemsToggle.getToggleState() || ! exporter.getStemNames().isEmpty()));
    resized();   // the message may now take one line more or less
    repaint();
}

//==============================================================================
void ExportDialog::chooseDestination()
{
    // Remember the choices, whether or not the export goes ahead.
    auto& prefs = engine.getPreferences();
    const auto format = getFormat();
    const auto extension = SongExporter::getFileExtension (format);
    prefs.set (settings::exportFormat, extension);
    prefs.set (settings::exportQuality (extension.toRawUTF8()), qualityBox.getSelectedId());
    prefs.set (settings::exportNormalize, normalizeToggle.getToggleState());
    engine.setAppSetting (cycleSetting, rangeBox.getSelectedId() == 2);

    const auto folder = prefs.getFolder (settings::exportFolder, juce::File::getSpecialLocation (juce::File::userMusicDirectory));

    const bool stems = stemsToggle.getToggleState();
    const auto name = juce::File::createLegalFileName (engine.getSessionName());

    // Stems go in a folder of their own, named here.
    fileChooser = stems ? std::make_unique<juce::FileChooser> ("Export Stems To a New Folder", folder.getChildFile (name + " Stems"))
                        : std::make_unique<juce::FileChooser> ("Export Song", folder.getChildFile (name + "." + extension),
                                                               "*." + extension);

    // The chooser runs asynchronously; SafePointer turns into nullptr if this
    // dialog has gone by the time it answers.
    fileChooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                                | juce::FileBrowserComponent::warnAboutOverwriting,
                              [safeThis = SafePointer<ExportDialog> (this)] (const juce::FileChooser& fc)
    {
        if (safeThis != nullptr && fc.getResult() != juce::File())
            safeThis->startExport (fc.getResult());
    });
}

void ExportDialog::startExport (const juce::File& destination)
{
    engine.getPreferences().set (settings::exportFolder, destination.getParentDirectory().getFullPathName());

    ExportSettings settings;
    settings.format = getFormat();
    settings.quality = qualityBox.getSelectedId();
    settings.normalize = normalizeToggle.getToggleState();
    settings.cycleOnly = rangeBox.getSelectedId() == 2;
    settings.stems = stemsToggle.getToggleState();
    settings.destination = destination;

    auto started = exporter.start (settings, [safeThis = SafePointer<ExportDialog> (this)] (const ExportResult& r)
    {
        if (safeThis != nullptr)
            safeThis->exportFinished (r);
    });

    if (started.failed())
    {
        message = started.getErrorMessage();
        resized();
        repaint();
        return;
    }

    message = settings.stems ? "Exporting the stems to \"" + destination.getFileName() + "\"..."
                             : "Exporting \"" + destination.getFileNameWithoutExtension() + "\"...";
    showStage (Stage::exporting);
    startTimerHz (20);
}

void ExportDialog::exportFinished (const ExportResult& r)
{
    stopTimer();
    result = r;

    if (r.cancelled)
        return close();

    if (r.ok)
        message = r.files.size() == 1 ? "Exported \"" + r.files[0].getFileName() + "\"."
                                      : "Exported " + juce::String (r.files.size()) + " stems to \""
                                            + r.files[0].getParentDirectory().getFileName() + "\".";
    else
        message = r.error;

    showStage (Stage::done);
}

void ExportDialog::timerCallback()
{
    progress = (double) exporter.getProgress();
}

void ExportDialog::showStage (Stage s)
{
    stage = s;

    for (auto* c : std::initializer_list<juce::Component*> { &rangeBox, &formatBox, &qualityBox, &normalizeToggle, &stemsToggle })
        c->setVisible (stage == Stage::options);

    progressBar.setVisible (stage == Stage::exporting);
    exportButton.setVisible (stage != Stage::exporting);
    exportButton.setText (stage == Stage::done ? "Done" : "Export...");   // (IconButton draws its own text, not getButtonText())
    cancelButton.setVisible (stage != Stage::done);
    revealButton.setVisible (stage == Stage::done && result.ok);

    if (stage == Stage::done)
        exportButton.grabKeyboardFocus();
    else
        grabKeyboardFocus();   // so Escape cancels while exporting

    resized();
    repaint();
}

void ExportDialog::close()
{
    // Remove ourselves from the parent and delete on the next message-loop
    // turn: we may be inside one of our own button callbacks right now.
    setVisible (false);
    juce::Component::unfocusAllComponents();

    juce::MessageManager::callAsync ([safeThis = SafePointer<ExportDialog> (this)]
    {
        if (safeThis != nullptr)
            delete safeThis.getComponent();
    });
}

bool ExportDialog::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey)
    {
        cancelButton.isVisible() ? cancelButton.triggerClick() : close();
        return true;
    }

    if (key == juce::KeyPress::returnKey && stage != Stage::exporting)
    {
        exportButton.triggerClick();
        return true;
    }

    return true;   // swallow everything else: the app's shortcuts mustn't reach the session meanwhile
}

//==============================================================================
int ExportDialog::getMessageHeight() const
{
    // Two lines if it doesn't fit on one (a long list of stems).
    const auto width = juce::GlyphArrangement::getStringWidth (fonts::ui (textSize::md), message);
    return width > (float) (panelWidth - 2 * padX) ? 2 * lineHeight : lineHeight;
}

void ExportDialog::paint (juce::Graphics& g)
{
    // Dim the app behind the dialog.
    g.fillAll (colour::scrim);

    shadow::dialog.drawForRectangle (g, panel);
    g.setColour (colour::toolbarBg);
    g.fillRoundedRectangle (panel.toFloat(), size::radiusPill);
    g.setColour (colour::border);
    g.drawRoundedRectangle (panel.toFloat().reduced (0.5f), size::radiusPill, 1.0f);

    auto r = panel.reduced (padX, padY);
    g.setColour (colour::text);
    g.setFont (fonts::uiBold (textSize::title));
    const auto title = stage == Stage::exporting ? "Exporting..."
                     : stage == Stage::done      ? (result.ok ? "Export finished" : "Export failed")
                     : stemsToggle.getToggleState() ? "Export Stems" : "Export Song";
    g.drawText (title, r.removeFromTop (titleHeight), juce::Justification::centredLeft);

    r.removeFromTop (6);
    g.setColour (colour::textSecondary);
    g.setFont (fonts::ui (textSize::md));
    g.drawFittedText (message, r.removeFromTop (getMessageHeight()), juce::Justification::topLeft, 2, 1.0f);

    if (stage == Stage::options)
    {
        auto labels = rows;

        for (auto* label : { "Export", "Format", "Quality" })
        {
            g.drawText (label, labels.removeFromTop (rowHeight).withWidth (labelWidth), juce::Justification::centredLeft);
            labels.removeFromTop (rowGap);
        }
    }
}

void ExportDialog::resized()
{
    // Sized to fit: the title, the message (one or two lines), then the
    // options (or the progress bar) and the buttons.
    const int content = stage == Stage::options ? 3 * (rowHeight + rowGap) + 2 * 26
                      : stage == Stage::exporting ? 20 : 0;
    const int height = 2 * padY + titleHeight + 6 + getMessageHeight() + 16 + content + 20 + 36;
    panel = getLocalBounds().withSizeKeepingCentre (panelWidth, height);

    auto r = panel.reduced (padX, padY);
    r.removeFromTop (titleHeight + 6 + getMessageHeight() + 16);

    rows = r;
    auto row = [&]
    {
        auto line = r.removeFromTop (rowHeight);
        r.removeFromTop (rowGap);
        return line.withTrimmedLeft (labelWidth);
    };

    rangeBox.setBounds (row());
    formatBox.setBounds (row().withWidth (240));
    qualityBox.setBounds (row().withWidth (240));
    normalizeToggle.setBounds (r.removeFromTop (26));
    stemsToggle.setBounds (r.removeFromTop (26));

    progressBar.setBounds (rows.withHeight (20));

    auto buttons = panel.reduced (padX, padY).removeFromBottom (36);

    // While exporting, Cancel is the only button: centered under the progress bar.
    if (stage == Stage::exporting)
    {
        cancelButton.setBounds (buttons.withSizeKeepingCentre (100, buttons.getHeight()));
        return;
    }

    exportButton.setBounds (buttons.removeFromRight (110));
    buttons.removeFromRight (10);
    cancelButton.setBounds (buttons.removeFromRight (100));
    revealButton.setBounds (buttons.removeFromLeft (150));
}

} // namespace ikaros
