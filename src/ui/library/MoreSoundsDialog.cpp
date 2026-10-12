#include "ui/library/MoreSoundsDialog.h"
#include "engine/PackStore.h"
#include "ui/theme/Fonts.h"
#include "ui/theme/Tokens.h"

namespace ikaros
{

using namespace tokens;

namespace
{
    juce::String sizeText (juce::int64 bytes)
    {
        return bytes > 0 ? juce::File::descriptionOfSizeInBytes (bytes) : juce::String();
    }
}

//==============================================================================
// One pack: what it is on the left, what can be done with it on the right.
class MoreSoundsDialog::PackRow  : public juce::Component
{
public:
    std::function<void()> onDownload, onCancel, onRemove;

    PackRow()
    {
        mainButton.setColours (IconButton::accentColours());
        mainButton.setFont (fonts::uiSemiBold (13.0f));
        mainButton.onClick = [this]
        {
            if (pack.state == StorePack::State::downloading)
            {
                if (onCancel) onCancel();
            }
            else if (onDownload)
            {
                onDownload();
            }
        };
        addChildComponent (mainButton);

        greyColours.border = colour::border;
        removeButton.setColours (greyColours);
        removeButton.setFont (fonts::uiSemiBold (13.0f));
        removeButton.onClick = [this] { if (onRemove) onRemove(); };
        addChildComponent (removeButton);
    }

    void setPack (const StorePack& p)
    {
        pack = p;
        using State = StorePack::State;
        const bool downloading = p.state == State::downloading;

        // The main button: Download, Update, or Cancel while downloading.
        mainButton.setVisible (p.state == State::notInstalled || p.state == State::updateAvailable || downloading);
        const juce::String action (downloading ? "Cancel" : p.state == State::updateAvailable ? "Update" : "Download");
        mainButton.setText (action);
        mainButton.setColours (downloading ? greyColours : IconButton::accentColours());
        mainButton.setTitle (action + " " + p.name);

        // Remove: installed packs, but never the ones that come with the app.
        removeButton.setVisible ((p.state == State::installed || p.state == State::updateAvailable) && ! p.bundled);
        removeButton.setTitle ("Remove " + p.name);

        setTitle (p.name);
        setDescription (p.description);
        resized();
        repaint();
    }

    const StorePack& getPack() const noexcept    { return pack; }

