#include "engine/SectionEditor.h"
#include "engine/AudioEngine.h"
#include "engine/FlexHelpers.h"
#include "engine/TrackHelpers.h"
#include <algorithm>

namespace ikaros
{

using namespace detail;

namespace
{
    // The sections live in the Edit's own ValueTree, which Tracktion saves
    // as it is (unknown children included), so they're in the .iksession and
    // every change to them goes through the undo manager like any other edit.
    //
    //   EDIT
    //     IKAROSSECTIONS
    //       SECTION id=… name="Verse" colour="ff5aa9ff" start=16 end=48   (beats)
    const juce::Identifier sectionsId ("IKAROSSECTIONS");
    const juce::Identifier sectionId ("SECTION");
    const juce::Identifier nameProp ("name"), colourProp ("colour"), startProp ("start"), endProp ("end");

    // Far past the end of any song: "everything after this point".
    constexpr double forever = 1.0e9;

    // Closer than this (in beats) counts as the same place.
    constexpr double tolerance = 1.0e-6;

    // Names for new sections, in the order a song often goes. After these
    // they're just numbered.
    const juce::StringArray suggestedNames { "Intro", "Verse", "Chorus", "Verse", "Chorus", "Bridge", "Chorus", "Outro" };

    SectionInfo makeInfo (const juce::ValueTree& s)
    {
        return { (SectionId) (juce::int64) s.getProperty (te::IDs::id),
                 s.getProperty (nameProp).toString(),
                 juce::Colour::fromString (s.getProperty (colourProp).toString()),
                 (double) s.getProperty (startProp),
                 (double) s.getProperty (endProp) };
    }

    juce::ValueTree findSectionState (te::Edit* edit, SectionId id)
    {
        if (edit != nullptr)
            for (auto s : edit->state.getChildWithName (sectionsId))   // ValueTree iterates over its children
                if ((SectionId) (juce::int64) s.getProperty (te::IDs::id) == id)
                    return s;

        return {};
    }

    //==============================================================================
    // Automation, as plain points in beats while it's being rearranged.
    struct Point
    {
        double beat;
        float value, curve;
        double side;        // which part of the song it belongs to (see pinCuts)
        bool pin = false;   // added by pinCuts, not drawn by the user
    };

    // The curve's value at a beat (just before it, if fromLeft): straight
    // lines between points, flat before the first and after the last, as
    // Tracktion plays them. Two points on one beat are a jump: fromLeft
    // gives the first one's value, otherwise the second's.
    float valueAt (const std::vector<Point>& points, double beat, bool fromLeft)
    {
        // The first point after the beat (at or after it, from the left).
        auto next = std::find_if (points.begin(), points.end(), [&] (const Point& p)
                                  { return fromLeft ? p.beat >= beat : p.beat > beat; });

        if (next == points.begin())
            return points.front().value;

        if (next == points.end())
            return points.back().value;

        auto& a = *(next - 1);
        auto& b = *next;

        if (b.beat - a.beat < tolerance)
            return b.value;

        return (float) juce::jmap (beat, a.beat, b.beat, (double) a.value, (double) b.value);
    }

    // Where the song is cut, the curve gets a point on each side holding its
    // value there, so each part keeps its own shape wherever it ends up (and
    // the curve jumps where two parts that didn't belong together now meet).
    // The left point belongs to the part ending at the cut, the right one to
    // the part starting there.
    void pinCuts (std::vector<Point>& points, const std::vector<double>& cuts)
    {
        std::vector<Point> pins;

        for (auto c : cuts)
        {
            pins.push_back ({ c, valueAt (points, c, true), 0.0f, c - tolerance, true });
            pins.push_back ({ c, valueAt (points, c, false), 0.0f, c, true });
        }

        points.insert (points.end(), pins.begin(), pins.end());
    }

