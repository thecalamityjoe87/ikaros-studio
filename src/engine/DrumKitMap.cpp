#include "engine/DrumKitMap.h"
#include <algorithm>
#include <set>

namespace ikaros::drumkit
{

namespace
{
    //==========================================================================
    // Reading an SFZ program

    /** A key number from an SFZ value: "36", or a note name like "c2" or
        "f#3" (c4 is 60, as sfizz reads it). -1 if it's neither. */
    int noteNumberOf (const juce::String& value)
    {
        const auto v = value.trim().toLowerCase();

        if (v.isEmpty())
            return -1;

        if (v.containsOnly ("-0123456789"))
            return v.getIntValue();

        static const juce::String letters ("cdefgab");
        static constexpr int semitones[] { 0, 2, 4, 5, 7, 9, 11 };
        const int letter = letters.indexOfChar (v[0]);

        if (letter < 0)
            return -1;

        int note = semitones[letter], i = 1;

        if (v[i] == '#')      { ++note; ++i; }
        else if (v[i] == 'b') { --note; ++i; }

        const auto octave = v.substring (i);

        if (octave.isEmpty() || ! octave.containsOnly ("-0123456789"))
            return -1;

        return note + (octave.getIntValue() + 1) * 12;
    }

    /** The opcodes in a header's text ("key=36 sample=Kick 01.wav"): a
        value runs up to the next opcode, so it may contain spaces. */
    std::map<juce::String, juce::String> opcodesOf (const juce::String& body)
    {
        std::map<juce::String, juce::String> opcodes;

        // Where each "name=" starts, and where its "=" is.
        struct Start { int name, equals; };
        std::vector<Start> starts;

        for (int i = body.indexOfChar ('='); i > 0; i = body.indexOfChar (i + 1, '='))
        {
            int s = i;

            while (s > 0 && (juce::CharacterFunctions::isLetterOrDigit (body[s - 1]) || body[s - 1] == '_'))
                --s;

            if (s < i && (s == 0 || juce::CharacterFunctions::isWhitespace (body[s - 1])))
                starts.push_back ({ s, i });
        }

        for (size_t j = 0; j < starts.size(); ++j)
        {
            const int end = j + 1 < starts.size() ? starts[j + 1].name : body.length();
            opcodes[body.substring (starts[j].name, starts[j].equals)] = body.substring (starts[j].equals + 1, end).trim();
        }

        return opcodes;
    }

    struct SfzReader
    {
        juce::File folder;   // #include paths are relative to the program's own folder (as in sfizz)
        std::map<juce::String, juce::String> defines;
        std::vector<juce::String> defineNamesBySize;   // longest first, for replacing
        KeyNames names;
        juce::String text;   // everything but the #lines, for the headers