    void paint (juce::Graphics& g) override
    {
        using State = StorePack::State;
        auto r = getLocalBounds().reduced (0, 4);
        g.setColour (colour::cardBg);
        g.fillRoundedRectangle (r.toFloat(), 10.0f);

        auto text = contentArea().withTrimmedRight (actionsWidth + 16);

        // Name, then category and size.
        g.setColour (colour::text);
        g.setFont (fonts::uiBold (15.0f));
        g.drawText (pack.name, text.removeFromTop (20), juce::Justification::centredLeft, true);

        juce::StringArray facts;
        facts.add (pack.category);

        if (pack.state == State::notInstalled)
            facts.add (sizeText (pack.downloadSize) + (pack.downloadSize > 0 ? " download" : ""));
        else
            facts.add (sizeText (pack.installedSize));

        if (pack.license.isNotEmpty())
            facts.add (pack.license);

        facts.removeEmptyStrings();
        g.setColour (colour::textSecondary);
        g.setFont (fonts::ui (12.0f));
        g.drawText (facts.joinIntoString (juce::String::fromUTF8 (" \xc2\xb7 ")), text.removeFromTop (18),
                    juce::Justification::centredLeft, true);

        // The description, or why the last download failed.
        text.removeFromTop (4);
        g.setColour (pack.error.isNotEmpty() ? colour::record : colour::textSecondary);
        g.setFont (fonts::ui (13.0f));
        g.drawFittedText (pack.error.isNotEmpty() ? pack.error : pack.description, text,
                          juce::Justification::topLeft, 2, 1.0f);

        // Top right: the pack's state. Its right edge lines up with the buttons'.
        // (Trimmed by a pixel: a button's last column is its faint border, so
        // its visible edge is one pixel in from its bounds.)
        auto status = contentArea().removeFromRight (actionsWidth).removeFromTop (statusHeight).withTrimmedRight (1);

        if (pack.state == State::downloading || pack.state == State::installing)
        {
            g.setColour (colour::textSecondary);
            g.setFont (fonts::ui (12.0f));
            g.drawText (pack.state == State::installing ? juce::String ("Installing...")
                                                        : "Downloading " + juce::String (juce::roundToInt (pack.progress * 100.0f)) + "%",
                        status.removeFromTop (16), juce::Justification::centredRight);

            auto bar = status.withSizeKeepingCentre (status.getWidth(), 6).toFloat();
            g.setColour (colour::controlFill);
            g.fillRoundedRectangle (bar, 3.0f);
            g.setColour (colour::accent);
            g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * (pack.state == State::installing ? 1.0f : pack.progress)), 3.0f);
        }
        else if (pack.state == State::installed && pack.bundled)
        {
            g.setColour (colour::textSecondary);
            g.setFont (fonts::ui (13.0f));
            g.drawText ("Comes with the app", status, juce::Justification::centredRight);
        }
        else if (pack.state == State::installed)
        {
            // A green tick in a circle, then "Installed".
            const juce::String word ("Installed");
            auto font = fonts::uiSemiBold (13.0f);
            const float textWidth = juce::GlyphArrangement::getStringWidth (font, word);
            const float tickSize = 16.0f, gap = 6.0f;

            g.setColour (colour::effectOn);
            g.setFont (font);
            g.drawText (word, status, juce::Justification::centredRight);

            auto circle = juce::Rectangle<float> (tickSize, tickSize)
                              .withCentre ({ (float) status.getRight() - textWidth - gap - tickSize * 0.5f,
                                             (float) status.getCentreY() });
            g.fillEllipse (circle);

            juce::Path tick;
            tick.startNewSubPath (circle.getX() + tickSize * 0.28f, circle.getCentreY());
            tick.lineTo (circle.getX() + tickSize * 0.44f, circle.getY() + tickSize * 0.66f);
            tick.lineTo (circle.getX() + tickSize * 0.72f, circle.getY() + tickSize * 0.34f);
            g.setColour (colour::textOnAccent);
            g.strokePath (tick, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
    }

    void resized() override
    {
        // Every button sits at the bottom right of the row, the main one
        // (Download, Update or Cancel) rightmost, Remove to its left.
        auto buttons = contentArea().removeFromBottom (buttonHeight);

        if (mainButton.isVisible())
        {
            mainButton.setBounds (buttons.removeFromRight (buttonWidth));
            buttons.removeFromRight (8);
        }

        removeButton.setBounds (buttons.removeFromRight (buttonWidth));
    }

private:
    StorePack pack;
    IconButton mainButton { "Download", std::nullopt, "Download" };
    IconButton removeButton { "Remove", std::nullopt, "Remove" };
    IconButton::Colours greyColours = IconButton::raisedColours();
    static constexpr int actionsWidth = 200, buttonWidth = 96, buttonHeight = 28, statusHeight = 22;

    // Inside the row's card, where the text, status and buttons go.
    juce::Rectangle<int> contentArea() const    { return getLocalBounds().reduced (0, 4).reduced (16, 12); }
};

//==============================================================================
void MoreSoundsDialog::show (juce::Component& parent, AudioEngine& engine, const juce::String& downloadPackId)
{
    // Owned by its parent from here on; it deletes itself in close().
    auto* dialog = new MoreSoundsDialog (engine, downloadPackId);
    parent.addAndMakeVisible (dialog);
    dialog->setBounds (parent.getLocalBounds());
    dialog->grabKeyboardFocus();
}

MoreSoundsDialog::MoreSoundsDialog (AudioEngine& e, const juce::String& downloadPackId)
    : engine (e), pendingDownload (downloadPackId)
{
    setTitle ("More Sounds");
    setWantsKeyboardFocus (true);
    setAlwaysOnTop (true);

    listViewport.setViewedComponent (&listHolder, false);   // false: we own listHolder, the viewport doesn't
    listViewport.setScrollBarsShown (true, false);
    addAndMakeVisible (listViewport);

    auto colours = IconButton::raisedColours();
    colours.border = colour::border;
    retryButton.setColours (colours);
    retryButton.onClick = [this] { engine.getPackStore().refreshIndex(); };
    addChildComponent (retryButton);

    doneButton.setColours (IconButton::accentColours());
    doneButton.onClick = [this] { close(); };
    addAndMakeVisible (doneButton);

    auto& store = engine.getPackStore();
    store.addChangeListener (this);

    // Fetch the list each time, so it's never out of date.
    if (store.getIndexStatus() != PackStore::IndexStatus::loading)
        store.refreshIndex();

    refresh();
}

MoreSoundsDialog::~MoreSoundsDialog()
{
    engine.getPackStore().removeChangeListener (this);
}

void MoreSoundsDialog::changeListenerCallback (juce::ChangeBroadcaster*)
{
    refresh();
}

void MoreSoundsDialog::refresh()
{
    auto& store = engine.getPackStore();
    const auto status = store.getIndexStatus();

    // The Sound tab's Download button: start it as soon as the index lists it.
    if (pendingDownload.isNotEmpty() && status == PackStore::IndexStatus::loaded)
    {
        if (auto p = store.findPack (pendingDownload); p.has_value() && p->availableVersion.isNotEmpty())
            store.install (pendingDownload);
        else
            requestedPackUnavailable = true;

        pendingDownload.clear();
    }

    statusText = status == PackStore::IndexStatus::loading     ? "Looking for sound packs..."
               : status == PackStore::IndexStatus::unavailable ? "Couldn't reach the sound library. Check your internet "
                                                                 "connection and try again. Your installed packs are below."
                                                               : requestedPackUnavailable
                                                                    ? "The sound pack that track needs isn't available to download. "
                                                                      "Ask whoever shared the session with you for it."
                                                                    : juce::String();
    retryButton.setVisible (status == PackStore::IndexStatus::unavailable);

    auto packs = store.getPacks();

    // Rows are kept (not rebuilt) while the same packs are listed, so a
    // button being clicked isn't deleted under the mouse.
    bool samePacks = (int) packs.size() == rows.size();

    for (int i = 0; samePacks && i < rows.size(); ++i)
        samePacks = rows[i]->getPack().id == packs[(size_t) i].id;

    if (! samePacks)
    {
        rows.clear();

        for (auto& p : packs)
        {
            auto* row = rows.add (new PackRow());
            const auto id = p.id;
            row->onDownload = [this, id] { engine.getPackStore().install (id); };
            row->onCancel = [this, id] { engine.getPackStore().cancel (id); };
            row->onRemove = [this, row] { confirmRemove (row->getPack()); };
            listHolder.addAndMakeVisible (row);
        }
    }

    for (int i = 0; i < rows.size(); ++i)
        rows[i]->setPack (packs[(size_t) i]);

    resized();
    repaint();
}

void MoreSoundsDialog::confirmRemove (const StorePack& pack)
{
    auto options = juce::MessageBoxOptions()
                       .withIconType (juce::MessageBoxIconType::QuestionIcon)
                       .withTitle ("Remove \"" + pack.name + "\"?")
                       .withMessage ("Its sounds and loops are deleted from this computer. Tracks that use them stay in "
                                     "your sessions and play again once you download the pack again.")
                       .withButton ("Remove")
                       .withButton ("Cancel")
                       .withAssociatedComponent (this);

    // The answer arrives later (no modal loops), so check we still exist.
    juce::AlertWindow::showAsync (options, [safeThis = SafePointer<MoreSoundsDialog> (this), id = pack.id] (int result)
    {
        if (safeThis == nullptr || result != 1)
            return;

        if (! safeThis->engine.getPackStore().remove (id))
            juce::AlertWindow::showAsync (juce::MessageBoxOptions()
                                              .withIconType (juce::MessageBoxIconType::WarningIcon)
                                              .withTitle ("Couldn't remove the pack")
                                              .withMessage ("Wait for downloads to finish, or check that its folder isn't open elsewhere.")
                                              .withButton ("OK")
                                              .withAssociatedComponent (safeThis.getComponent()),
                                          nullptr);
    });
}

void MoreSoundsDialog::close()
{
    // Downloads carry on in the background after the window closes.
    setVisible (false);
    juce::Component::unfocusAllComponents();

    juce::MessageManager::callAsync ([safeThis = SafePointer<MoreSoundsDialog> (this)]
    {
        if (safeThis != nullptr)
            delete safeThis.getComponent();
    });
}

bool MoreSoundsDialog::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey || key == juce::KeyPress::returnKey)
    {
        close();
        return true;
    }

    return false;
}

