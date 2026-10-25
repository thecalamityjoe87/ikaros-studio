#include "ui/dialogs/AboutTechnology.h"
#include "BinaryData.h"
#include "ui/theme/Fonts.h"
#include "ui/theme/Theme.h"
#include "ui/theme/Tokens.h"

namespace ikaros
{

using namespace tokens;

namespace
{
    constexpr int headingHeight = 22, headingGap = 10, sectionGap = 22;
    constexpr int cardHeight = 186, cardGap = 12, cardPadding = 16, logoHeight = 64;
    constexpr int rowHeight = 58, rowGap = 4, tileSize = 40;
    constexpr int footerHeight = 40;

    juce::Image loadImage (const void* data, int size)
    {
        return juce::ImageFileFormat::loadFrom (data, (size_t) size);
    }

    void openInBrowser (const juce::String& url)
    {
        juce::URL (url).launchInDefaultBrowser();
    }
}

//==============================================================================
// One project: a card with its logo, or a row with a lettered tile. A button,
// so it can be clicked, focused and read out by a screen reader.
class AboutTechnology::Entry  : public juce::Button
{
public:
    Entry (juce::String nameIn, juce::String tileIn, juce::String whatIn, juce::String licenseIn,
           juce::String urlIn, juce::Image logoIn = {})
        : juce::Button (nameIn), name (nameIn), tile (tileIn), what (whatIn), license (licenseIn),
          url (urlIn), logo (logoIn)
    {
        setTitle (name);
        setDescription (what + ". " + license + ".");
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        onClick = [this] { openInBrowser (url); };
    }

    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        if (logo.isValid())
            paintCard (g, over, down);
        else
            paintRow (g, over, down);
    }

private:
    juce::Colour background (bool over, bool down, juce::Colour normal) const
    {
        return down ? theme().rowPressed : over ? theme().rowHover : normal;
    }

    void paintCard (juce::Graphics& g, bool over, bool down)
    {
        auto bounds = getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (background (over, down, theme().cardBg));
        g.fillRoundedRectangle (bounds, size::radiusCard);
        g.setColour (theme().border);
        g.drawRoundedRectangle (bounds, size::radiusCard, 1.0f);

        auto r = getLocalBounds().reduced (cardPadding);

        // drawImage draws at the current colour's opacity.
        g.setOpacity (1.0f);
        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        g.drawImage (logo, r.removeFromTop (logoHeight).toFloat(), juce::RectanglePlacement::centred);
        r.removeFromTop (12);

        g.setColour (theme().text);
        g.setFont (fonts::uiBold (textSize::lg));
        g.drawText (name, r.removeFromTop (20), juce::Justification::centred);
        r.removeFromTop (4);

        g.setColour (theme().textSecondary);
        g.setFont (fonts::ui (textSize::sm));
        g.drawFittedText (what, r.removeFromTop (34), juce::Justification::centredTop, 2, 1.0f);
        r.removeFromTop (4);

        g.setFont (fonts::ui (textSize::xs));
        g.drawText (license, r.removeFromTop (16), juce::Justification::centred);
    }

    void paintRow (juce::Graphics& g, bool over, bool down)
    {
        if (over || down)
        {
            g.setColour (background (over, down, {}));
            g.fillRoundedRectangle (getLocalBounds().toFloat(), size::radiusLarge);
        }

        auto r = getLocalBounds().reduced (8, 0);
        auto tileArea = r.removeFromLeft (tileSize).withSizeKeepingCentre (tileSize, tileSize).toFloat();
        g.setColour (theme().tileBg);
        g.fillRoundedRectangle (tileArea, size::radiusMedium);
        g.setColour (theme().tileBorder);
        g.drawRoundedRectangle (tileArea.reduced (0.5f), size::radiusMedium, 1.0f);
        g.setColour (theme().text);
        g.setFont (fonts::uiBold (textSize::sm));
        g.drawText (tile, tileArea, juce::Justification::centred);

        r.removeFromLeft (12);
        auto lines = r.withSizeKeepingCentre (r.getWidth(), 38);
        auto first = lines.removeFromTop (20);

        // The name, then the license after it in the secondary colour.
        const auto nameFont = fonts::uiBold (textSize::md);
        const auto nameWidth = juce::roundToInt (std::ceil (juce::GlyphArrangement::getStringWidth (nameFont, name)));
        g.setColour (theme().text);
        g.setFont (nameFont);
        g.drawText (name, first.removeFromLeft (nameWidth), juce::Justification::centredLeft);

        g.setColour (theme().textSecondary);
        g.setFont (fonts::ui (textSize::sm));
        g.drawText (juce::String::fromUTF8 ("  \xc2\xb7  ") + license, first, juce::Justification::centredLeft, true);
        g.drawText (what, lines, juce::Justification::centredLeft, true);
    }

