#include "ui/widgets/FolderField.h"
#include "ui/theme/Fonts.h"
#include "ui/theme/Tokens.h"

namespace ikaros
{

using namespace tokens;

namespace
{
    constexpr int chooseWidth = 92, defaultWidth = 76, gap = 8;

    // "/home/me/Music" as "~/Music" (not on Windows, where paths are written in full).
    juce::String describe (const juce::File& folder)
    {
        auto path = folder.getFullPathName();

       #if ! JUCE_WINDOWS
        const auto home = juce::File::getSpecialLocation (juce::File::userHomeDirectory).getFullPathName();

        if (path == home || path.startsWith (home + "/"))
            path = "~" + path.substring (home.length());
       #endif

        return path;
    }

    // A path too long for its space loses its start, which says less than its end.
    juce::String fitFromStart (const juce::String& text, const juce::Font& font, float width)
    {
        auto shown = text;

        while (shown.length() > 4 && juce::GlyphArrangement::getStringWidth (font, juce::String::fromUTF8 ("\xe2\x80\xa6") + shown) > width)
            shown = shown.substring (1);

        return shown == text ? text : juce::String::fromUTF8 ("\xe2\x80\xa6") + shown;
    }
}

FolderField::FolderField (const juce::String& n) : name (n)
{
    setTitle (name);

    for (auto* b : { &chooseButton, &defaultButton })
    {
        b->setColours (IconButton::borderedColours);
        b->setFont (fonts::ui (textSize::md));
        addAndMakeVisible (b);
    }

    chooseButton.onClick = [this] { chooseFolder(); };
    defaultButton.onClick = [this]
    {
        if (onChanged)
            onChanged ({});
    };
}

void FolderField::setFolder (const juce::File& chosen, const juce::File& defaultOne)
{
    chosenFolder = chosen;
    defaultFolder = defaultOne;
    defaultButton.setEnabled (chosen != juce::File());
    setTooltip ((chosen != juce::File() ? chosen : defaultOne).getFullPathName());
    repaint();
}

void FolderField::chooseFolder()
{
    const auto start = chosenFolder != juce::File() ? chosenFolder : defaultFolder;
    chooser = std::make_unique<juce::FileChooser> (name, start);

    // The chooser runs asynchronously; SafePointer is null if this has been deleted meanwhile.
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                          [safeThis = SafePointer<FolderField> (this)] (const juce::FileChooser& fc)
    {
        if (safeThis != nullptr && fc.getResult() != juce::File() && safeThis->onChanged)
            safeThis->onChanged (fc.getResult());
    });
}

void FolderField::paint (juce::Graphics& g)
{
    g.setColour (theme().wellBg);
    g.fillRoundedRectangle (pathArea.toFloat(), size::radiusSmall);
    g.setColour (theme().borderSoft);
    g.drawRoundedRectangle (pathArea.toFloat().reduced (0.5f), size::radiusSmall, 1.0f);

    const bool isDefault = chosenFolder == juce::File();
    const auto font = fonts::ui (textSize::md);
    const auto text = describe (isDefault ? defaultFolder : chosenFolder);

    g.setColour (isDefault ? theme().textSecondary : theme().text);
    g.setFont (font);
    g.drawText (fitFromStart (text, font, (float) pathArea.getWidth() - 20.0f), pathArea.reduced (10, 0),
                juce::Justification::centredLeft);
}

void FolderField::resized()
{
    auto r = getLocalBounds();
    defaultButton.setBounds (r.removeFromRight (defaultWidth));
    r.removeFromRight (gap);
    chooseButton.setBounds (r.removeFromRight (chooseWidth));
    r.removeFromRight (gap);
    pathArea = r;
}

} // namespace ikaros
