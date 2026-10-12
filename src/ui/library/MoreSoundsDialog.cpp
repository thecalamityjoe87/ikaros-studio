#include "ui/library/MoreSoundsDialog.h"
#include "engine/PackStore.h"
#include "ui/theme/Fonts.h"
#include "ui/theme/Tokens.h"

namespace ikaros
{

using namespace tokens;

namespace
{
    // Remembered once the user has answered the first-run offer.
    const juce::String starterOfferedSetting ("starterSoundsOffered");

    juce::String sizeText (juce::int64 bytes)
    {
        return bytes > 0 ? juce::File::descriptionOfSizeInBytes (bytes) : juce::String();
    }

    juce::String joinFacts (juce::StringArray facts)
    {
        facts.removeEmptyStrings();
        return facts.joinIntoString (juce::String::fromUTF8 (" \xc2\xb7 "));   // " · "
    }

    // What one row shows. Packs and collections both become one of these,
    // so they're laid out by the same code.
    struct RowView
    {
        enum class Status { none, installed, text, progress };

        juce::String key;              // "pack:<id>" or "collection:<id>"
        juce::String title, facts, description, error;
        Status status = Status::none;
        juce::String statusText;       // for text and progress
        float progress = 0.0f;
        juce::String mainAction;       // the main button's text, or empty for none
        bool mainIsCancel = false;     // a grey Cancel rather than an amber action
        bool canRemove = false;
        bool highlighted = false;      // collections stand out from the packs
    };

    RowView viewOf (const StorePack& p)
    {
        using State = StorePack::State;
        RowView v;
        v.key = "pack:" + p.id;
        v.title = p.name;
        v.facts = joinFacts ({ p.category,
                               p.state == State::notInstalled ? sizeText (p.downloadSize) + (p.downloadSize > 0 ? " download" : "")
                                                              : sizeText (p.installedSize),
                               p.license });
        v.description = p.description;
        v.error = p.error;

        if (p.state == State::downloading || p.state == State::installing)
        {
            v.status = RowView::Status::progress;
            v.progress = p.state == State::installing ? 1.0f : p.progress;
            v.statusText = p.state == State::installing ? juce::String ("Installing...")
                                                        : "Downloading " + juce::String (juce::roundToInt (p.progress * 100.0f)) + "%";
        }
        else if (p.state == State::installed && p.bundled)
        {
            v.status = RowView::Status::text;
            v.statusText = "Comes with the app";
        }
        else if (p.state == State::installed)
        {
            v.status = RowView::Status::installed;
        }

        if (p.state == State::downloading)              { v.mainAction = "Cancel"; v.mainIsCancel = true; }
        else if (p.state == State::notInstalled)        v.mainAction = "Download";
        else if (p.state == State::updateAvailable)     v.mainAction = "Update";

        v.canRemove = (p.state == State::installed || p.state == State::updateAvailable) && ! p.bundled;
        return v;
    }

    RowView viewOf (const StoreCollection& c, const std::vector<StorePack>& packs)
    {
        RowView v;
        v.key = "collection:" + c.id;
        v.title = c.name;
        v.highlighted = true;

        const auto count = (int) c.packIds.size();
        v.facts = joinFacts ({ juce::String (count) + (count == 1 ? " pack" : " packs"),
                               c.packsToInstall > 0 ? sizeText (c.downloadSize) + " download" : juce::String() });

        // A short list of what's in it, by name.
        juce::StringArray names;

        for (auto& id : c.packIds)
            for (auto& p : packs)
                if (p.id == id)
                    names.add (p.name);

        v.description = c.description.isNotEmpty() ? c.description : names.joinIntoString (", ");

        if (c.packsDownloading > 0)
        {
            // Overall progress: installed packs count as done, downloading ones as far as they've got.
            float done = 0.0f;

            for (auto& id : c.packIds)
                for (auto& p : packs)
                    if (p.id == id)
                        done += p.state == StorePack::State::installed   ? 1.0f
                              : p.state == StorePack::State::downloading ? p.progress
                              : p.state == StorePack::State::installing  ? 1.0f : 0.0f;

            v.status = RowView::Status::progress;
            v.progress = count > 0 ? done / (float) count : 0.0f;
            v.statusText = "Downloading " + juce::String (count - c.packsToInstall - c.packsDownloading + 1)
                           + " of " + juce::String (count);
        }
        else if (c.allInstalled)
        {
            v.status = RowView::Status::installed;
        }

        if (c.packsToInstall > 0)
            v.mainAction = "Download All";

        return v;
    }
}

//==============================================================================
// One row: what it is on the left; its state at the top right; its buttons
// at the bottom right, right-aligned with the state above them.
class MoreSoundsDialog::Row  : public juce::Component
{
public:
    std::function<void()> onMain, onRemove;