    // Takes away the pins that turned out not to be needed: ones on the
    // straight line between their neighbours, or repeating the point before.
    // The user's own points are always kept.
    std::vector<Point> dropUnneededPins (const std::vector<Point>& points)
    {
        std::vector<Point> kept;

        for (size_t i = 0; i < points.size(); ++i)
        {
            const auto& p = points[i];

            if (p.pin && ! kept.empty())
            {
                const auto& prev = kept.back();
                const bool repeat = std::abs (p.beat - prev.beat) < tolerance && std::abs (p.value - prev.value) < 1.0e-6f;
                bool onLine = false;

                if (i + 1 < points.size())
                {
                    const auto& next = points[i + 1];

                    if (prev.beat < p.beat - tolerance && p.beat < next.beat - tolerance)
                        onLine = std::abs (juce::jmap (p.beat, prev.beat, next.beat, (double) prev.value, (double) next.value) - p.value) < 1.0e-5;
                    else if (std::abs (p.beat - next.beat) < tolerance)
                        onLine = std::abs (p.value - next.value) < 1.0e-6f;   // the next point says the same
                }
                else
                {
                    onLine = std::abs (p.value - prev.value) < 1.0e-6f;   // flat to the end anyway
                }

                if (repeat || onLine)
                    continue;
            }
            else if (p.pin && i + 1 < points.size() && std::abs (p.value - points[i + 1].value) < 1.0e-6f)
            {
                continue;   // flat from the start anyway
            }

            kept.push_back (p);
        }

        return kept;
    }

