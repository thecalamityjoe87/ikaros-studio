#pragma once

// Flex Time's maths, with no Tracktion in it, so the audio editor can use it
// too (it previews a drag before the session hears about it).
//
// A flexed region plays its recording through a list of warp markers. Each
// one pins a moment of the recording (its time in the file) to a moment of
// the region's content (its time after warping). Between two markers the
// audio is stretched or squashed evenly to fit. The first marker is always
// the start of the recording and the last its end; the ones in between are
// the hits the user dragged (and the neighbours that hold the rest still).

#include <juce_core/juce_core.h>
#include <algorithm>
#include <vector>

namespace ikaros
{

/** One warp marker, in seconds: where a moment of the recording is in its
    file (source), and where it plays in the region's content (warp). */
struct WarpPoint
{
    double source = 0.0;
    double warp = 0.0;

    // A tempo marker: one the engine adds where the tempo changes inside
    // the region, so the audio either side stays on its beats (see
    // FlexHelpers.h). It isn't shown, dragged or removed like the others.
    bool tempoMarker = false;

    bool operator== (const WarpPoint&) const = default;
};

/** A flex marker as the audio editor shows it: the moment of the recording
    (seconds into its file) and where it plays on the timeline (in beats). */
struct FlexMarker
{
    double sourceSeconds = 0.0;
    double beat = 0.0;
    bool tempoMarker = false;   // see WarpPoint::tempoMarker

    bool operator== (const FlexMarker&) const = default;
};

namespace flexmap
{
    /** Two markers closer than this (in seconds of the file) are the same one. */
    inline constexpr double sameMarkerSeconds = 0.001;

    /** The most a stretch between two markers can squash the audio: a part
        never plays in less than a tenth of its own length (as Tracktion's
        own warp editing allows). Markers can't be dragged past their
        neighbours either way. */
    inline constexpr double minStretch = 0.1;

    /** Where a moment of the content (warp seconds) comes from in the
        file. Markers are sorted, first to last. Beyond the first and last
        markers the audio plays at its own speed. */
    inline double toSource (const std::vector<WarpPoint>& markers, double warp)
    {
        if (markers.empty())
            return warp;

        if (warp <= markers.front().warp)
            return markers.front().source + (warp - markers.front().warp);

        if (warp >= markers.back().warp)
            return markers.back().source + (warp - markers.back().warp);

        // std::upper_bound: the first marker after `warp` (they're sorted).
        auto next = std::upper_bound (markers.begin(), markers.end(), warp,
                                      [] (double w, const WarpPoint& m) { return w < m.warp; });
        auto prev = next - 1;
        const double span = next->warp - prev->warp;

        if (span <= 0.0)
            return prev->source;

        return prev->source + (warp - prev->warp) / span * (next->source - prev->source);
    }

    /** Where a moment of the file (source seconds) plays in the content. */
    inline double toWarp (const std::vector<WarpPoint>& markers, double source)
    {
        if (markers.empty())
            return source;

        if (source <= markers.front().source)
            return markers.front().warp + (source - markers.front().source);

        if (source >= markers.back().source)
            return markers.back().warp + (source - markers.back().source);

        auto next = std::upper_bound (markers.begin(), markers.end(), source,
                                      [] (double s, const WarpPoint& m) { return s < m.source; });
        auto prev = next - 1;
        const double span = next->source - prev->source;

        if (span <= 0.0)
            return prev->warp;

        return prev->warp + (source - prev->source) / span * (next->warp - prev->warp);
    }

    /** The index of the marker at `source` (within sameMarkerSeconds), or -1. */
    inline int indexOf (const std::vector<WarpPoint>& markers, double source)
    {
        for (size_t i = 0; i < markers.size(); ++i)
            if (std::abs (markers[i].source - source) < sameMarkerSeconds)
                return (int) i;

        return -1;
    }

    /** Adds a marker at `source` where it plays now (so adding it changes
        nothing you hear), keeping the list sorted. Returns its index. */
    inline int insertAtCurrentPlace (std::vector<WarpPoint>& markers, double source)
    {
        if (auto existing = indexOf (markers, source); existing >= 0)
            return existing;

        const WarpPoint m { source, toWarp (markers, source) };
        auto it = std::upper_bound (markers.begin(), markers.end(), source,
                                    [] (double s, const WarpPoint& p) { return s < p.source; });
        // Two steps: inserting can move the vector's storage, so begin()
        // must be asked for after it (C++ doesn't say which side of a `-`
        // is worked out first).
        const auto inserted = markers.insert (it, m);
        return (int) (inserted - markers.begin());
    }