    juce::String name, tile, what, license, url;
    juce::Image logo;
};

//==============================================================================
AboutTechnology::AboutTechnology()
{
    featured.add (new Entry ("Tracktion Engine", {},
                             "The audio engine underneath: playback, recording, editing, plug-ins and mixing",
                             "GPLv3", "https://github.com/Tracktion/tracktion_engine",
                             loadImage (BinaryData::tracktion_engine_powered_png, BinaryData::tracktion_engine_powered_pngSize)));
    featured.add (new Entry ("JUCE", {},
                             "The framework for the app's windows, graphics, and audio and MIDI devices",
                             "AGPLv3", "https://juce.com",
                             loadImage (BinaryData::juce_icon_png, BinaryData::juce_icon_pngSize)));

    others.add (new Entry ("sfizz", "sfz", "Plays the sampled instruments",
                           "BSD 2-Clause", "https://sfz.tools/sfizz"));
    others.add (new Entry ("Rubber Band Library", "RB", "Stretches loops and recordings to the song's tempo and key",
                           "GPLv2 or later", "https://breakfastquay.com/rubberband"));
    others.add (new Entry ("Guitarix and GxPlugins", "Gx", "The sound of the Amp and most of the pedalboard's pedals",
                           "GPL", "https://github.com/brummer10/guitarix"));
    others.add (new Entry ("KlonCentaur and chowdsp_utils", "KC", "The sound of the Golden Boost pedal",
                           "BSD 3-Clause, GPLv3", "https://github.com/jatinchowdhury18/KlonCentaur"));
    others.add (new Entry ("fat1 and zita-at1", "fat1", "The pitch correction behind the Tune effect",
                           "GPLv2 or later", "https://github.com/x42/fat1.lv2"));
    others.add (new Entry ("pitch-detection", "pY", "Finds the notes in the audio editor's Pitch mode",
                           "MIT", "https://github.com/sevagh/pitch-detection"));
    others.add (new Entry ("LAME", "LAME", "Encodes MP3 exports",
                           "LGPL", "https://lame.sourceforge.io"));
    others.add (new Entry ("miniz", "mz", "Reads and writes .iksession files",
                           "MIT", "https://github.com/richgel999/miniz"));
    others.add (new Entry ("Lucide", "Lu", "Some of the instrument icons",
                           "ISC", "https://lucide.dev"));

    for (auto* e : featured) addAndMakeVisible (e);
    for (auto* e : others)   addAndMakeVisible (e);
}

AboutTechnology::~AboutTechnology() = default;

int AboutTechnology::getHeightForWidth (int) const
{
    return headingHeight + headingGap + cardHeight + sectionGap
         + headingHeight + headingGap + others.size() * (rowHeight + rowGap)
         + footerHeight;
}

void AboutTechnology::resized()
{
    auto r = getLocalBounds();

    builtOnHeading = r.removeFromTop (headingHeight);
    r.removeFromTop (headingGap);

    auto cards = r.removeFromTop (cardHeight);
    const auto cardWidth = (cards.getWidth() - cardGap * (featured.size() - 1)) / featured.size();

    for (auto* e : featured)
    {
        e->setBounds (cards.removeFromLeft (cardWidth));
        cards.removeFromLeft (cardGap);
    }

    r.removeFromTop (sectionGap);
    alsoInsideHeading = r.removeFromTop (headingHeight);
    r.removeFromTop (headingGap);

    for (auto* e : others)
    {
        e->setBounds (r.removeFromTop (rowHeight));
        r.removeFromTop (rowGap);
    }

    footer = r.removeFromTop (footerHeight);
}

void AboutTechnology::paint (juce::Graphics& g)
{
    g.setColour (theme().text);
    g.setFont (fonts::uiBold (textSize::lg));
    g.drawText ("Built on", builtOnHeading, juce::Justification::centredLeft);
    g.drawText ("Also inside", alsoInsideHeading, juce::Justification::centredLeft);

    g.setColour (theme().textSecondary);
    g.setFont (fonts::ui (textSize::sm));
    g.drawFittedText ("Click a project to visit its website. Credits and licenses are under Acknowledgements, "
                      "and the full license texts are in the CREDITS file that comes with Ikaros Studio.",
                      footer, juce::Justification::bottomLeft, 2, 1.0f);
}

} // namespace ikaros
