#include "ui/widgets/Icons.h"
#include <map>

namespace ikaros::icons
{

namespace
{
    const char* svgFor (Icon icon)
    {
        switch (icon)
        {
            case Icon::library:   return "M5 4h14a2 2 0 0 1 2 2v12a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2V6a2 2 0 0 1 2-2z M9 4v16";
            case Icon::plus:      return "M12 5v14 M5 12h14";
            case Icon::undo:      return "M9 14L4 9l5-5 M4 9h11a5 5 0 0 1 0 10h-3";
            case Icon::toStart:   return "M6 5v14 M19 5l-9 7 9 7z";
            case Icon::play:      return "M8 5v14l11-7z";
            case Icon::pause:     return "M7 5h3v14H7z M14 5h3v14h-3z";
            case Icon::record:    return "M12 5a7 7 0 1 1 0 14a7 7 0 1 1 0-14z";
            case Icon::loop:      return "M17 2l3 3-3 3 M4 11V9a4 4 0 0 1 4-4h12 M7 22l-3-3 3-3 M20 13v2a4 4 0 0 1-4 4H4";
            case Icon::metronome: return "M9 3h6l4 18H5z M12 15l5-8 M7 17h10";
            case Icon::search:    return "M11 5a6 6 0 1 1 0 12a6 6 0 1 1 0-12z M20 20l-4.5-4.5";
            case Icon::keyboards: return "M3 5h18v14H3z M8 5v8 M12 5v8 M16 5v8 M6.5 13v6 M10 13v6 M14 13v6 M17.5 13v6";
            case Icon::drums:     return "M4 9c0-2 3.6-3 8-3s8 1 8 3-3.6 3-8 3-8-1-8-3z M4 9v6c0 2 3.6 3 8 3s8-1 8-3V9 M15 3l-3 6";
            case Icon::bass:      return "M3 12c2-6 4-6 6 0s4 6 6 0 4-6 6 0";
            case Icon::synths:    return "M5 20V4 M10 20V4 M15 20V4 M20 20V4 M3 8h4 M8 15h4 M13 10h4 M18 13h4";
            case Icon::guitar:    return "M19 3l2 2 M20 4l-8 8 M11.5 12.5a4 4 0 1 0-1 6.5 3 3 0 0 0 2-5z";
            case Icon::strings:   return "M8 3v18 M12 3v18 M16 3v18 M5 17h14";
            case Icon::loops:     return "M17 2l3 3-3 3 M4 11V9a4 4 0 0 1 4-4h12 M7 22l-3-3 3-3 M20 13v2a4 4 0 0 1-4 4H4";
            case Icon::mic:       return "M12 3a3 3 0 0 1 3 3v6a3 3 0 0 1-6 0V6a3 3 0 0 1 3-3z M6 11a6 6 0 0 0 12 0 M12 17v4";

            // Window controls: drawn thin and small, like typical title-bar glyphs.
            case Icon::windowMinimise: return "M6 12h12";
            case Icon::windowMaximise: return "M6 6h12v12H6z";
            case Icon::windowRestore:  return "M6 9h9v9H6z M9 9V6h9v9h-3";
            case Icon::windowClose:    return "M6 6l12 12 M18 6L6 18";
        }

        return "";
    }
}

const juce::Path& get (Icon icon)
{
    // Parse each SVG string once and cache the resulting Path.
    static std::map<Icon, juce::Path> cache;

    auto it = cache.find (icon);

    if (it == cache.end())
        it = cache.emplace (icon, juce::Drawable::parseSVGPath (svgFor (icon))).first;

    return it->second;
}

bool isFilled (Icon icon)
{
    return icon == Icon::play || icon == Icon::pause || icon == Icon::record;
}

void draw (juce::Graphics& g, Icon icon, juce::Rectangle<float> area, juce::Colour colour,
           float iconSize, float strokeWidth)
{
    auto box = area.withSizeKeepingCentre (iconSize, iconSize);
    auto transform = juce::AffineTransform::scale (iconSize / 24.0f).translated (box.getX(), box.getY());
    auto path = get (icon);
    path.applyTransform (transform);

    g.setColour (colour);

    if (isFilled (icon))
        g.fillPath (path);
    else
        // strokeWidth is in SVG units (like stroke-width="1.8"), so scale it with the icon.
        g.strokePath (path, juce::PathStrokeType (strokeWidth * iconSize / 24.0f,
                                                  juce::PathStrokeType::curved,
                                                  juce::PathStrokeType::rounded));
}

Icon forCategory (const juce::String& c)
{
    if (c == "Keyboards")   return Icon::keyboards;
    if (c == "Drums")       return Icon::drums;
    if (c == "Bass")        return Icon::bass;
    if (c == "Guitar")      return Icon::guitar;
    if (c == "Strings")     return Icon::strings;
    if (c == "Loops")       return Icon::loops;
    if (c == "Audio Files") return Icon::mic;
    return Icon::synths;
}

} // namespace ikaros::icons