        void read (const juce::File& file, int depth)
        {
            if (depth > 12 || ! file.existsAsFile())
                return;

            auto content = file.loadFileAsString();

            // /* block comments */
            for (int start = content.indexOf ("/*"); start >= 0; start = content.indexOf (start, "/*"))
            {
                const int end = content.indexOf (start + 2, "*/");
                content = content.substring (0, start) + " " + (end < 0 ? juce::String() : content.substring (end + 2));
            }

            for (auto line : juce::StringArray::fromLines (content))
            {
                line = line.upToFirstOccurrenceOf ("//", false, false);
                const auto trimmed = line.trim();

                if (trimmed.startsWith ("#define"))
                {
                    auto parts = juce::StringArray::fromTokens (trimmed, " \t", {});
                    parts.removeEmptyStrings();

                    if (parts.size() >= 3)
                    {
                        if (defines.count (parts[1]) == 0)
                            defineNamesBySize.push_back (parts[1]);

                        defines[parts[1]] = parts[2];
                        std::stable_sort (defineNamesBySize.begin(), defineNamesBySize.end(),
                                          [] (const juce::String& a, const juce::String& b) { return a.length() > b.length(); });
                    }

                    continue;
                }

                // A key given by a $define: its name is one of the key's names.
                for (const char* opcode : { "key=$", "lokey=$", "hikey=$" })
                {
                    for (int i = line.indexOf (opcode); i >= 0; i = line.indexOf (i + 1, opcode))
                    {
                        const int nameStart = line.indexOfChar (i, '$');
                        int nameEnd = nameStart + 1;

                        while (nameEnd < line.length() && (juce::CharacterFunctions::isLetterOrDigit (line[nameEnd]) || line[nameEnd] == '_'))
                            ++nameEnd;

                        const auto name = line.substring (nameStart, nameEnd);

                        if (auto found = defines.find (name); found != defines.end())
                            if (const int key = noteNumberOf (found->second); juce::isPositiveAndBelow (key, 128))
                                names[key].push_back (name.substring (1));
                    }
                }

                for (auto& name : defineNamesBySize)
                    if (line.contains (name))
                        line = line.replace (name, defines[name]);

                // (There can be more than one #include on a line.)
                if (line.contains ("#include"))
                {
                    for (int i = line.indexOf ("#include"); i >= 0; i = line.indexOf (i + 1, "#include"))
                    {
                        const int open = line.indexOfChar (i, '"');
                        const int close = open >= 0 ? line.indexOfChar (open + 1, '"') : -1;

                        if (close > open)
                            read (folder.getChildFile (line.substring (open + 1, close).replaceCharacter ('\\', '/')), depth + 1);
                    }

                    continue;
                }

                text << line << " ";
            }
        }

        void readRegions()
        {
            // <global>, <master> and <group> pass their opcodes down to the
            // regions under them; a new one clears the levels below it.
            const juce::StringArray levels { "control", "global", "master", "group" };
            std::array<std::map<juce::String, juce::String>, 4> inherited;
            int i = text.indexOfChar ('<');

            while (i >= 0)
            {
                const int close = text.indexOfChar (i, '>');

                if (close < 0)
                    break;

                const auto header = text.substring (i + 1, close).trim();
                const int next = text.indexOfChar (close, '<');
                const auto body = text.substring (close + 1, next < 0 ? text.length() : next);
                auto opcodes = opcodesOf (body);
                i = next;

                if (const int level = levels.indexOf (header); level >= 0)
                {
                    inherited[(size_t) level] = opcodes;

                    for (int lower = level + 1; lower < 4; ++lower)
                        inherited[(size_t) lower].clear();

                    continue;
                }

                if (header != "region")
                    continue;

                std::map<juce::String, juce::String> region;

                for (auto& level : inherited)
                    for (auto& [name, value] : level)
                        region[name] = value;

                for (auto& [name, value] : opcodes)
                    region[name] = value;

                const auto key = region.count ("key") ? region["key"] : juce::String();
                const int lo = noteNumberOf (region.count ("lokey") ? region["lokey"] : key);
                const int hi = noteNumberOf (region.count ("hikey") ? region["hikey"] : key);

                // A drum is on a key or two; a wide range is something else
                // (a pitched sound, or a keyswitch).
                if (lo < 0 || hi < lo || hi - lo > 2 || hi > 127)
                    continue;

                juce::String name;

                for (const char* opcode : { "sample", "region_label", "group_label", "master_label" })
                    if (region.count (opcode))
                        name << region[opcode] << " ";

                for (int k = lo; k <= hi; ++k)
                    names[k].push_back (name.trim());
            }
        }
    };

    //==========================================================================
    // Telling the pieces apart

    using Words = std::set<juce::String>;

    bool hasAny (const Words& words, std::initializer_list<const char*> wanted)
    {
        for (auto* w : wanted)
            if (words.count (w) > 0)
                return true;

        return false;
    }