    Row()
    {
        mainButton.setFont (fonts::uiSemiBold (13.0f));
        mainButton.onClick = [this] { if (onMain) onMain(); };
        addChildComponent (mainButton);

        greyColours.border = colour::border;
        removeButton.setColours (greyColours);
        removeButton.setFont (fonts::uiSemiBold (13.0f));
        removeButton.onClick = [this] { if (onRemove) onRemove(); };
        addChildComponent (removeButton);
    }

    const juce::String& getKey() const noexcept    { return view.key; }

    void setView (const RowView& v)
    {
        view = v;

        mainButton.setVisible (v.mainAction.isNotEmpty());
        mainButton.setText (v.mainAction);
        mainButton.setColours (v.mainIsCancel ? greyColours : IconButton::accentColours());
        mainButton.setTitle (v.mainAction + " " + v.title);

        removeButton.setVisible (v.canRemove);
        removeButton.setTitle ("Remove " + v.title);

        setTitle (v.title);
        setDescription (v.description);
        resized();
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto card = getLocalBounds().reduced (0, 4).toFloat();
        g.setColour (view.highlighted ? colour::selectedRow : colour::cardBg);
        g.fillRoundedRectangle (card, 10.0f);

        if (view.highlighted)
        {
            g.setColour (colour::accent.withAlpha (0.6f));
            g.drawRoundedRectangle (card.reduced (0.5f), 10.0f, 1.0f);
        }

        // Left: name, facts, then the description (or why a download failed).
        auto text = contentArea().withTrimmedRight (actionsWidth + 16);
        g.setColour (colour::text);
        g.setFont (fonts::uiBold (15.0f));
        g.drawText (view.title, text.removeFromTop (20), juce::Justification::centredLeft, true);

        g.setColour (colour::textSecondary);
        g.setFont (fonts::ui (12.0f));
        g.drawText (view.facts, text.removeFromTop (18), juce::Justification::centredLeft, true);

        text.removeFromTop (4);
        g.setColour (view.error.isNotEmpty() ? colour::record : colour::textSecondary);
        g.setFont (fonts::ui (13.0f));
        g.drawFittedText (view.error.isNotEmpty() ? view.error : view.description, text,
                          juce::Justification::topLeft, 2, 1.0f);

        // Top right: the state. Its right edge lines up with the buttons'.
        // (Trimmed by a pixel: a button's last column is its faint border, so
        // its visible edge is one pixel in from its bounds.)
        auto status = contentArea().removeFromRight (actionsWidth).removeFromTop (statusHeight).withTrimmedRight (1);

        switch (view.status)
        {
            case RowView::Status::progress:
            {
                g.setColour (colour::textSecondary);
                g.setFont (fonts::ui (12.0f));
                g.drawText (view.statusText, status.removeFromTop (16), juce::Justification::centredRight);

                auto bar = status.withSizeKeepingCentre (status.getWidth(), 6).toFloat();
                g.setColour (colour::controlFill);
                g.fillRoundedRectangle (bar, 3.0f);
                g.setColour (colour::accent);
                g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * juce::jlimit (0.0f, 1.0f, view.progress)), 3.0f);
                break;
            }

            case RowView::Status::text:
                g.setColour (colour::textSecondary);
                g.setFont (fonts::ui (13.0f));
                g.drawText (view.statusText, status, juce::Justification::centredRight);
                break;

            case RowView::Status::installed:
                drawInstalled (g, status);
                break;