    // Makes `parent`'s POINT children match `points`, reusing the trees in
    // order (so a curve whose points only moved records just the property
    // changes), all through the undo manager.
    void writePoints (te::Edit& edit, juce::ValueTree curve, const std::vector<Point>& points)
    {
        auto* um = &edit.getUndoManager();

        while (curve.getNumChildren() > (int) points.size())
            curve.removeChild (curve.getNumChildren() - 1, um);

        for (size_t i = 0; i < points.size(); ++i)
        {
            auto tree = curve.getChild ((int) i);

            if (! tree.isValid())
            {
                tree = juce::ValueTree (te::IDs::POINT);
                curve.appendChild (tree, um);
            }

            // Setting a property to the value it already has records nothing.
            tree.setProperty (te::IDs::t, beatToTime (edit, points[i].beat).inSeconds(), um);
            tree.setProperty (te::IDs::v, points[i].value, um);
            tree.setProperty (te::IDs::c, points[i].curve, um);
        }
    }
}

//==============================================================================
SectionEditor::SectionEditor (AudioEngine& o)  : owner (o) {}

const std::vector<juce::Colour>& SectionEditor::getSectionColours()
{
    static const std::vector<juce::Colour> colours {
        juce::Colour (0xff5aa9ff), juce::Colour (0xffff8f5a), juce::Colour (0xff4fd18b), juce::Colour (0xffc78bff),
        juce::Colour (0xffffd25a), juce::Colour (0xffff6fa8), juce::Colour (0xff4fd1c5)
    };

    return colours;
}

bool SectionEditor::isSectionState (const juce::ValueTree& tree)
{
    return tree.hasType (sectionsId) || tree.hasType (sectionId);
}

std::vector<SectionInfo> SectionEditor::getSections() const
{
    std::vector<SectionInfo> result;

    if (auto* edit = owner.getEdit())
        for (auto s : edit->state.getChildWithName (sectionsId))
            result.push_back (makeInfo (s));

    std::sort (result.begin(), result.end(), [] (auto& a, auto& b) { return a.startBeat < b.startBeat; });
    return result;
}

std::optional<SectionInfo> SectionEditor::getSection (SectionId id) const
{
    if (auto s = findSectionState (owner.getEdit(), id); s.isValid())
        return makeInfo (s);

    return std::nullopt;
}

SectionId SectionEditor::addSection (int bar)
{
    auto* edit = owner.getEdit();

    if (edit == nullptr || bar < 0)
        return 0;

    const auto tempo = owner.getTempoMap();
    const double start = tempo.barToBeat (bar);
    double end = tempo.barToBeat (bar + defaultBars);
    const auto sections = getSections();

    for (auto& s : sections)
    {
        if (start >= s.startBeat - tolerance && start < s.endBeat - tolerance)
            return 0;   // already inside one

        if (s.startBeat > start)
            end = juce::jmin (end, s.startBeat);   // stop where the next one starts
    }

    // A new colour each time, round the palette.
    const auto& colours = getSectionColours();
    const auto count = (int) sections.size();
    const auto name = count < suggestedNames.size() ? suggestedNames[count] : "Section " + juce::String (count + 1);

    owner.beginUndoStep ("Add Section");
    auto* um = &edit->getUndoManager();

    // createNewItemID gives an ID no other part of the Edit uses (it's what
    // Tracktion gives tracks and clips), saved with the section.
    const auto id = edit->createNewItemID().getRawID();

    juce::ValueTree s (sectionId);
    s.setProperty (te::IDs::id, (juce::int64) id, nullptr);
    s.setProperty (nameProp, name, nullptr);
    s.setProperty (colourProp, colours[(size_t) count % colours.size()].toString(), nullptr);
    s.setProperty (startProp, start, nullptr);
    s.setProperty (endProp, end, nullptr);

    // Only adding the child needs the undo manager: undoing that takes the
    // whole section (with its properties) away again.
    edit->state.getOrCreateChildWithName (sectionsId, um).appendChild (s, um);
    return id;
}

void SectionEditor::renameSection (SectionId id, const juce::String& newName)
{
    auto s = findSectionState (owner.getEdit(), id);
    const auto name = newName.trim();

    if (s.isValid() && name.isNotEmpty() && name != s.getProperty (nameProp).toString())
    {
        owner.beginUndoStep ("Rename Section");
        s.setProperty (nameProp, name, &owner.getEdit()->getUndoManager());
    }
}

void SectionEditor::setSectionColour (SectionId id, juce::Colour colour)
{
    auto s = findSectionState (owner.getEdit(), id);

    if (s.isValid() && colour.toString() != s.getProperty (colourProp).toString())
    {
        owner.beginUndoStep ("Section Color");
        s.setProperty (colourProp, colour.toString(), &owner.getEdit()->getUndoManager());
    }
}

bool SectionEditor::setSectionRange (SectionId id, double start, double end)
{
    auto s = findSectionState (owner.getEdit(), id);

    if (! s.isValid() || start < -tolerance || end - start < tolerance)
        return false;

    for (auto& other : getSections())
        if (other.id != id && start < other.endBeat - tolerance && end > other.startBeat + tolerance)
            return false;

    auto* um = &owner.getEdit()->getUndoManager();
    s.setProperty (startProp, juce::jmax (0.0, start), um);
    s.setProperty (endProp, end, um);
    return true;
}

bool SectionEditor::canMoveSectionTo (SectionId id, double beat, bool copy) const
{
    const auto section = getSection (id);

    if (! section || beat < -tolerance)
        return false;

    for (auto& s : getSections())
        if (beat > s.startBeat + tolerance && beat < s.endBeat - tolerance)
            return false;   // the middle of a section (its own included)

    const bool inPlace = std::abs (beat - section->startBeat) < tolerance || std::abs (beat - section->endBeat) < tolerance;
    return copy || ! inPlace;
}

bool SectionEditor::moveSection (SectionId id, double d, bool copy)
{
    if (! canMoveSectionTo (id, d, copy))
        return false;

    auto* edit = owner.getEdit();
    const auto section = *getSection (id);
    const double s = section.startBeat, e = section.endBeat, len = e - s;

    // What goes where, as parts of the song (see Piece).
    std::vector<Piece> pieces;

    if (copy)
        pieces = { { 0.0, d, 0.0 }, { d, forever, d + len }, { s, e, d } };   // the last one is the copy
    else if (d < s)
        pieces = { { 0.0, d, 0.0 }, { s, e, d }, { d, s, d + len }, { e, forever, e } };
    else
        pieces = { { 0.0, s, 0.0 }, { e, d, s }, { s, e, d - len }, { d, forever, d } };

    owner.beginUndoStep (copy ? "Copy Section" : "Move Section");
    auto* um = &edit->getUndoManager();

    // The other sections go with their part of the song, the same way.
    for (auto tree : edit->state.getChildWithName (sectionsId))
    {
        const auto info = makeInfo (tree);
        const double middle = (info.startBeat + info.endBeat) / 2.0;

        // The first piece holding a section is where the section itself goes.
        for (auto& p : pieces)
        {
            if (middle >= p.from && middle < p.to)
            {
                if (std::abs (p.dest - p.from) > tolerance)
                {
                    tree.setProperty (startProp, info.startBeat + p.dest - p.from, um);
                    tree.setProperty (endProp, info.endBeat + p.dest - p.from, um);
                }

                break;
            }
        }
    }

    if (copy)
    {
        juce::ValueTree c (sectionId);
        c.setProperty (te::IDs::id, (juce::int64) edit->createNewItemID().getRawID(), nullptr);
        c.setProperty (nameProp, section.name, nullptr);
        c.setProperty (colourProp, section.colour.toString(), nullptr);
        c.setProperty (startProp, d, nullptr);
        c.setProperty (endProp, d + len, nullptr);
        edit->state.getChildWithName (sectionsId).appendChild (c, um);
    }

    rearrange (pieces);
    return true;
}

void SectionEditor::deleteSection (SectionId id)
{
    auto tree = findSectionState (owner.getEdit(), id);

    if (! tree.isValid())
        return;

    auto* edit = owner.getEdit();
    const auto section = makeInfo (tree);
    const double s = section.startBeat, e = section.endBeat, len = e - s;

    owner.beginUndoStep ("Delete Section");
    auto* um = &edit->getUndoManager();
    auto sections = edit->state.getChildWithName (sectionsId);
    sections.removeChild (tree, um);

    for (auto other : sections)
        if ((double) other.getProperty (startProp) >= e - tolerance)
        {
            other.setProperty (startProp, (double) other.getProperty (startProp) - len, um);
            other.setProperty (endProp, (double) other.getProperty (endProp) - len, um);
        }

    rearrange ({ { 0.0, s, 0.0 }, { e, forever, s } });
}

void SectionEditor::removeSectionMarker (SectionId id)
{
    auto tree = findSectionState (owner.getEdit(), id);

    if (tree.isValid())
    {
        owner.beginUndoStep ("Remove Section Marker");
        tree.getParent().removeChild (tree, &owner.getEdit()->getUndoManager());
    }
}

//==============================================================================
void SectionEditor::rearrange (const std::vector<Piece>& pieces)
{
    auto& edit = *owner.getEdit();

    // Where the song is cut: every piece's ends.
    std::vector<double> cuts;

    for (auto& p : pieces)
        for (auto c : { p.from, p.to })
            if (c > tolerance && c < forever
                 && std::none_of (cuts.begin(), cuts.end(), [c] (double x) { return std::abs (x - c) < tolerance; }))
                cuts.push_back (c);

    std::sort (cuts.begin(), cuts.end());

    // The first piece holding a beat (or none, if the beat is being removed).
    auto piecesAt = [&pieces] (double beat)
    {
        std::vector<const Piece*> result;

        for (auto& p : pieces)
            if (beat >= p.from && beat < p.to)
                result.push_back (&p);

        return result;
    };

    // 1. Regions. Split the ones that cross a cut, so each part lies in one
    // piece, the same way "Split" does (edge fades where it cut).
    for (auto* track : te::getAudioTracks (edit))
    {
        const auto clips = track->getClips();   // a copy of the list: splitting adds to it

        for (auto* clip : clips)
        {
            auto* current = clip;

            for (auto c : cuts)
            {
                if (c <= current->getStartBeat().inBeats() + 0.01 || c >= current->getEndBeat().inBeats() - 0.01)
                    continue;

                auto* right = track->splitClip (*current, beatToTime (edit, c));

                if (right == nullptr)
                    break;

                for (auto* half : { current, right })
                    if (auto* audio = dynamic_cast<te::AudioClipBase*> (half))
                        ensureEdgeFades (*audio);

                current = right;
            }
        }
    }

    // Then put each region where its piece goes. Where they go is worked out
    // first, from the regions as they are now, so moving one doesn't affect
    // where the next one lands. (Tracktion doesn't mind regions overlapping
    // on the way: when it's done, none do.)
    struct Placement { te::Clip::Ptr clip; double startBeat; std::vector<const Piece*> pieces; };
    std::vector<Placement> placements;

    for (auto* track : te::getAudioTracks (edit))
        for (auto* clip : track->getClips())
        {
            const double start = clip->getStartBeat().inBeats();
            const double middle = (start + clip->getEndBeat().inBeats()) / 2.0;
            placements.push_back ({ clip, start, piecesAt (middle) });
        }

    for (auto& p : placements)
    {
        if (p.pieces.empty())
        {
            p.clip->removeFromParent();
            continue;
        }

        // Copies first, while the original is still where they're copied
        // from. A flexed region keeps its hits on their beats where it lands
        // (the tempo can differ there, see FlexHelpers.h).
        for (size_t i = 1; i < p.pieces.size(); ++i)
            if (auto copy = te::duplicateClip (*p.clip))
                moveKeepingFlex (*copy, [&] { copy->setStart (beatToTime (edit, p.startBeat + p.pieces[i]->dest - p.pieces[i]->from), false, true); });

        if (const double shift = p.pieces[0]->dest - p.pieces[0]->from; std::abs (shift) > tolerance)
            moveKeepingFlex (*p.clip, [&] { p.clip->setStart (beatToTime (edit, p.startBeat + shift), false, true); });   // keeps its length
    }

    // 2. Automation, on every plug-in (track volume and pan included, and the
    // master's). The curves are read and written as their stored points, so
    // this also covers curves of plug-ins that are missing.
    for (auto* plugin : te::getAllPlugins (edit, true))
    {
        for (auto curve : plugin->state)
        {
            if (! curve.hasType (te::IDs::AUTOMATIONCURVE) || curve.getNumChildren() < 2)
                continue;

            std::vector<Point> points;

            for (auto pt : curve)
            {
                const double beat = timeToBeat (edit, te::TimePosition::fromSeconds ((double) pt.getProperty (te::IDs::t)));
                points.push_back ({ beat, (float) pt.getProperty (te::IDs::v), (float) pt.getProperty (te::IDs::c), beat });
            }

            // A flat curve is the same everywhere: nothing to rearrange.
            if (std::all_of (points.begin(), points.end(), [&] (const Point& p) { return p.value == points.front().value; }))
                continue;

            pinCuts (points, cuts);

            std::vector<Point> placed;

            for (auto& p : points)
                for (auto* piece : piecesAt (p.side))
                {
                    auto moved = p;
                    moved.beat += piece->dest - piece->from;
                    moved.side += piece->dest - piece->from;
                    placed.push_back (moved);
                }

            // In time order. Sorting by side keeps a left pin before the right
            // one on its beat (their beats can differ by a rounding error).
            std::stable_sort (placed.begin(), placed.end(), [] (const Point& a, const Point& b) { return a.side < b.side; });

            writePoints (edit, curve, dropUnneededPins (placed));
        }
    }
}

} // namespace ikaros
