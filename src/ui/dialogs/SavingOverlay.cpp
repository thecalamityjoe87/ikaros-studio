#include "ui/dialogs/SavingOverlay.h"
#include "ui/theme/Fonts.h"
#include "ui/theme/Tokens.h"

namespace ikaros
{

using namespace tokens;

namespace
{
    constexpr juce::uint32 showAfterMs = 300;   // quicker saves never show the overlay
}

void SavingOverlay::run (juce::Component& parent, AudioEngine& engine, std::unique_ptr<AudioEngine::SaveJob> job,
                         std::function<void (bool)> onDone)
{
    // Owned by its parent from here on; it deletes itself when the save is done.
    auto* overlay = new SavingOverlay (engine, std::move (job), std::move (onDone));
    parent.addAndMakeVisible (overlay);
    overlay->setBounds (parent.getLocalBounds());
    overlay->grabKeyboardFocus();
}

SavingOverlay::SavingOverlay (AudioEngine& e, std::unique_ptr<AudioEngine::SaveJob> j, std::function<void (bool)> done)
    : engine (e), job (std::move (j)), onDone (std::move (done))
{
    setTitle ("Saving");
    setWantsKeyboardFocus (true);
    setInterceptsMouseClicks (true, true);   // nothing underneath can be clicked meanwhile
    addChildComponent (progressBar);

    // The packing runs on its own thread; this component only watches it.
    worker = std::thread ([this]
    {
        job->run();
        finished = true;
    });

    startTime = juce::Time::getMillisecondCounter();
    startTimerHz (20);
}

SavingOverlay::~SavingOverlay()
{
    // Normally the save has finished by now; if the window closes first,
    // wait for it (a half-written temporary file is cleaned up by pack()).
    if (worker.joinable())
        worker.join();
}

void SavingOverlay::timerCallback()
{
    progress = (double) job->progress.load();

    if (! finished)
    {
        // Only a slow save shows anything (until then the overlay is invisible,
        // but still blocks clicks and keys).
        if (! progressBar.isVisible() && juce::Time::getMillisecondCounter() - startTime > showAfterMs)
        {
            progressBar.setVisible (true);
            repaint();
        }

        return;
    }

    stopTimer();
    worker.join();
    const bool saved = engine.finishSave (*job);

    // Calling onDone may show a dialog or start another save, so tidy up
    // first: take what's needed, then delete this overlay.
    auto done = std::move (onDone);
    delete this;

    if (done)
        done (saved);
}

void SavingOverlay::paint (juce::Graphics& g)
{
    if (! progressBar.isVisible())
        return;

    g.fillAll (theme().scrimLight);
    g.setColour (theme().cardBg);
    g.fillRoundedRectangle (card.toFloat(), size::radiusLarge);
    g.setColour (theme().border);
    g.drawRoundedRectangle (card.toFloat().reduced (0.5f), size::radiusLarge, 1.0f);

    g.setColour (theme().text);
    g.setFont (fonts::uiBold (textSize::xl));
    g.drawText ("Saving \"" + job->bundle.getFileNameWithoutExtension() + "\"...",
                card.reduced (20, 16).removeFromTop (24), juce::Justification::centredLeft, true);
}

void SavingOverlay::resized()
{
    card = getLocalBounds().withSizeKeepingCentre (380, 104);
    progressBar.setBounds (card.reduced (20, 0).withTrimmedTop (56).withHeight (20));
}

} // namespace ikaros