    /** Words that mean a sound other than the plain hit: a rim shot, a
        choke, a brush, another instrument... */
    int countOthers (const Words& words)
    {
        static const Words others { "rim", "rims", "rimshot", "edge", "ed", "ss", "side", "sidestick", "xstick", "cross",
                                    "brush", "brushes", "dig", "stir", "rel", "release", "buzz", "roll", "flam", "wsk",
                                    "crc", "rst", "noise", "mute", "choke", "chk", "stop", "grab", "shank", "swish",
                                    "return", "ret", "splash", "china", "sizzle", "fx", "perc", "clap", "snap", "cowbell",
                                    "shaker", "tamb", "tambourine", "maraca", "conga", "bongo", "clave", "whisker",
                                    "whiskers", "rimclick" };
        int n = 0;

        for (auto& w : words)
            n += (int) others.count (w);

        return n;
    }

    /** The number in a tom's name like "t1" or "t18" (-1 if there isn't one). */
    int tomNumberOf (const Words& words)
    {
        for (auto& w : words)
            if (w.length() >= 2 && w[0] == 't' && w.substring (1).containsOnly ("0123456789"))
                return w.substring (1).getIntValue();

        return -1;
    }
}

//==============================================================================
juce::StringArray wordsOf (const juce::String& name)
{
    juce::StringArray words;

    // Runs of letters and digits.
    juce::StringArray chunks;
    juce::String current;

    for (auto c : name)
    {
        if (juce::CharacterFunctions::isLetterOrDigit (c))
        {
            current << c;
        }
        else if (current.isNotEmpty())
        {
            chunks.add (current);
            current.clear();
        }
    }

    if (current.isNotEmpty())
        chunks.add (current);

    for (auto& chunk : chunks)
    {
        words.add (chunk.toLowerCase());

        // Within a chunk: "HatClosed" -> hat, closed; "bd8" -> bd, 8; "HTMLTom" -> html, tom.
        juce::String part;

        for (int i = 0; i < chunk.length(); ++i)
        {
            // juce_wchar: JUCE's type for one character.
            const juce::juce_wchar c = chunk[i];
            const juce::juce_wchar prev = i > 0 ? chunk[i - 1] : 0;
            const juce::juce_wchar next = i + 1 < chunk.length() ? chunk[i + 1] : 0;
            const bool digit = juce::CharacterFunctions::isDigit (c), prevDigit = juce::CharacterFunctions::isDigit (prev);
            const bool upper = juce::CharacterFunctions::isUpperCase (c);
            const bool split = i > 0 && (digit != prevDigit
                                         || (upper && juce::CharacterFunctions::isLowerCase (prev))
                                         || (upper && juce::CharacterFunctions::isUpperCase (prev) && juce::CharacterFunctions::isLowerCase (next)));

            if (split && part.isNotEmpty())
            {
                words.add (part.toLowerCase());
                part.clear();
            }

            part << c;
        }

        if (part.isNotEmpty() && part != chunk)
            words.add (part.toLowerCase());
    }

    words.removeDuplicates (false);
    return words;
}

KeyNames readSfzKeyNames (const juce::File& sfz)
{
    SfzReader reader;
    reader.folder = sfz.getParentDirectory();
    reader.read (sfz, 0);
    reader.readRegions();
    return reader.names;
}

std::array<int, numDrumPieces> guessKeys (const KeyNames& names)
{
    std::array<int, numDrumPieces> keys {};

    for (int i = 0; i < numDrumPieces; ++i)
        keys[(size_t) i] = getGeneralMidiNote ((DrumPiece) i);

    if (names.empty())
        return keys;

    // Each of a key's names as a set of words.
    std::map<int, std::vector<Words>> texts;

    for (auto& [key, list] : names)
    {
        std::set<juce::String> seen;

        for (auto& name : list)
        {
            if (! seen.insert (name).second)
                continue;

            Words words;

            for (auto& w : wordsOf (name))
                words.insert (w);

            texts[key].push_back (std::move (words));
        }
    }

    // Words most keys share (the pack's or a folder's name) say nothing
    // about any one of them.
    if (texts.size() >= 4)
    {
        std::map<juce::String, int> counts;

        for (auto& [key, list] : texts)
        {
            Words all;

            for (auto& words : list)
                all.insert (words.begin(), words.end());

            for (auto& w : all)
                ++counts[w];
        }

        for (auto& [key, list] : texts)
            for (auto& words : list)
                for (auto it = words.begin(); it != words.end();)
                    it = counts[*it] > 0.6 * (double) texts.size() ? words.erase (it) : std::next (it);
    }

    // An 808-style kit names its toms ht, mt and lt; elsewhere "ht" is a hi-hat.
    bool drumMachineToms = false;

    for (auto& [key, list] : texts)
        for (auto& words : list)
            drumMachineToms = drumMachineToms || hasAny (words, { "mt", "lt" });

    // Scores: 3 for a clear match (2 for a hi-hat that's probably the
    // closed one), less 2 for each word that means another sound. A key
    // scores its best name.
    enum Kind { kick, snare, closedHat, openHat, pedalHat, crash, ride, rideBell, tom, numKinds };
    std::array<std::map<int, int>, numKinds> scores;
    std::map<int, Words> allWords;

    for (auto& [key, list] : texts)
    {
        for (auto& w : list)
        {
            allWords[key].insert (w.begin(), w.end());

            const int others = countOthers (w);
            auto put = [&, k = key] (Kind kind, int score)
            {
                auto& best = scores[(size_t) kind];
                const int s = score - 2 * others;

                if (best.count (k) == 0 || best[k] < s)
                    best[k] = s;
            };

            const bool hat = hasAny (w, { "hh", "hihat", "hat", "hats", "hihats" }) || (hasAny (w, { "ht" }) && ! drumMachineToms);

            if (hasAny (w, { "kick", "kik", "bd", "kd", "bassdrum" }))
                put (kick, 3);

            if (hasAny (w, { "snare", "sn", "sd", "snr" }))
                put (snare, 3);

            if (hasAny (w, { "ch" }) || (hat && hasAny (w, { "closed", "close", "cl", "cs", "tight", "tc", "shut" })))
                put (closedHat, 3);
            else if (hat && ! hasAny (w, { "open", "op", "oh", "so", "semi", "half", "loose", "pedal", "foot", "ft", "chik", "chick", "ph", "pdl" }))
                put (closedHat, 2);

            if (hasAny (w, { "oh" }) || (hat && hasAny (w, { "open", "op", "so", "semi", "half", "loose" })))
                put (openHat, 3);

            if (hat && hasAny (w, { "pedal", "foot", "ft", "chik", "chick", "ph", "pdl" }))
                put (pedalHat, 3);

            if (hasAny (w, { "crash", "cr", "cy", "cym", "cymbal" }))
                put (crash, 3);

            if (hasAny (w, { "ride", "rd", "rid" }))
                put (hasAny (w, { "bell", "bl", "sbl", "tbl", "bel" }) ? rideBell : ride, 3);

            if (hasAny (w, { "tom", "toms", "floor", "flr", "rack" }) || tomNumberOf (w) >= 0
                 || (drumMachineToms && hasAny (w, { "ht", "mt", "lt" })))
                put (tom, 3);
        }
    }

    std::array<int, numKinds> found;
    found.fill (-1);
    std::set<int> used;

    // The best-scoring free key: on a tie the General MIDI key, else the lowest.
    auto best = [&] (Kind kind, int generalMidi)
    {
        int top = 0;

        for (auto& [key, score] : scores[(size_t) kind])
            if (used.count (key) == 0)
                top = std::max (top, score);

        if (top <= 0)
            return -1;

        int chosen = -1;

        for (auto& [key, score] : scores[(size_t) kind])
            if (score == top && used.count (key) == 0 && (chosen < 0 || key == generalMidi))
                chosen = key;

        return chosen;
    };

    const std::array<std::pair<Kind, DrumPiece>, 8> order {{
        { kick, DrumPiece::kick }, { snare, DrumPiece::snare }, { closedHat, DrumPiece::hihatClosed },
        { openHat, DrumPiece::hihatOpen }, { pedalHat, DrumPiece::hihatPedal }, { crash, DrumPiece::crash },
        { ride, DrumPiece::ride }, { rideBell, DrumPiece::rideBell } }};

    for (auto [kind, piece] : order)
    {
        found[(size_t) kind] = best (kind, getGeneralMidiNote (piece));

        if (found[(size_t) kind] >= 0)
            used.insert (found[(size_t) kind]);
    }

    // Toms, high to low: by their names (floor, low, mid, high), else their
    // number ("t1" is the high tom, and so is a 14" one beside an 18"),
    // else the higher key is the higher tom.
    std::vector<int> toms;

    for (auto& [key, score] : scores[(size_t) tom])
        if (score >= 3 && used.count (key) == 0)
            toms.push_back (key);

    auto rank = [&] (int key)
    {
        const auto& w = allWords[key];

        if (hasAny (w, { "floor", "flr", "lt", "low", "lo" }))  return std::make_pair (3, 0);
        if (hasAny (w, { "mid", "mt" }))                         return std::make_pair (2, 0);
        if (hasAny (w, { "high", "hi", "ht", "rack" }))          return std::make_pair (1, 0);
        if (const int n = tomNumberOf (w); n >= 0)               return std::make_pair (2, n);
        return std::make_pair (2, -key);
    };

    std::stable_sort (toms.begin(), toms.end(), [&] (int a, int b) { return rank (a) < rank (b); });

    // Does the kit follow General MIDI? Then a piece it hasn't named can
    // stay on its General MIDI key (if the kit plays something there).
    int onGeneralMidi = 0, named = 0;

    for (auto [kind, piece] : order)
    {
        if (found[(size_t) kind] >= 0)
        {
            ++named;
            onGeneralMidi += found[(size_t) kind] == getGeneralMidiNote (piece) ? 1 : 0;
        }
    }

    const bool generalMidiKit = named > 0 && onGeneralMidi * 2 >= named;
    auto set = [&keys] (DrumPiece p, int key) { keys[(size_t) p] = key; };

    for (auto [kind, piece] : order)
        if (found[(size_t) kind] >= 0)
            set (piece, found[(size_t) kind]);

    if (! toms.empty())
    {
        set (DrumPiece::tomHigh, toms.front());
        set (DrumPiece::tomFloor, toms.back());
        set (DrumPiece::tomMid, toms.size() >= 3 ? toms[toms.size() / 2] : toms.front());
    }

    // Pieces it has no name for: their General MIDI key on a General MIDI
    // kit, or else a close cousin.
    auto missing = [&] (Kind kind)  { return found[(size_t) kind] < 0; };
    auto generalMidiFree = [&] (DrumPiece p)
    {
        const int key = getGeneralMidiNote (p);
        return generalMidiKit && names.count (key) > 0 && used.count (key) == 0;
    };

    const std::array<std::pair<Kind, std::vector<DrumPiece>>, 6> cousins {{
        { closedHat, { DrumPiece::hihatPedal, DrumPiece::hihatOpen, DrumPiece::ride } },
        { pedalHat,  { DrumPiece::hihatClosed } },
        { openHat,   { DrumPiece::hihatClosed } },
        { ride,      { DrumPiece::hihatClosed } },
        { rideBell,  { DrumPiece::ride } },
        { crash,     { DrumPiece::ride, DrumPiece::hihatOpen } } }};

    std::array<bool, numDrumPieces> settled {};

    for (auto [kind, piece] : order)
        settled[(size_t) piece] = ! missing (kind);

    for (auto& [kind, alternatives] : cousins)
    {
        DrumPiece piece = DrumPiece::kick;

        for (auto [k, p] : order)
            if (k == kind)
                piece = p;

        if (settled[(size_t) piece])
            continue;

        if (generalMidiFree (piece))
        {
            used.insert (getGeneralMidiNote (piece));
            settled[(size_t) piece] = true;
            continue;   // (keys[] already has its General MIDI key)
        }

        for (auto alternative : alternatives)
        {
            if (settled[(size_t) alternative])
            {
                set (piece, keys[(size_t) alternative]);
                settled[(size_t) piece] = true;
                break;
            }
        }
    }

    return keys;
}

} // namespace ikaros::drumkit
