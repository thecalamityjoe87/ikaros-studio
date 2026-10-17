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

    bool operator== (const WarpPoint&) const = default;
};

/** A flex marker as the audio editor shows it: the moment of the recording
    (seconds into its file) and where it plays on the timeline (in beats). */
struct FlexMarker
{
    double sourceSeconds = 0.0;
    double beat = 0.0;

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

    /** True if any marker plays somewhere other than its own place in the
        recording: the region sounds different from the plain recording. */
    inline bool isWarped (const std::vector<WarpPoint>& markers)
    {
        return std::any_of (markers.begin(), markers.end(),
                            [] (const WarpPoint& m) { return std::abs (m.warp - m.source) > sameMarkerSeconds; });
    }
}

} // namespace ikaros
