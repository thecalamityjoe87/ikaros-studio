#include "engine/DrummerEngine.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace ikaros::drummer
{

namespace
{
    constexpr int ticksPerBeat = GrooveLibrary::ticksPerBeat;
    constexpr int barTicks = 4 * ticksPerBeat;
    constexpr int sixteenth = ticksPerBeat / 4;

    //==========================================================================
    // Random numbers that are the same on every computer and compiler (the
    // standard library's distributions aren't). splitmix64: a well-known,
    // tiny generator. Each decision gets its own, seeded from the variation
    // seed, the bar it's for and what it decides, so one decision never
    // shifts another: a region plays bar 12 the same way whether it starts
    // at bar 0 or bar 8, and turning a piece off changes nothing else.
    enum class Purpose : std::uint64_t
    {
        phraseLength = 1, groove, nextPhrase, extraFill, fillLength, fillChoice,
        complexity, timekeeping, sectionRide, crashAfterFill
    };

    class Random
    {
    public:
        Random (int seed, int bar, Purpose purpose)
            : state ((std::uint64_t) (std::uint32_t) seed * 0x9E3779B97F4A7C15ull
                     ^ ((std::uint64_t) (std::uint32_t) (bar + 4096) << 24)
                     ^ (std::uint64_t) purpose * 0xD1B54A32D192ED03ull)
        {
            next();
        }

        std::uint64_t next()
        {
            auto z = (state += 0x9E3779B97F4A7C15ull);
            z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
            z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
            return z ^ (z >> 31);
        }

        /** 0 (included) to 1 (not included). */
        double uniform()            { return (double) (next() >> 11) * (1.0 / 9007199254740992.0); }
        bool chance (double p)      { return uniform() < p; }

    private:
        std::uint64_t state;
    };

    double square (double x)        { return x * x; }

    bool isTimekeeper (DrumPiece p)
    {
        return p == DrumPiece::hihatClosed || p == DrumPiece::hihatOpen || p == DrumPiece::ride || p == DrumPiece::rideBell;
    }

    /** The nearest 16th note to a tick, counting from the bar's start (0..16). */
    int sixteenthOf (int tick)      { return (int) std::floor ((tick + sixteenth / 2.0) / sixteenth); }

    /** A note while a bar is being put together: its tick is from the
        bar's start. */
    struct Note
    {
        DrumPiece piece;
        int tick;
        int velocity;
    };

    //==========================================================================
    // Choosing grooves

    /** A run of bars from one performance. */
    struct GrooveChoice
    {
        int performance = -1;
        int start = 0;
        bool valid() const noexcept     { return performance >= 0; }
    };

    /** Running totals of each performance's scores, so a run's average is
        two subtractions. Worked out once per generate() call. */
    struct ScoreSums
    {
        std::vector<std::vector<double>> complexity, loudness;

        explicit ScoreSums (const GrooveLibrary::Drummer& d)
        {
            for (auto& perf : d.beats)
            {
                std::vector<double> c (1, 0.0), l (1, 0.0);

                for (auto& bar : perf.bars)
                {
                    c.push_back (c.back() + bar.complexity);
                    l.push_back (l.back() + bar.loudness);
                }

                complexity.push_back (std::move (c));
                loudness.push_back (std::move (l));
            }
        }
    };

    /** How far a choice is from what's wanted: complexity and loudness (in
        units of how far apart grooves feel), the tempo (a performance
        played 30% faster or slower counts as much as a clearly different
        groove), and a run too short for the phrase. */
    double distanceOf (double complexity, double loudness, double bpm, double songBpm, bool tooShort,
                       const DrummerSettings& s)
    {
        return square ((complexity - s.complexity) / 0.12)
             + square ((loudness - s.loudness) / 0.18)
             + square (std::log (bpm / songBpm) / std::log (1.3))
             + (tooShort ? 1.5 : 0.0);
    }

    /** Keeps the eight closest candidates seen, then picks one of them, the
        closer the likelier. On a tie the one seen first stays ahead, so
        every computer picks the same. */
    template <typename Candidate>
    class Nearest
    {
    public:
        void add (double distance, const Candidate& candidate)
        {
            if (count == best.size() && distance >= best.back().first)
                return;

            if (count < best.size())
                ++count;

            // Insertion: slide the farther ones down to make room.
            auto pos = count - 1;

            while (pos > 0 && best[pos - 1].first > distance)
            {
                best[pos] = best[pos - 1];
                --pos;
            }

            best[pos] = { distance, candidate };
        }

        bool empty() const noexcept     { return count == 0; }

        const Candidate& pick (Random& random) const
        {
            std::array<double, 8> weights {};
            double total = 0.0;

            for (size_t i = 0; i < count; ++i)
                total += weights[i] = std::exp (-(best[i].first - best[0].first));

            auto r = random.uniform() * total;

            for (size_t i = 0; i < count; ++i)
            {
                r -= weights[i];

                if (r < 0.0)
                    return best[i].second;
            }

            return best[count - 1].second;
        }

    private:
        std::array<std::pair<double, Candidate>, 8> best {};
        size_t count = 0;
    };

    GrooveChoice chooseGroove (const GrooveLibrary::Drummer& d, const ScoreSums& sums, const DrummerSettings& s,
                               int phraseBars, double songBpm, Random& random)
    {
        Nearest<GrooveChoice> candidates;

        for (int p = 0; p < (int) d.beats.size(); ++p)
        {
            const int length = (int) d.beats[(size_t) p].bars.size();
            const bool tooShort = length < phraseBars;
            const int lastStart = tooShort ? 0 : length - phraseBars;
            const int run = std::min (length, phraseBars);
            auto& c = sums.complexity[(size_t) p];
            auto& l = sums.loudness[(size_t) p];

            for (int start = 0; start <= lastStart; ++start)
                candidates.add (distanceOf ((c[(size_t) (start + run)] - c[(size_t) start]) / run,
                                            (l[(size_t) (start + run)] - l[(size_t) start]) / run,
                                            d.beats[(size_t) p].bpm, songBpm, tooShort, s),
                                GrooveChoice { p, start });
        }

        if (candidates.empty())
            return {};

        return candidates.pick (random);
    }

    const GrooveLibrary::Fill* chooseFill (const GrooveLibrary::Drummer& d, const DrummerSettings& s, int beats,
                                           double songBpm, Random& random)
    {
        Nearest<const GrooveLibrary::Fill*> candidates;

        for (auto& f : d.fills)
            if (f.beats == beats)
                candidates.add (distanceOf (f.complexity, f.loudness, f.bpm, songBpm, false, s), &f);

        if (candidates.empty())
            return nullptr;

        return candidates.pick (random);
    }

    //==========================================================================
    // Shaping a bar

    /** Complexity between grooves: below the groove's own score, ghost notes
        and in-between hi-hats go (then off-beat 8th hats and kicks); above
        it, hats fill in the 16ths between them and ghost notes join the
        snare. Each choice is made once per section, for a slot on the grid
        and a piece (not afresh in every bar), so every bar of the section
        changes the same way and the groove stays a groove. */
    void adjustComplexity (std::vector<Note>& notes, double residual, int seed, int sectionStart)
    {
        // The same number for the same slot (and piece) all through the section.
        auto draw = [seed, sectionStart] (int key)
        {
            return Random (seed, sectionStart * 512 + key, Purpose::complexity).uniform();
        };

        if (residual < 0.0)
        {
            const auto amount = std::min (1.0, -residual * 2.5);
            int loudestSnare = 0;

            for (auto& n : notes)
                if (n.piece == DrumPiece::snare)
                    loudestSnare = std::max (loudestSnare, n.velocity);

            std::vector<Note> kept;

            for (auto& n : notes)
            {
                const int slot = sixteenthOf (n.tick) % 16;
                const auto chance = draw (slot * numDrumPieces + (int) n.piece);
                const bool offSixteenth = slot % 2 == 1, offEighth = slot % 4 == 2;
                bool remove = false;

                if (n.piece == DrumPiece::snare && n.velocity < loudestSnare / 2)
                    remove = chance < amount;                             // a ghost note
                else if (isTimekeeper (n.piece) && offSixteenth)
                    remove = chance < amount;                             // a hat between the 8ths
                else if (isTimekeeper (n.piece) && offEighth)
                    remove = chance < amount - 0.6;                       // down to quarters
                else if (n.piece == DrumPiece::kick && offSixteenth)
                    remove = chance < amount - 0.5;                       // a syncopated kick

                if (! remove)
                    kept.push_back (n);
            }

            notes = std::move (kept);
            return;
        }

        const auto amount = std::min (1.0, residual * 2.5);

        // The piece keeping time (the most-played of the hats and ride).
        std::array<int, numDrumPieces> counts {};

        for (auto& n : notes)
            if (isTimekeeper (n.piece))
                ++counts[(size_t) n.piece];

        const auto keeper = (DrumPiece) (std::max_element (counts.begin(), counts.end()) - counts.begin());
        std::vector<Note> added;

        auto noteAt = [&notes] (int slot, auto&& matches) -> const Note*
        {
            for (auto& n : notes)
                if (sixteenthOf (n.tick) == slot && matches (n.piece))
                    return &n;

            return nullptr;
        };

        for (int slot = 1; slot < 16; slot += 2)
        {
            if (counts[(size_t) keeper] == 0 || draw (300 + slot) >= amount * 0.8)
                continue;

            auto isKeeper = [keeper] (DrumPiece p) { return p == keeper; };
            auto* before = noteAt (slot - 1, isKeeper);
            auto* after = noteAt (slot + 1, isKeeper);

            if (before != nullptr && after != nullptr && noteAt (slot, isKeeper) == nullptr)
            {
                // Halfway between its neighbours, as late or early as they are.
                const int drift = ((before->tick - (slot - 1) * sixteenth) + (after->tick - (slot + 1) * sixteenth)) / 2;
                added.push_back ({ keeper, slot * sixteenth + drift, std::max (1, (before->velocity + after->velocity) * 3 / 10) });
            }
        }

        for (int slot = 1; slot < 16; slot += 2)
        {
            if (draw (340 + slot) >= amount * 0.3)
                continue;

            auto isSnare = [] (DrumPiece p) { return p == DrumPiece::snare; };
            const auto feel = draw (380 + slot);

            // Not right next to another snare hit (a backbeat, say).
            if (noteAt (slot, isSnare) == nullptr && noteAt (slot - 1, isSnare) == nullptr && noteAt (slot + 1, isSnare) == nullptr)
                added.push_back ({ DrumPiece::snare, slot * sixteenth + (int) std::lround ((feel - 0.5) * 16.0),
                                   18 + (int) std::lround (feel * 20.0) });
        }

        notes.insert (notes.end(), added.begin(), added.end());
    }

    /** What keeps time once the loudness and the Hi-hat/Ride choice have
        had their say. `keep` = whatever the groove itself plays. */
    enum class Keeper { keep, hihat, ride };

    void applyTimekeeping (std::vector<Note>& notes, Keeper keeper, double loudAmount, Random& random)
    {
        for (auto& n : notes)
        {
            const auto draw = random.uniform();
            const int slot = sixteenthOf (n.tick);

            if (keeper == Keeper::hihat)
            {
                if (n.piece == DrumPiece::ride || n.piece == DrumPiece::rideBell)
                    n.piece = DrumPiece::hihatClosed;

                // Loud: the hats open up, on the 8ths.
                if (n.piece == DrumPiece::hihatClosed && slot % 2 == 0 && draw < loudAmount * 0.6)
                    n.piece = DrumPiece::hihatOpen;
            }
            else if (keeper == Keeper::ride)
            {
                if (n.piece == DrumPiece::hihatClosed || n.piece == DrumPiece::hihatOpen)
                    n.piece = DrumPiece::ride;

                // Loud: the bell on some of the beats.
                if (n.piece == DrumPiece::ride && slot % 4 == 0 && draw < loudAmount * 0.35)
                    n.piece = DrumPiece::rideBell;
            }
        }
    }

    /** A crash (with a kick under it) on the bar's first beat, instead of
        the time-keeping there. */
    void addCrash (std::vector<Note>& notes)
    {
        notes.erase (std::remove_if (notes.begin(), notes.end(),
                                     [] (const Note& n) { return (isTimekeeper (n.piece) || n.piece == DrumPiece::crash)
                                                                    && std::abs (n.tick) < sixteenth / 2; }),
                     notes.end());

        const bool kickThere = std::any_of (notes.begin(), notes.end(),
                                            [] (const Note& n) { return n.piece == DrumPiece::kick && std::abs (n.tick) < sixteenth / 2; });

        notes.push_back ({ DrumPiece::crash, 0, 112 });

        if (! kickThere)
            notes.push_back ({ DrumPiece::kick, 0, 100 });
    }

    /** Swing: off-beat notes of the subdivision move later by up to a third
        of it (a triplet feel at 1), keeping how early or late the drummer
        played them. */
    int swingTick (int tick, float swing, int subdivisionTicks)
    {
        const int grid = (int) std::floor ((tick + subdivisionTicks / 2.0) / subdivisionTicks);

        if (grid % 2 == 0)
            return tick;

        return tick + (int) std::lround (swing * subdivisionTicks / 3.0);
    }

    //==========================================================================
    // The plan: what each bar plays.
    struct PlannedBar
    {
        const DrumBar* bar = nullptr;
        const GrooveLibrary::Bar* groove = nullptr;   // nullptr: silent (not 4/4)
        float grooveComplexity = 0.0f;
        const GrooveLibrary::Fill* fill = nullptr;    // played over the end of the bar
        bool crash = false;
        int sectionStart = 0;                         // the bar its section started on
    };
}

//==============================================================================
int shapeVelocity (int velocity, float loudness)
{
    // A power curve keeps the drummer's dynamics in order: soft playing
    // pulls the quieter notes down further than the accents, loud playing
    // lifts them. Below the middle the whole kit gets a little quieter too.
    const auto x = juce::jlimit (0.0, 1.0, velocity / 127.0);
    const auto gamma = std::pow (2.0, (0.5 - (double) loudness) * 1.6);
    const auto scale = std::min (1.0, 0.8 + 0.4 * (double) loudness);
    return juce::jlimit (1, 127, (int) std::lround (scale * std::pow (x, gamma) * 127.0));
}

DrumPart generate (const GrooveLibrary& library, const DrummerSettings& settings, const DrumContext& context)
{
    DrumPart part;
    const auto* drummer = library.findDrummerOrFirst (settings.drummer);

    if (drummer == nullptr || context.bars.empty())
        return part;

    const ScoreSums sums (*drummer);
    const int seed = settings.seed;
    const bool loud = settings.loudness > loudTimekeeping;
    const double loudAmount = loud ? (settings.loudness - loudTimekeeping) / (1.0 - loudTimekeeping) : 0.0;

    auto isBoundary = [&context] (int bar)
    {
        return std::find (context.boundaries.begin(), context.boundaries.end(), bar) != context.boundaries.end();
    };

    // The bars, cut into sections at the boundaries.
    std::vector<PlannedBar> plan;

    for (auto& b : context.bars)
        plan.push_back ({ &b });

    for (size_t first = 0; first < plan.size();)
    {
        size_t end = first + 1;

        while (end < plan.size() && ! isBoundary (plan[end].bar->bar))
            ++end;

        const int sectionStart = plan[first].bar->bar;
        std::vector<PlannedBar*> playable;

        for (auto i = first; i < end; ++i)
        {
            plan[i].sectionStart = sectionStart;

            if (plan[i].bar->isFourFour())
                playable.push_back (&plan[i]);
        }

        // Phrases of four or eight playable bars, each a run of one
        // performance's bars.
        const int phraseLength = Random (seed, sectionStart, Purpose::phraseLength).chance (0.5) ? 8 : 4;
        GrooveChoice previous;

        for (size_t k = 0; k < playable.size(); k += (size_t) phraseLength)
        {
            const auto bars = (int) std::min ((size_t) phraseLength, playable.size() - k);
            const auto songBpm = playable[k]->bar->bpm;
            Random random (seed, playable[k]->bar->bar, Purpose::groove);
            GrooveChoice choice;

            if (previous.valid())
            {
                // Carry on with the performance, play the phrase again, or
                // (now and then) move to another groove.
                const auto r = Random (seed, playable[k]->bar->bar, Purpose::nextPhrase).uniform();
                const auto& perf = drummer->beats[(size_t) previous.performance];

                if (r < 0.5 && previous.start + phraseLength * 2 <= (int) perf.bars.size())
                    choice = { previous.performance, previous.start + phraseLength };
                else if (r < 0.85)
                    choice = previous;
            }

            // Chosen for a whole phrase even when the region ends partway
            // through it, so every region (and both halves of a split one)
            // plays a bar the same way.
            if (! choice.valid())
                choice = chooseGroove (*drummer, sums, settings, phraseLength, songBpm, random);

            if (! choice.valid())
                break;

            const auto& perf = drummer->beats[(size_t) choice.performance];

            for (int i = 0; i < bars; ++i)
            {
                // A run shorter than the phrase goes round again.
                const auto& groove = perf.bars[(size_t) (choice.start + i) % perf.bars.size()];
                playable[k + (size_t) i]->groove = &groove;
                playable[k + (size_t) i]->grooveComplexity = groove.complexity;
            }

            previous = choice;
        }

        // Fills: before the next section, and (with Fills up) extra ones at
        // the end of each group of four bars.
        const bool endsAtBoundary = end < plan.size();

        for (size_t i = 0; i < playable.size(); ++i)
        {
            auto& p = *playable[i];
            const bool beforeBoundary = endsAtBoundary && i == playable.size() - 1
                                          && p.bar->bar == plan[end].bar->bar - 1;
            const bool extra = ! beforeBoundary && (i + 1) % 4 == 0 && i + 1 < playable.size()
                                 && Random (seed, p.bar->bar, Purpose::extraFill).chance (settings.fills);

            if (! beforeBoundary && ! extra)
                continue;

            const int beats = Random (seed, p.bar->bar, Purpose::fillLength).chance (settings.fills) ? 4 : 2;
            Random choice (seed, p.bar->bar, Purpose::fillChoice);
            p.fill = chooseFill (*drummer, settings, beats, p.bar->bpm, choice);

            if (p.fill == nullptr)
                continue;

            // A crash after it: always going into the next section, and
            // often after a whole bar of fill.
            if (beforeBoundary)
            {
                if (plan[end].bar->isFourFour())
                    plan[end].crash = true;
            }
            else if (beats == 4 && Random (seed, p.bar->bar, Purpose::crashAfterFill).chance (0.5))
            {
                playable[i + 1]->crash = true;
            }
        }

        // Loud: every section starts with a crash.
        if (loud && isBoundary (sectionStart) && plan[first].bar->isFourFour())
            plan[first].crash = true;

        first = end;
    }

    // The notes, bar by bar, for the bars the region covers.
    for (auto& p : plan)
    {
        const auto& bar = *p.bar;
        const double barEnd = bar.startBeat + 4.0;

        if (barEnd <= context.fromBeat || bar.startBeat >= context.toBeat)
            continue;

        if (p.groove == nullptr)
        {
            if (! bar.isFourFour())
                part.silentBars.push_back (bar.bar);

            continue;
        }

        std::vector<Note> notes;

        for (auto& n : p.groove->notes)
            notes.push_back ({ n.piece, n.tick, n.velocity });

        adjustComplexity (notes, settings.complexity - p.grooveComplexity, seed, p.sectionStart);

        if (p.fill != nullptr)
        {
            // A whole-bar fill replaces the bar; a half-bar one its second half.
            const int fillStart = barTicks - p.fill->beats * ticksPerBeat;
            notes.erase (std::remove_if (notes.begin(), notes.end(),
                                         [fillStart] (const Note& n) { return n.tick >= fillStart - sixteenth / 2; }),
                         notes.end());

            for (auto& n : p.fill->notes)
                notes.push_back ({ n.piece, fillStart + n.tick, n.velocity });

            part.fillBars.push_back (bar.bar);
        }

        // Hi-hat or ride: the user's choice, or by loudness.
        Keeper keeper = settings.timekeeping == Timekeeping::hihat ? Keeper::hihat
                      : settings.timekeeping == Timekeeping::ride  ? Keeper::ride
                                                                   : Keeper::keep;

        if (settings.timekeeping == Timekeeping::automatic && loud)
            keeper = Random (seed, p.sectionStart, Purpose::sectionRide).chance (loudAmount) ? Keeper::ride : Keeper::hihat;

        Random timekeepingRandom (seed, bar.bar, Purpose::timekeeping);
        applyTimekeeping (notes, keeper, loudAmount, timekeepingRandom);

        if (p.crash)
        {
            addCrash (notes);
            part.crashBars.push_back (bar.bar);
        }

        // Last, the steps that make no random choices: velocity, swing and
        // the piece toggles. So changing them changes nothing else.
        const int subdivision = drummer->swingSubdivision == 16 ? sixteenth : ticksPerBeat / 2;

        for (auto& n : notes)
        {
            if (! settings.plays (n.piece))
                continue;

            // Which bar (and region) a note belongs to goes by where it was
            // meant to be: a downbeat played a hair early is still the
            // downbeat, and stays in its region.
            const double nominal = bar.startBeat + std::max (0, n.tick) / (double) ticksPerBeat;

            if (nominal < context.fromBeat || nominal >= context.toBeat)
                continue;

            const auto tick = swingTick (n.tick, settings.swing, subdivision);
            const auto beat = std::max (context.fromBeat, bar.startBeat + tick / (double) ticksPerBeat);
            part.hits.push_back ({ n.piece, beat, shapeVelocity (n.velocity, settings.loudness) });
        }
    }

    // In time order (and the same order on every computer).
    std::stable_sort (part.hits.begin(), part.hits.end(), [] (const DrumHit& a, const DrumHit& b)
    {
        return a.beat != b.beat ? a.beat < b.beat : a.piece < b.piece;
    });

    return part;
}

} // namespace ikaros::drummer
