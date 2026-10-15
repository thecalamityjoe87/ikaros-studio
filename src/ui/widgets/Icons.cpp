#include "ui/widgets/Icons.h"
#include <map>

/*
    The keyboard (piano), drums, bass (bass clef) and guitar icons are from
    Lucide 1.55.0 (https://lucide.dev), converted to plain SVG paths:

    ISC License

    Copyright (c) 2026 Lucide Icons and Contributors

    Permission to use, copy, modify, and/or distribute this software for any
    purpose with or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
    MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/

namespace ikaros::icons
{

namespace
{
    const char* svgFor (Icon icon)
    {
        switch (icon)
        {
            case Icon::library:   return "M4 4v16 M8 8v12 M12 6v14 M16 6l4 14";   // books on a shelf, one leaning
            case Icon::plus:      return "M12 5v14 M5 12h14";
            case Icon::undo:      return "M9 14L4 9l5-5 M4 9h11a5 5 0 0 1 0 10h-3";
            case Icon::redo:      return "M15 14l5-5-5-5 M20 9H9a5 5 0 0 0 0 10h3";   // undo, mirrored
            case Icon::toStart:   return "M6 5v14 M19 5l-9 7 9 7z";
            case Icon::play:      return "M8 5v14l11-7z";
            case Icon::pause:     return "M7 5h3v14H7z M14 5h3v14h-3z";
            case Icon::record:    return "M12 5a7 7 0 1 1 0 14a7 7 0 1 1 0-14z";
            case Icon::loop:      return "M17 2l3 3-3 3 M4 11V9a4 4 0 0 1 4-4h12 M7 22l-3-3 3-3 M20 13v2a4 4 0 0 1-4 4H4";
            case Icon::metronome: return "M9 3h6l4 18H5z M12 15l5-8 M7 17h10";
            case Icon::search:    return "M11 5a6 6 0 1 1 0 12a6 6 0 1 1 0-12z M20 20l-4.5-4.5";
            // Lucide "piano": a grand piano from the side.
            case Icon::keyboards: return "M10 13v4 M14 13v4 M18 13v4 M2 13h20 M6 13v4 "
                                         "M22 11.5A3.5 3.5 0 0 0 18.5 8a3.52 3.52 0 0 1-3.173-2A7 7 0 0 0 2 9v10a2 2 0 0 0 2 2h16a2 2 0 0 0 2-2z";
            // Lucide "drum": a drum with two crossed sticks.
            case Icon::drums:     return "M2 2l8 8 M22 2l-8 8 M2 9a10 5 0 1 0 20 0a10 5 0 1 0-20 0z "
                                         "M7 13.4v7.9 M12 14v8 M17 13.4v7.9 M2 9v8a10 5 0 0 0 20 0V9";
            // Lucide "clef-bass": the bass clef. (There's no bass guitar icon,
            // and a second guitar would be hard to tell apart.)
            case Icon::bass:      return "M19 11h.01 M19 6h.01 M5 8c0-4 4-4 4-4 6 0 6 6 6 6 0 7-10 11-10 11 "
                                         "M5 8a2 2 0 1 0 4 0a2 2 0 1 0-4 0z";
            case Icon::synths:    return "M5 20V4 M10 20V4 M15 20V4 M20 20V4 M3 8h4 M8 15h4 M13 10h4 M18 13h4";
            // Lucide "guitar": an acoustic guitar with its headstock.
            case Icon::guitar:    return "M11.9 12.1l4.514-4.514 M6 16l2 2 "
                                         "M20.1 2.3a1 1 0 0 0-1.4 0l-1.114 1.114A2 2 0 0 0 17 4.828v1.344a2 2 0 0 1-.586 1.414"
                                         "A2 2 0 0 1 17.828 7h1.344a2 2 0 0 0 1.414-.586L21.7 5.3a1 1 0 0 0 0-1.4z "
                                         "M8.23 9.85A3 3 0 0 1 11 8a5 5 0 0 1 5 5 3 3 0 0 1-1.85 2.77l-.92.38A2 2 0 0 0 12 18"
                                         "a4 4 0 0 1-4 4 6 6 0 0 1-6-6 4 4 0 0 1 4-4 2 2 0 0 0 1.85-1.23z";
            case Icon::strings:   return "M8 3v18 M12 3v18 M16 3v18 M5 17h14";
            case Icon::woodwinds: return "M3.5 17.5l13-13 3 3-13 13z M9 15h.01 M12 12h.01 M15 9h.01";   // a flute with finger holes
            case Icon::brass:     return "M3 9h5l11-5v16L8 15H3z M8 15v5h3";   // a horn with a flared bell
            case Icon::loops:     return "M17 2l3 3-3 3 M4 11V9a4 4 0 0 1 4-4h12 M7 22l-3-3 3-3 M20 13v2a4 4 0 0 1-4 4H4";
            case Icon::mic:       return "M12 3a3 3 0 0 1 3 3v6a3 3 0 0 1-6 0V6a3 3 0 0 1 3-3z M6 11a6 6 0 0 0 12 0 M12 17v4";

            // Window controls: drawn thin and small, like typical title-bar glyphs.
            case Icon::windowMinimise: return "M6 12h12";
            case Icon::windowMaximise: return "M6 6h12v12H6z";
            case Icon::windowRestore:  return "M6 9h9v9H6z M9 9V6h9v9h-3";
            case Icon::windowClose:    return "M6 6l12 12 M18 6L6 18";

            case Icon::power:          return "M12 4v8 M7.5 7a7 7 0 1 0 9 0";
            case Icon::more:           return "M5.5 12h.5 M12 12h.5 M18.5 12h.5";   // three dots (round caps)
            case Icon::chevronDown:    return "M7 10l5 5 5-5";
            case Icon::chevronRight:   return "M10 7l5 5-5 5";
            case Icon::chevronUp:      return "M7 14l5-5 5 5";
            case Icon::chevronLeft:    return "M14 7l-5 5 5 5";
            case Icon::clock:          return "M12 3a9 9 0 1 0 0 18a9 9 0 1 0 0-18 M12 7v5l3.5 2";
            case Icon::headphones:     return "M4 15v-3a8 8 0 0 1 16 0v3 M4 15h3v5H4z M17 15h3v5h-3z";
            case Icon::waveform:       return "M4 10v4 M8 7v10 M12 4v16 M16 7v10 M20 10v4";
            case Icon::mute:           return "M3 9h4l5-4v14l-5-4H3z M16 9l5 6 M21 9l-5 6";   // speaker with a cross
            case Icon::plugin:         return "M9 3v5 M15 3v5 M6 8h12v4a6 6 0 0 1-12 0z M12 18v3";   // a mains plug
            case Icon::folder:         return "M3 6a1 1 0 0 1 1-1h5l2 2h9a1 1 0 0 1 1 1v10a1 1 0 0 1-1 1H4a1 1 0 0 1-1-1z";
            case Icon::stop:           return "M7 7h10v10H7z";
            case Icon::warning:        return "M12 3.5l9.5 17h-19z M12 10v5 M12 18h.01";
            case Icon::openWindow:     return "M14 4h6v6 M20 4l-9 9 M18 14v5a1 1 0 0 1-1 1H5a1 1 0 0 1-1-1V7a1 1 0 0 1 1-1h5";   // box with an arrow out
            // A ramp between two points: low, up, high.
            case Icon::automation:     return "M2 17h1.5 M8.5 17a2.5 2.5 0 1 0-5 0a2.5 2.5 0 1 0 5 0 M7.8 15.2l6.4-6.4 "
                                              "M18.5 7a2.5 2.5 0 1 0-5 0a2.5 2.5 0 1 0 5 0 M20.5 7H22";
            case Icon::download:       return "M12 4v11 M7 10l5 5l5-5 M4 17v2a1 1 0 0 0 1 1h14a1 1 0 0 0 1-1v-2";   // arrow down into a tray
            case Icon::trim:           return "M3 12h18 M7 8l-4 4 4 4 M17 8l4 4-4 4";   // left-right arrow, like the resize cursor
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
    return icon == Icon::play || icon == Icon::pause || icon == Icon::record || icon == Icon::stop;
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
    if (c == "Woodwinds")   return Icon::woodwinds;
    if (c == "Brass")       return Icon::brass;
    if (c == "Loops")       return Icon::loops;
    if (c == "Audio Files") return Icon::mic;
    if (c == "Plug-ins")    return Icon::plugin;
    return Icon::synths;
}

const std::vector<TrackIconChoice>& getTrackIconChoices()
{
    static const std::vector<TrackIconChoice> choices {
        { "keyboard", "Keyboard",   Icon::keyboards },
        { "synth",    "Synth",      Icon::synths },
        { "bass",     "Bass",       Icon::bass },
        { "guitar",   "Guitar",     Icon::guitar },
        { "drums",    "Drums",      Icon::drums },
        { "strings",  "Strings",    Icon::strings },
        { "loops",    "Loops",      Icon::loops },
        { "mic",      "Microphone", Icon::mic },
    };

    return choices;
}

Icon forTrack (const juce::String& iconKey, const juce::String& category)
{
    for (auto& choice : getTrackIconChoices())
        if (choice.key == iconKey)
            return choice.icon;

    return forCategory (category);   // automatic (or a key from a newer version)
}

std::unique_ptr<juce::Drawable> createDrawable (Icon icon, juce::Colour colour)
{
    // Drawn into a fixed square, so every icon keeps the same size and
    // padding (a path's own bounds would differ from icon to icon).
    juce::Image image (juce::Image::ARGB, 48, 48, true);
    juce::Graphics g (image);
    draw (g, icon, { 0.0f, 0.0f, 48.0f, 48.0f }, colour, 44.0f, 1.8f);

    auto drawable = std::make_unique<juce::DrawableImage>();
    drawable->setImage (image);
    return drawable;
}

} // namespace ikaros::icons