            case RowView::Status::none:
                break;
        }
    }

    void resized() override
    {
        // The main button (Download, Update, Cancel) rightmost, Remove to its left.
        auto buttons = contentArea().removeFromBottom (buttonHeight);
        const int mainWidth = juce::jmax (buttonWidth, juce::roundToInt (juce::GlyphArrangement::getStringWidth (fonts::uiSemiBold (13.0f), view.mainAction)) + 32);

        if (mainButton.isVisible())
        {
            mainButton.setBounds (buttons.removeFromRight (mainWidth));
            buttons.removeFromRight (8);
        }

        removeButton.setBounds (buttons.removeFromRight (buttonWidth));
    }

private:
    RowView view;
    IconButton mainButton { "Download", std::nullopt, "Download" };
    IconButton removeButton { "Remove", std::nullopt, "Remove" };
    IconButton::Colours greyColours = IconButton::raisedColours();
    static constexpr int actionsWidth = 200, buttonWidth = 96, buttonHeight = 28, statusHeight = 22;

    // Inside the row's card, where the text, state and buttons go.
    juce::Rectangle<int> contentArea() const    { return getLocalBounds().reduced (0, 4).reduced (16, 12); }

    // A green tick in a circle, then "Installed", right-aligned in the area.
    static void drawInstalled (juce::Graphics& g, juce::Rectangle<int> area)
    {
        const juce::String word ("Installed");
        auto font = fonts::uiSemiBold (13.0f);
        const float textWidth = juce::GlyphArrangement::getStringWidth (font, word);
        const float tickSize = 16.0f, gap = 6.0f;

        g.setColour (colour::effectOn);
        g.setFont (font);
        g.drawText (word, area, juce::Justification::centredRight);

        auto circle = juce::Rectangle<float> (tickSize, tickSize)
                          .withCentre ({ (float) area.getRight() - textWidth - gap - tickSize * 0.5f, (float) area.getCentreY() });
        g.fillEllipse (circle);

        juce::Path tick;
        tick.startNewSubPath (circle.getX() + tickSize * 0.28f, circle.getCentreY());
        tick.lineTo (circle.getX() + tickSize * 0.44f, circle.getY() + tickSize * 0.66f);
        tick.lineTo (circle.getX() + tickSize * 0.72f, circle.getY() + tickSize * 0.34f);
        g.setColour (colour::textOnAccent);
        g.strokePath (tick, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
};

//==============================================================================
void MoreSoundsDialog::show (juce::Component& parent, AudioEngine& engine, const juce::String& downloadPackId)
{
    // Owned by its parent from here on; it deletes itself in close().
    auto* dialog = new MoreSoundsDialog (engine, false, downloadPackId, {});
    parent.addAndMakeVisible (dialog);
    dialog->setBounds (parent.getLocalBounds());
    dialog->grabKeyboardFocus();
}

void MoreSoundsDialog::showWelcome (juce::Component& parent, AudioEngine& engine, std::function<void()> onClosed)
{
    auto* dialog = new MoreSoundsDialog (engine, true, {}, std::move (onClosed));
    parent.addAndMakeVisible (dialog);
    dialog->setBounds (parent.getLocalBounds());
    dialog->grabKeyboardFocus();
}

bool MoreSoundsDialog::shouldOfferStarterSounds (AudioEngine& engine)
{
    return ! (bool) engine.getAppSetting (starterOfferedSetting, false);
}

MoreSoundsDialog::MoreSoundsDialog (AudioEngine& e, bool isWelcome, const juce::String& downloadPackId,
                                    std::function<void()> closed)
    : engine (e), welcome (isWelcome), pendingDownload (downloadPackId), onClosed (std::move (closed))
{
    setTitle (welcome ? "Welcome" : "More Sounds");
    setWantsKeyboardFocus (true);
    setAlwaysOnTop (true);

    listViewport.setViewedComponent (&listHolder, false);   // false: we own listHolder, the viewport doesn't
    listViewport.setScrollBarsShown (true, false);
    addAndMakeVisible (listViewport);

    auto grey = IconButton::raisedColours();
    grey.border = colour::border;

    retryButton.setColours (grey);
    retryButton.onClick = [this] { engine.getPackStore().refreshIndex(); };
    addChildComponent (retryButton);

    // Welcome: "Not Now" says no for now (asked again next time only if the
    // offer never got shown, e.g. offline).
    notNowButton.setColours (grey);
    notNowButton.onClick = [this]
    {
        if (engine.getPackStore().getIndexStatus() == PackStore::IndexStatus::loaded)
            markStarterOffered();

        close();
    };
    addChildComponent (notNowButton);

    doneButton.setColours (IconButton::accentColours());
    doneButton.onClick = [this]
    {
        // Welcome, before choosing: the big button downloads the starter sounds.
        if (welcome && ! starterRequested)
        {
            if (auto starter = engine.getPackStore().findCollection (PackStore::starterCollectionId))
            {
                engine.getPackStore().installCollection (starter->id);
                starterRequested = true;
                markStarterOffered();
                refresh();
                return;
            }
        }

        close();
    };
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

void MoreSoundsDialog::markStarterOffered()
{
    engine.setAppSetting (starterOfferedSetting, true);
}

void MoreSoundsDialog::refresh()
{
    auto& store = engine.getPackStore();
    const auto status = store.getIndexStatus();
    const auto packs = store.getPacks();
    const auto collections = store.getCollections();
    const auto starter = store.findCollection (PackStore::starterCollectionId);

    // The Sound tab's Download button: start it as soon as the index lists it.
    if (pendingDownload.isNotEmpty() && status == PackStore::IndexStatus::loaded)
    {
        if (auto p = store.findPack (pendingDownload); p.has_value() && p->availableVersion.isNotEmpty())
            store.install (pendingDownload);
        else
            requestedPackUnavailable = true;

        pendingDownload.clear();
    }

    // Welcome, with nothing to offer (no starter set, or it's all installed):
    // don't ask, just carry on.
    if (welcome && ! starterRequested && status == PackStore::IndexStatus::loaded
         && (! starter.has_value() || starter->allInstalled))
    {
        markStarterOffered();
        close();
        return;
    }

    const bool offline = status == PackStore::IndexStatus::unavailable;

    statusText = status == PackStore::IndexStatus::loading ? "Looking for sound packs..."
               : offline && welcome ? "Couldn't reach the sound library. Check your internet connection and try again, "
                                      "or start with the sounds that come with the app. We'll offer these again next time."
               : offline ? "Couldn't reach the sound library. Check your internet connection and try again. "
                           "Your installed packs are below."
               : requestedPackUnavailable ? "The sound pack that track needs isn't available to download. "
                                            "Ask whoever shared the session with you for it."
               : juce::String();
    retryButton.setVisible (offline);

    // What's listed: collections first, then packs. The welcome shows just
    // the starter set and what's in it.
    std::vector<RowView> views;

    for (auto& c : collections)
    {
        if (welcome && c.id != PackStore::starterCollectionId)
            continue;

        auto v = viewOf (c, packs);

        if (welcome)
            v.mainAction.clear();   // the big button at the bottom does that

        views.push_back (v);
    }

    for (auto& p : packs)
    {
        if (welcome && ! (starter.has_value() && std::find (starter->packIds.begin(), starter->packIds.end(), p.id) != starter->packIds.end()))
            continue;

        auto v = viewOf (p);
        v.canRemove = v.canRemove && ! welcome;
        views.push_back (v);
    }

    // Rows are kept (not rebuilt) while the same things are listed, so a
    // button being clicked isn't deleted under the mouse.
    bool sameRows = (int) views.size() == rows.size();

    for (int i = 0; sameRows && i < rows.size(); ++i)
        sameRows = rows[i]->getKey() == views[(size_t) i].key;

    if (! sameRows)
    {
        rows.clear();

        for (auto& v : views)
        {
            auto* row = rows.add (new Row());
            listHolder.addAndMakeVisible (row);
            const auto id = v.key.fromFirstOccurrenceOf (":", false, false);

            if (v.key.startsWith ("collection:"))
            {
                row->onMain = [this, id] { engine.getPackStore().installCollection (id); };
            }
            else
            {
                row->onMain = [this, id]
                {
                    auto& s = engine.getPackStore();
                    auto p = s.findPack (id);

                    if (p.has_value() && p->state == StorePack::State::downloading)
                        s.cancel (id);
                    else
                        s.install (id);
                };

                row->onRemove = [this, id, name = v.title] { confirmRemove (id, name); };
            }
        }
    }

    for (int i = 0; i < rows.size(); ++i)
        rows[i]->setView (views[(size_t) i]);

    // The buttons at the bottom.
    if (welcome && ! starterRequested && starter.has_value())
        doneText = "Download Starter Sounds" + (starter->downloadSize > 0 ? " (" + sizeText (starter->downloadSize) + ")"
                                                                          : juce::String());
    else
        doneText = "Done";

    doneButton.setText (doneText);
    doneButton.setTitle (doneText);

    notNowButton.setVisible (welcome && ! starterRequested);

    resized();
    repaint();
}

void MoreSoundsDialog::confirmRemove (const juce::String& packId, const juce::String& name)
{
    auto options = juce::MessageBoxOptions()
                       .withIconType (juce::MessageBoxIconType::QuestionIcon)
                       .withTitle ("Remove \"" + name + "\"?")
                       .withMessage ("Its sounds and loops are deleted from this computer. Tracks that use them stay in "
                                     "your sessions and play again once you download the pack again.")
                       .withButton ("Remove")
                       .withButton ("Cancel")
                       .withAssociatedComponent (this);

    // The answer arrives later (no modal loops), so check we still exist.
    juce::AlertWindow::showAsync (options, [safeThis = SafePointer<MoreSoundsDialog> (this), packId] (int result)
    {
        if (safeThis == nullptr || result != 1)
            return;

        if (! safeThis->engine.getPackStore().remove (packId))
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
    if (! isVisible())
        return;   // already closing

    // Downloads carry on in the background after the window closes.
    setVisible (false);
    juce::Component::unfocusAllComponents();

    juce::MessageManager::callAsync ([safeThis = SafePointer<MoreSoundsDialog> (this)]
    {
        if (safeThis == nullptr)
            return;

        auto closed = std::move (safeThis->onClosed);
        delete safeThis.getComponent();

        if (closed)
            closed();
    });
}

bool MoreSoundsDialog::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey)
    {
        if (notNowButton.isVisible())
            notNowButton.triggerClick();
        else
            close();

        return true;
    }

    if (key == juce::KeyPress::returnKey)
    {
        doneButton.triggerClick();
        return true;
    }

    return false;
}

void MoreSoundsDialog::mouseDown (const juce::MouseEvent& e)
{
    // The welcome asks a question, so a stray click outside doesn't dismiss it.
    if (! welcome && ! panel.contains (e.getPosition()))
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
    g.drawText (welcome ? "Welcome to Ikaros Studio" : "More Sounds", r.removeFromTop (26), juce::Justification::centredLeft);

    const juce::String subtitle = ! welcome ? "Download instruments and loops. They appear in the library as soon as they're installed."
                                : starterRequested ? "The starter sounds are downloading. You can start making music now: "
                                                     "each pack appears in the library as soon as it's installed."
                                                   : "Start with the starter sounds: real instruments and loops to play and build "
                                                     "songs with. You can download more any time from More Sounds.";
    g.setColour (colour::textSecondary);
    g.setFont (fonts::ui (13.0f));
    g.drawFittedText (subtitle, r.removeFromTop (welcome ? 40 : 22), juce::Justification::topLeft, 2, 1.0f);

    if (statusText.isNotEmpty())
    {
        g.setColour (colour::text);
        g.drawFittedText (statusText, statusArea, juce::Justification::centredLeft, 3, 1.0f);
    }
}

void MoreSoundsDialog::resized()
{
    panel = getLocalBounds().withSizeKeepingCentre (juce::jmin (panelWidth, getWidth() - 32),
                                                    juce::jmin (panelHeight, getHeight() - 32));
    auto r = panel.reduced (28, 24);
    r.removeFromTop (26 + (welcome ? 40 : 22) + 12);

    // Bottom right: the main button, with Not Now to its left.
    auto bottom = r.removeFromBottom (36);
    const int doneWidth = juce::jmax (96, juce::roundToInt (juce::GlyphArrangement::getStringWidth (fonts::uiSemiBold (14.0f), doneText)) + 40);
    doneButton.setBounds (bottom.removeFromRight (doneWidth));
    bottom.removeFromRight (8);
    notNowButton.setBounds (bottom.removeFromRight (96));
    r.removeFromBottom (12);

    if (statusText.isNotEmpty())
    {
        statusArea = r.removeFromTop (48);

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
