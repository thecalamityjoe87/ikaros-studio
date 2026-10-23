#include "ui/dialogs/StartupSplash.h"
#include "BinaryData.h"
#include "ui/theme/Fonts.h"

namespace ikaros
{

namespace
{
    constexpr int width = 440, height = 300, iconSize = 112;
    constexpr float cornerRadius = 14.0f;

    // The icon's background (resources/icons/*.svg) with light text, whatever the theme.
    const juce::Colour background (0xff1a1d23);
    const juce::Colour title (0xfff4f5f7), subtitle (0xff9aa0ab), accent (0xffffb547);

    // Long enough to read the name, short enough not to hold anyone up.
    constexpr juce::uint32 minimumVisibleMs = 1200;

    // If the window system never asks the splash to draw (no display, or
    // a very slow one), start the app anyway after this long.
    constexpr juce::uint32 paintTimeoutMs = 1000;
}

StartupSplash::StartupSplash (std::function<void()> shown)
    : onShown (std::move (shown))
{
    setTitle ("Ikaros Studio");
    icon = juce::ImageFileFormat::loadFrom (BinaryData::io_github_thecalamityjoe87_IkarosStudio_png,
                                            (size_t) BinaryData::io_github_thecalamityjoe87_IkarosStudio_pngSize);

    // Not opaque, so the rounded corners show the desktop behind them
    // (where the desktop supports see-through windows).
    setOpaque (false);
    setAlwaysOnTop (true);   // stays above the main window until it closes
    centreWithSize (width, height);

    // A Component with no parent becomes a window of its own through
    // addToDesktop. No style flags = no title bar or border.
    addToDesktop (juce::ComponentPeer::windowHasDropShadow);
    setVisible (true);

    createdAt = juce::Time::getMillisecondCounter();
    startTimer (20);
}

int StartupSplash::getRemainingMilliseconds() const
{
    const auto shownFor = juce::Time::getMillisecondCounter() - createdAt;
    return shownFor >= minimumVisibleMs ? 0 : (int) (minimumVisibleMs - shownFor);
}

void StartupSplash::setStage (const juce::String& text, float fraction)
{
    stage = text;
    progress = juce::jlimit (0.0f, 1.0f, fraction);

    repaint();

    // Draws the pending repaint now instead of on a later pass of the
    // message loop. Without this the splash would stay on its first text.
    if (auto* peer = getPeer())
        peer->performAnyPendingRepaintsNow();
}

void StartupSplash::timerCallback()
{
    const auto now = juce::Time::getMillisecondCounter();

    // Wait one more timer tick after the first paint, so the message loop
    // sends the drawn window to the screen before the slow part blocks it.
    const bool drawn = paintedAt != 0 && now - paintedAt >= 20;

    if (drawn || now - createdAt >= paintTimeoutMs)
    {
        stopTimer();

        // Called asynchronously: onShown may take a while, and the app
        // might delete the splash from it.
        juce::MessageManager::callAsync ([callback = onShown] { callback(); });
    }
}

void StartupSplash::paint (juce::Graphics& g)
{
    if (paintedAt == 0)
        paintedAt = juce::Time::getMillisecondCounter();

    const auto bounds = getLocalBounds().toFloat();
    g.setColour (background);
    g.fillRoundedRectangle (bounds, cornerRadius);
    g.setColour (juce::Colours::white.withAlpha (0.08f));
    g.drawRoundedRectangle (bounds.reduced (0.5f), cornerRadius, 1.0f);

    // The icon's own square is the same colour as the background, so only
    // its bars stand out.
    auto area = getLocalBounds().reduced (24);
    area.removeFromTop (22);
    g.setOpacity (1.0f);   // drawImage uses the current colour's alpha (the faint border's)
    g.drawImage (icon, area.removeFromTop (iconSize).withSizeKeepingCentre (iconSize, iconSize).toFloat(),
                 juce::RectanglePlacement::centred);

    area.removeFromTop (14);
    g.setColour (title);
    g.setFont (fonts::uiBold (28.0f));
    g.drawText ("Ikaros Studio", area.removeFromTop (36), juce::Justification::centred);

    g.setColour (subtitle);
    g.setFont (fonts::ui (13.0f));
    g.drawText ("Version " + juce::JUCEApplication::getInstance()->getApplicationVersion() + " (alpha)",
                area.removeFromTop (20), juce::Justification::centred);

    g.setColour (subtitle);
    g.setFont (fonts::ui (12.0f));
    g.drawText (stage, area.removeFromBottom (16), juce::Justification::centred);

    // A thin bar above the text: a track, and the amber part for what's done.
    area.removeFromBottom (8);
    const auto track = area.removeFromBottom (3).reduced (40, 0).toFloat();
    g.setColour (juce::Colours::white.withAlpha (0.1f));
    g.fillRoundedRectangle (track, 1.5f);
    g.setColour (accent);
    g.fillRoundedRectangle (track.withWidth (track.getWidth() * progress), 1.5f);
}

} // namespace ikaros