void MoreSoundsDialog::mouseDown (const juce::MouseEvent& e)
{
    if (! panel.contains (e.getPosition()))
        close();
}

void MoreSoundsDialog::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black.withAlpha (0.55f));

    juce::DropShadow (juce::Colours::black.withAlpha (0.6f), 30, { 0, 8 }).drawForRectangle (g, panel);
    g.setColour (colour::toolbarBg);
    g.fillRoundedRectangle (panel.toFloat(), 14.0f);
    g.setColour (colour::border);
    g.drawRoundedRectangle (panel.toFloat().reduced (0.5f), 14.0f, 1.0f);

    auto r = panel.reduced (28, 24);
    g.setColour (colour::text);
    g.setFont (fonts::uiBold (18.0f));
    g.drawText ("More Sounds", r.removeFromTop (26), juce::Justification::centredLeft);

    g.setColour (colour::textSecondary);
    g.setFont (fonts::ui (13.0f));
    g.drawText ("Download instruments and loops. They appear in the library as soon as they're installed.",
                r.removeFromTop (22), juce::Justification::centredLeft, true);

    if (statusText.isNotEmpty())
    {
        g.setColour (colour::text);
        g.drawFittedText (statusText, statusArea, juce::Justification::centredLeft, 2, 1.0f);
    }
}

void MoreSoundsDialog::resized()
{
    panel = getLocalBounds().withSizeKeepingCentre (juce::jmin (panelWidth, getWidth() - 32),
                                                    juce::jmin (panelHeight, getHeight() - 32));
    auto r = panel.reduced (28, 24);
    r.removeFromTop (26 + 22 + 12);

    auto bottom = r.removeFromBottom (36);
    doneButton.setBounds (bottom.removeFromRight (96));
    r.removeFromBottom (12);

    if (statusText.isNotEmpty())
    {
        statusArea = r.removeFromTop (40);

        if (retryButton.isVisible())
            retryButton.setBounds (statusArea.removeFromRight (100).withSizeKeepingCentre (100, 30));

        r.removeFromTop (8);
    }
    else
    {
        statusArea = {};
    }

    listViewport.setBounds (r);
    const int width = r.getWidth() - (rows.size() * rowHeight > r.getHeight() ? listViewport.getScrollBarThickness() + 4 : 0);
    listHolder.setSize (width, rows.size() * rowHeight);

    for (int i = 0; i < rows.size(); ++i)
        rows[i]->setBounds (0, i * rowHeight, width, rowHeight);
}

} // namespace ikaros