    /** Grabbing a hit (a transient) to drag it: the markers it needs. One on
        the hit itself, and one on each neighbouring hit (unless a marker is
        already nearer), so only the audio between the neighbours stretches
        and the rest of the region stays where it is, as in GarageBand.
        `transients` are sorted source times. Returns the dragged marker's index. */
    inline int prepareDrag (std::vector<WarpPoint>& markers, const std::vector<double>& transients, double source)
    {
        if (indexOf (markers, source) < 0)
        {
            // The hits either side, if they're inside the markers either side.
            auto after = std::upper_bound (transients.begin(), transients.end(), source + sameMarkerSeconds);
            auto before = std::lower_bound (transients.begin(), transients.end(), source - sameMarkerSeconds);
            const int at = insertAtCurrentPlace (markers, source);
            const double prevMarker = markers[(size_t) juce::jmax (0, at - 1)].source;
            const double nextMarker = markers[(size_t) juce::jmin ((int) markers.size() - 1, at + 1)].source;
            std::vector<double> anchors;

            if (before != transients.begin() && *(before - 1) > prevMarker + sameMarkerSeconds)
                anchors.push_back (*(before - 1));

            if (after != transients.end() && *after < nextMarker - sameMarkerSeconds)
                anchors.push_back (*after);

            for (auto a : anchors)
                insertAtCurrentPlace (markers, a);
        }

        return indexOf (markers, source);
    }

    /** The nearest place marker `index` can go to `wantedWarp`: between
        its neighbours, never squashing either side more than minStretch
        allows. The first and last markers stay put. Warp times are in
        seconds, unless warpUnitsPerSecond says otherwise (the audio editor
        previews a drag in beats). */
    inline double limitMove (const std::vector<WarpPoint>& markers, int index, double wantedWarp,
                             double warpUnitsPerSecond = 1.0)
    {
        if (index <= 0 || index >= (int) markers.size() - 1)
            return markers.empty() ? wantedWarp : markers[(size_t) juce::jlimit (0, (int) markers.size() - 1, index)].warp;

        const auto& prev = markers[(size_t) index - 1];
        const auto& here = markers[(size_t) index];
        const auto& next = markers[(size_t) index + 1];
        const double lowest = prev.warp + (here.source - prev.source) * minStretch * warpUnitsPerSecond;
        const double highest = next.warp - (next.source - here.source) * minStretch * warpUnitsPerSecond;

        return lowest <= highest ? juce::jlimit (lowest, highest, wantedWarp) : here.warp;
    }

    /** True if the part between markers i - 1 and i can be played: the
        second is later in the file and in the content, and the part isn't
        squashed more than minStretch allows. */
    inline bool partFits (const std::vector<WarpPoint>& markers, size_t i)
    {
        const double source = markers[i].source - markers[i - 1].source;
        const double warp = markers[i].warp - markers[i - 1].warp;
        return source > 0.0 && warp >= source * minStretch - 1.0e-9;
    }

    /** True if every part between the markers can be played (see partFits). */
    inline bool isPlayable (const std::vector<WarpPoint>& markers)
    {
        for (size_t i = 1; i < markers.size(); ++i)
            if (! partFits (markers, i))
                return false;

        return true;
    }

    /** Moves several markers at once (Quantize): `moves` pairs a marker's
        index with the warp time wanted for it, sorted by index. Each goes
        as near there as its neighbours allow (as limitMove does for one),
        judged by where they're going rather than where they are, so markers
        moving together don't hold each other back. One that still can't fit
        (it would cross a marker that stays put) goes back where it was. The
        first and last markers stay put. */
    inline void moveMarkers (std::vector<WarpPoint>& markers, const std::vector<std::pair<int, double>>& moves)
    {
        const auto before = markers;
        std::vector<int> moved;

        // Everything where it's wanted first, then first to last, each one
        // between where the one before ended up and where the next is going.
        for (auto [index, warp] : moves)
        {
            if (index > 0 && index < (int) markers.size() - 1)
            {
                markers[(size_t) index].warp = warp;
                moved.push_back (index);
            }
        }

        for (auto index : moved)
            markers[(size_t) index].warp = limitMove (markers, index, markers[(size_t) index].warp);

        // Put back the moved markers next to a part that still doesn't fit,
        // one at a time, until everything fits. (With every moved marker
        // back, it's the list it was, which did.)
        while (! moved.empty() && ! isPlayable (markers))
        {
            size_t bad = 1;

            while (partFits (markers, bad))
                ++bad;

            auto it = std::find (moved.begin(), moved.end(), (int) bad);

            if (it == moved.end())
                it = std::find (moved.begin(), moved.end(), (int) bad - 1);

            if (it == moved.end())
                break;   // neither end was moved: the markers were like that already

            markers[(size_t) *it].warp = before[(size_t) *it].warp;
            moved.erase (it);
        }

        if (! isPlayable (markers) && isPlayable (before))
            markers = before;
    }

    /** True if any marker plays somewhere other than its own place in the
        recording: the region sounds different from the plain recording. */
    inline bool isWarped (const std::vector<WarpPoint>& markers)
    {
        return std::any_of (markers.begin(), markers.end(),
                            [] (const WarpPoint& m) { return std::abs (m.warp - m.source) > sameMarkerSeconds; });
    }
}

} // namespace ikaros
