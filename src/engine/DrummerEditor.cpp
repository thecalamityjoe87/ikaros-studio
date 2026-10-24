#include "engine/DrummerEditor.h"
#include "engine/AudioEngine.h"
#include "engine/DrummerEngine.h"
#include "engine/DrumKitMap.h"
#include "engine/DrummerHelpers.h"
#include "engine/EffectsController.h"
#include "engine/SectionEditor.h"
#include "engine/SoundLibrary.h"
#include <cmath>
#include <map>
#include <tuple>

namespace te = tracktion;

namespace ikaros
{

using namespace detail;

const juce::String DrummerEditor::fallbackKit ("sampler:ikaros-studio.basics/Electronic Kit");

//==============================================================================
// The shared helpers (DrummerHelpers.h)
namespace detail
{
    std::array<int, numDrumPieces> getDrumKeys (const SoundLibrary& sounds, const juce::String& soundId)
    {
        auto* kit = sounds.resolveSampledInstrument (soundId);

        if (kit == nullptr)
            return drumkit::guessKeys ({});   // General MIDI

        // The kit's own drum map, if its pack has one...
        if (! kit->drumMap.empty())
        {
            auto keys = drumkit::guessKeys ({});

            for (auto& [name, key] : kit->drumMap)   // C++17 "structured bindings": the pair's two halves
                if (auto piece = drumPieceFromKey (name))
                    keys[(size_t) *piece] = key;

            return keys;
        }

        // ...or else what its sample names say (see DrumKitMap.h). Reading
        // a big SFZ program takes a moment, so each is read once: until
        // its file changes (a pack update). Message thread only.
        const auto file = kit->sfzFile.existsAsFile() ? kit->sfzFile
                                                      : (kit->zones.empty() ? juce::File() : kit->zones.front().file);
        const auto cacheKey = soundId + "|" + file.getFullPathName() + "|" + juce::String (file.getLastModificationTime().toMilliseconds());
        static std::map<juce::String, std::array<int, numDrumPieces>> guessed;

        if (auto found = guessed.find (cacheKey); found != guessed.end())
            return found->second;

        drumkit::KeyNames names;

        if (kit->sfzFile.existsAsFile())
        {
            names = drumkit::readSfzKeyNames (kit->sfzFile);
        }
        else
        {
            for (auto& zone : kit->zones)
                if (zone.high - zone.low <= 2)
                    for (int key = zone.low; key <= zone.high; ++key)
                        names[key].push_back (zone.file.getFileNameWithoutExtension());
        }

        return guessed[cacheKey] = drumkit::guessKeys (names);
    }

    void moveDrumNotesToKit (te::AudioTrack& track, const SoundLibrary& sounds, juce::UndoManager* um)
    {
        const auto keys = getDrumKeys (sounds, track.state.getProperty (soundIdProp).toString());

        for (auto* clip : track.getClips())
        {
            if (auto* midi = dynamic_cast<te::MidiClip*> (clip))
            {
                // Iterating a ValueTree gives its children (each a handle on
                // the shared node, so setProperty changes the real note).
                for (auto note : midi->getSequence().state)
                {
                    if (! note.hasType (te::IDs::NOTE) || ! note.hasProperty (drumPieceProp))
                        continue;

                    const int piece = note.getProperty (drumPieceProp);

                    if (juce::isPositiveAndBelow (piece, numDrumPieces))
                        note.setProperty (te::IDs::p, keys[(size_t) piece], um);
                }
            }
        }
    }
}

//==============================================================================
namespace
{
    // The settings, as properties of the IKAROSDRUMMER tree in the clip.
    // `madeFor` is the key: what the region's notes were made for.
    const juce::Identifier drummerProp ("drummer"), complexityProp ("complexity"), loudnessProp ("loudness"),
                           fillsProp ("fills"), swingProp ("swing"), kickSnareProp ("kickSnare"), hihatProp ("hihat"),
                           cymbalsProp ("cymbals"), tomsProp ("toms"), timekeepingProp ("timekeeping"),
                           seedProp ("seed"), madeForProp ("madeFor");

    juce::String timekeepingName (Timekeeping t)
    {
        return t == Timekeeping::hihat ? "hihat" : t == Timekeeping::ride ? "ride" : "auto";
    }

    Timekeeping timekeepingFrom (const juce::String& name)
    {
        return name == "hihat" ? Timekeeping::hihat : name == "ride" ? Timekeeping::ride : Timekeeping::automatic;
    }

    float unitFrom (const juce::ValueTree& v, const juce::Identifier& id, float fallback)
    {
        return juce::jlimit (0.0f, 1.0f, (float) (double) v.getProperty (id, fallback));
    }

    DrummerSettings readSettings (const juce::ValueTree& v)
    {
        DrummerSettings s;   // the defaults stand in for anything missing

        s.drummer = v.getProperty (drummerProp, s.drummer).toString();
        s.complexity = unitFrom (v, complexityProp, s.complexity);
        s.loudness = unitFrom (v, loudnessProp, s.loudness);
        s.fills = unitFrom (v, fillsProp, s.fills);
        s.swing = unitFrom (v, swingProp, s.swing);
        s.kickSnare = v.getProperty (kickSnareProp, true);
        s.hihat = v.getProperty (hihatProp, true);
        s.cymbals = v.getProperty (cymbalsProp, true);
        s.toms = v.getProperty (tomsProp, true);
        s.timekeeping = timekeepingFrom (v.getProperty (timekeepingProp).toString());
        s.seed = v.getProperty (seedProp, s.seed);
        return s;
    }

    // ValueTree::setProperty records nothing when the value is the same, so
    // only what changed goes into the undo step.
    void writeSettings (juce::ValueTree v, const DrummerSettings& s, juce::UndoManager* um)
    {
        v.setProperty (drummerProp, s.drummer, um);
        v.setProperty (complexityProp, s.complexity, um);
        v.setProperty (loudnessProp, s.loudness, um);
        v.setProperty (fillsProp, s.fills, um);
        v.setProperty (swingProp, s.swing, um);
        v.setProperty (kickSnareProp, s.kickSnare, um);
        v.setProperty (hihatProp, s.hihat, um);
        v.setProperty (cymbalsProp, s.cymbals, um);
        v.setProperty (tomsProp, s.toms, um);
        v.setProperty (timekeepingProp, timekeepingName (s.timekeeping), um);
        v.setProperty (seedProp, s.seed, um);
    }

    juce::ValueTree drummerStateOf (te::Clip& clip)
    {
        return clip.state.getChildWithName (drummerStateType);
    }

    juce::String drummerName (const juce::String& id)
    {
        if (auto* d = GrooveLibrary::get().findDrummerOrFirst (id))
            return d->info.name;

        return "Drummer";
    }

    //==========================================================================
    /** What a region's notes are made from: its bars and the song around
        them (for DrummerEngine), and the key that sums it up. */
    struct Recipe
    {
        DrumContext context;
        juce::String key;
    };

    Recipe makeRecipe (double fromBeat, double toBeat, const DrummerSettings& s, const TempoMap& tempo,
                       const std::vector<SectionInfo>& sections)
    {
        Recipe r;
        auto& c = r.context;
        c.fromBeat = fromBeat;
        c.toBeat = toBeat;

        const int firstBar = juce::jmax (0, tempo.barAt (fromBeat + 1.0e-6));
        const int lastBar = juce::jmax (firstBar, tempo.barAt (toBeat - 1.0e-6));

        // Where sections change: each start, and each end (into the next
        // section, or into a gap).
        std::vector<int> boundaries;

        for (auto& section : sections)
        {
            boundaries.push_back (tempo.nearestBar (section.startBeat));
            boundaries.push_back (tempo.nearestBar (section.endBeat));
        }

        std::sort (boundaries.begin(), boundaries.end());
        boundaries.erase (std::unique (boundaries.begin(), boundaries.end()), boundaries.end());

        // Phrases count from the section change before the region (see DrumContext).
        int anchor = 0;

        for (auto b : boundaries)
            if (b <= firstBar)
                anchor = juce::jmax (anchor, b);

        for (auto b : boundaries)
            if (b >= anchor && b <= lastBar + 1)
                c.boundaries.push_back (b);

        for (int bar = anchor; bar <= lastBar + 1; ++bar)
        {
            DrumBar d;
            d.bar = bar;
            d.startBeat = tempo.barToBeat (bar);
            const auto& sig = tempo.signatureAt (d.startBeat + 1.0e-6);
            d.numerator = sig.numerator;
            d.denominator = sig.denominator;
            d.bpm = tempo.tempoAt (d.startBeat + 1.0e-6).bpm;
            c.bars.push_back (d);
        }

        // The key: the settings, the region, and the markers and time
        // signatures it plays to. Not the tempo: the notes are in beats, so
        // a tempo change doesn't need new ones. (Changing this would remake
        // regions the first time they're touched, so keep it as it is.)
        auto& k = r.key;
        k << "1|" << s.drummer << "|" << juce::String (s.complexity, 4) << "|" << juce::String (s.loudness, 4)
          << "|" << juce::String (s.fills, 4) << "|" << juce::String (s.swing, 4)
          << "|" << (s.kickSnare ? "k" : "-") << (s.hihat ? "h" : "-") << (s.cymbals ? "c" : "-") << (s.toms ? "t" : "-")
          << "|" << timekeepingName (s.timekeeping) << "|" << s.seed
          << "|" << juce::String (fromBeat, 4) << "-" << juce::String (toBeat, 4)
          << "|" << anchor << "@" << juce::String (tempo.barToBeat (anchor), 4) << "|";

        for (auto b : c.boundaries)
            k << b << ",";

        k << "|";
        int lastNumerator = 0, lastDenominator = 0;

        for (auto& bar : c.bars)
        {
            if (bar.numerator != lastNumerator || bar.denominator != lastDenominator)
                k << bar.bar << ":" << bar.numerator << "/" << bar.denominator << ",";

            lastNumerator = bar.numerator;
            lastDenominator = bar.denominator;
        }

        return r;
    }

    //==========================================================================
    /** Writes a part's hits as the clip's notes. Notes that are already
        there exactly as wanted stay (with their ids), so a small change (a
        piece turned off, say) makes a small undo step. */
    void writeNotes (te::MidiClip& clip, const DrumPart& part, const std::array<int, numDrumPieces>& keys,
                     juce::UndoManager* um)
    {
        auto sequence = clip.getSequence().state;
        const double contentStart = clip.getContentStartBeat().inBeats();
        constexpr double noteLength = 0.125;   // drums play to the end of their sample anyway

        // A note, for matching: its piece, key, position (to a millionth of
        // a beat) and velocity.
        using Match = std::tuple<int, int, long long, int>;
        auto matchOf = [] (int piece, int key, double beat, int velocity)
        {
            return Match { piece, key, std::llround (beat * 1.0e6), velocity };
        };

        // std::multimap: sorted by key, and a key can be there more than once.
        std::multimap<Match, juce::ValueTree> existing;
        NoteId highestId = 0;

        for (auto note : sequence)
        {
            if (! note.hasType (te::IDs::NOTE))
                continue;

            highestId = juce::jmax (highestId, (NoteId) (juce::int64) note.getProperty (noteIdProp, 0));
            existing.emplace (matchOf (note.getProperty (drumPieceProp, -1), note.getProperty (te::IDs::p),
                                       note.getProperty (te::IDs::b), note.getProperty (te::IDs::v)),
                              note);
        }

        std::vector<const DrumHit*> toAdd;

        for (auto& hit : part.hits)
        {
            auto found = existing.find (matchOf ((int) hit.piece, keys[(size_t) hit.piece], hit.beat - contentStart, hit.velocity));

            if (found != existing.end())
                existing.erase (found);       // it's there already: keep it
            else
                toAdd.push_back (&hit);
        }

        // What's left over isn't wanted any more.
        for (auto& [match, note] : existing)
            sequence.removeChild (note, um);

        for (auto* hit : toAdd)
        {
            juce::ValueTree note (te::IDs::NOTE);
            note.setProperty (te::IDs::p, keys[(size_t) hit->piece], nullptr);
            note.setProperty (te::IDs::b, hit->beat - contentStart, nullptr);
            note.setProperty (te::IDs::l, noteLength, nullptr);
            note.setProperty (te::IDs::v, hit->velocity, nullptr);
            note.setProperty (te::IDs::c, 0, nullptr);
            note.setProperty (noteIdProp, (juce::int64) ++highestId, nullptr);
            note.setProperty (drumPieceProp, (int) hit->piece, nullptr);

            // Only adding the finished note is recorded: one undo action per note.
            sequence.addChild (note, -1, um);
        }
    }

    /** Puts a region's settings and notes back as they were (copies taken
        earlier), without undo. */
    void restore (te::MidiClip& clip, const juce::ValueTree& settings, const juce::ValueTree& sequence)
    {
        if (auto state = drummerStateOf (clip); state.isValid())
            state.copyPropertiesFrom (settings, nullptr);

        auto now = clip.getSequence().state;

        for (int i = now.getNumChildren(); --i >= 0;)
            if (now.getChild (i).hasType (te::IDs::NOTE))
                now.removeChild (i, nullptr);

        for (auto note : sequence)
            if (note.hasType (te::IDs::NOTE))
                now.addChild (note.createCopy(), -1, nullptr);
    }
}

//==============================================================================
DrummerEditor::DrummerEditor (AudioEngine& o, EffectsController& e, const SectionEditor& s)
    : owner (o), effects (e), sections (s)
{
}

DrummerEditor::~DrummerEditor() = default;

void DrummerEditor::sessionChanged()
{
    preview.reset();
}

std::vector<DrummerInfo> DrummerEditor::getDrummers() const
{
    std::vector<DrummerInfo> list;

    for (auto& d : GrooveLibrary::get().getDrummers())
        list.push_back (d.info);

    return list;
}

//==============================================================================
// Tracks
bool DrummerEditor::isDrummerTrack (TrackId id) const
{
    auto* t = findTrack (owner.getEdit(), id);
    return t != nullptr && detail::isDrummerTrack (*t);
}

RegionId DrummerEditor::setUpDrummerTrack (TrackId id, const DrummerSettings& settings)
{
    auto* edit = owner.getEdit();
    auto* t = findTrack (edit, id);

    if (t == nullptr)
        return 0;

    auto* um = &edit->getUndoManager();
    t->state.setProperty (kindProp, drummerKind, um);

    // The drummer's kit, or the fallback until its pack is installed.
    const auto* drummer = GrooveLibrary::get().findDrummerOrFirst (settings.drummer);
    const auto kit = drummer != nullptr && drummer->info.kit.isNotEmpty() ? drummer->info.kit : fallbackKit;
    const bool installed = owner.getSounds().resolveSampledInstrument (kit) != nullptr;
    effects.setSound (id, installed ? kit : fallbackKit, false);

    // After setSound, which takes the sound it was given as the track's choice.
    t->state.setProperty (drummerKitProp, kit, um);

    // Over the song's sections, or eight bars.
    const auto tempo = owner.getTempoMap();
    const auto list = sections.getSections();
    double start = 0.0, end = tempo.barToBeat (8);

    if (! list.empty())
    {
        start = list.front().startBeat;
        end = list.back().endBeat;
    }

    return addDrummerRegion (id, start, end, settings);
}

juce::String DrummerEditor::getMissingKitPack (TrackId id, juce::String* kitName) const
{
    auto* t = findTrack (owner.getEdit(), id);

    if (t == nullptr || ! detail::isDrummerTrack (*t))
        return {};

    const auto wanted = t->state.getProperty (drummerKitProp).toString();

    if (wanted.isEmpty() || owner.getSounds().resolveSampledInstrument (wanted) != nullptr)
        return {};

    if (kitName != nullptr)
        *kitName = wanted.fromLastOccurrenceOf ("/", false, false);

    return SoundLibrary::getPackIdOf (wanted);
}

void DrummerEditor::updateKits (bool asUndoStep)
{
    auto* edit = owner.getEdit();

    if (edit == nullptr)
        return;

    auto& sounds = owner.getSounds();

    for (auto* t : getUserTracks (*edit))
    {
        if (! detail::isDrummerTrack (*t))
            continue;

        const auto id = t->itemID.getRawID();
        const auto wanted = t->state.getProperty (drummerKitProp).toString();
        const auto playing = t->state.getProperty (soundIdProp).toString();
        const bool wantedHere = sounds.resolveSampledInstrument (wanted) != nullptr;
        juce::String sound;

        if (wanted.isEmpty())
            continue;

        // Its own kit has arrived: over to it.
        if (wantedHere && playing != wanted)
            sound = wanted;

        // Its kit isn't installed (any more): the fallback until it is.
        else if (! wantedHere && playing == wanted && SoundLibrary::getPackIdOf (wanted).isNotEmpty())
            sound = fallbackKit;

        if (sound.isEmpty())
        {
            // The same kit, but its keys may have been worked out better
            // since the notes were written (see DrumKitMap.h): the notes
            // follow the kit. Not an edit, so not an undo step.
            if (! asUndoStep)
                moveDrumNotesToKit (*t, sounds, nullptr);

            continue;
        }

        if (! asUndoStep)
        {
            // Opening a session: not an edit. (Tracktion records a few actions
            // of its own while it sets up the new instrument; the history is
            // cleared once the session has settled, see AudioEngine.)
            effects.setSound (id, sound, false, false);
            continue;
        }

        const auto* info = sounds.resolveSampledInstrument (sound);
        owner.beginUndoStep ("Use " + (info != nullptr ? info->info.name : juce::String ("Drum Kit")));
        effects.setSound (id, sound, false);

        // setSound takes a sound it's given as the track's own choice: it
        // still wants its kit.
        t->state.setProperty (drummerKitProp, wanted, &edit->getUndoManager());
    }
}

//==============================================================================
// Regions
bool DrummerEditor::isDrummerRegion (RegionId id) const
{
    auto* clip = findClip (owner.getEdit(), id);
    return clip != nullptr && isDrummerClip (*clip);
}

RegionId DrummerEditor::addDrummerRegion (TrackId id, double startBeat, double endBeat, const DrummerSettings& settings)
{
    auto* edit = owner.getEdit();
    auto* t = findTrack (edit, id);

    if (t == nullptr || ! detail::isDrummerTrack (*t) || endBeat <= startBeat)
        return 0;

    const te::TimeRange range (beatToTime (*edit, startBeat), beatToTime (*edit, endBeat));
    auto* clip = dynamic_cast<te::MidiClip*> (t->insertNewClip (te::TrackItem::Type::midi, drummerName (settings.drummer), range, nullptr));

    if (clip == nullptr)
        return 0;

    auto* um = &edit->getUndoManager();
    juce::ValueTree state (drummerStateType);
    writeSettings (state, settings, nullptr);
    clip->state.addChild (state, -1, um);
    regenerate (*clip, um);
    return clip->itemID.getRawID();
}

RegionId DrummerEditor::addDrummerRegionAt (TrackId id, double beat)
{
    auto* t = findTrack (owner.getEdit(), id);

    if (t == nullptr || ! detail::isDrummerTrack (*t))
        return 0;

    const auto tempo = owner.getTempoMap();
    const int firstBar = tempo.barAt (juce::jmax (0.0, beat));
    const double start = tempo.barToBeat (firstBar);
    double end = tempo.barToBeat (firstBar + 8);
    DrummerSettings settings;
    double nearest = -1.0;

    for (auto* clip : t->getClips())
    {
        const double cs = clip->getStartBeat().inBeats(), ce = clip->getEndBeat().inBeats();

        if (start >= cs - 1.0e-6 && start < ce - 1.0e-6)
            return 0;   // it would start inside another region

        if (cs > start)
            end = juce::jmin (end, cs);

        // The same drummer as the region before it, playing the same way.
        if (isDrummerClip (*clip) && ce <= start + 1.0e-6 && ce > nearest)
        {
            nearest = ce;
            settings = readSettings (drummerStateOf (*clip));
        }
    }

    if (end < tempo.barToBeat (firstBar + 1) - 1.0e-6)
        return 0;

    owner.beginUndoStep ("Add Drummer Region");
    return addDrummerRegion (id, start, end, settings);
}

std::optional<DrummerSettings> DrummerEditor::getSettings (RegionId id) const
{
    if (auto* clip = findClip (owner.getEdit(), id); clip != nullptr && isDrummerClip (*clip))
        return readSettings (drummerStateOf (*clip));

    return std::nullopt;
}

void DrummerEditor::setSettings (RegionId id, const DrummerSettings& settings, const juce::String& undoName)
{
    auto* edit = owner.getEdit();
    auto* clip = findMidiClip (edit, id);

    if (clip == nullptr || ! isDrummerClip (*clip))
        return;

    auto state = drummerStateOf (*clip);
    const auto old = readSettings (state);

    if (old == settings)
        return;

    owner.beginUndoStep (undoName);
    auto* um = &edit->getUndoManager();

    // A region still named after its drummer takes the new one's name.
    if (settings.drummer != old.drummer && clip->getName() == drummerName (old.drummer))
        clip->setName (drummerName (settings.drummer));

    writeSettings (state, settings, um);
    regenerate (*clip, um);
}

void DrummerEditor::previewSettings (RegionId id, const DrummerSettings& settings)
{
    auto* clip = findMidiClip (owner.getEdit(), id);

    if (clip == nullptr || ! isDrummerClip (*clip))
        return;

    if (preview.has_value() && preview->region != id)
        cancelPreview();

    // Copies of how it was, to put back before the real (undoable) change.
    if (! preview.has_value())
        preview = Preview { id, drummerStateOf (*clip).createCopy(), clip->getSequence().state.createCopy() };

    writeSettings (drummerStateOf (*clip), settings, nullptr);
    regenerate (*clip, nullptr);
}

void DrummerEditor::finishPreview (const juce::String& undoName)
{
    if (! preview.has_value())
        return;

    const auto p = std::move (*preview);
    preview.reset();

    if (auto* clip = findMidiClip (owner.getEdit(), p.region); clip != nullptr && isDrummerClip (*clip))
    {
        // Back to how it was before the drag, then the drag's result again,
        // this time as one undo step.
        const auto settings = readSettings (drummerStateOf (*clip));
        restore (*clip, p.settings, p.sequence);
        setSettings (p.region, settings, undoName);
    }
}

void DrummerEditor::cancelPreview()
{
    if (! preview.has_value())
        return;

    const auto p = std::move (*preview);
    preview.reset();

    if (auto* clip = findMidiClip (owner.getEdit(), p.region))
        restore (*clip, p.settings, p.sequence);
}

void DrummerEditor::newVariation (RegionId id)
{
    if (auto settings = getSettings (id))
    {
        const auto old = settings->seed;

        // Any new seed will do; positive and well inside an int.
        while (settings->seed == old)
            settings->seed = 1 + juce::Random::getSystemRandom().nextInt (0x3fffffff);

        setSettings (id, *settings, "New Variation");
    }
}

void DrummerEditor::convertToMidiRegion (RegionId id)
{
    auto* edit = owner.getEdit();
    auto* clip = findMidiClip (edit, id);

    if (clip == nullptr || ! isDrummerClip (*clip))
        return;

    if (preview.has_value() && preview->region == id)
        finishPreview ("Drummer Settings");

    owner.beginUndoStep ("Convert to MIDI Region");

    // The notes stay as they are (each still knows its drum piece, so they
    // follow a change of kit); only the settings go.
    clip->state.removeChild (drummerStateOf (*clip), &edit->getUndoManager());
}

std::vector<int> DrummerEditor::getSilentBars (RegionId id) const
{
    std::vector<int> silent;
    auto* clip = findClip (owner.getEdit(), id);

    if (clip == nullptr || ! isDrummerClip (*clip))
        return silent;

    const auto tempo = owner.getTempoMap();
    const double from = clip->getStartBeat().inBeats(), to = clip->getEndBeat().inBeats();

    for (int bar = juce::jmax (0, tempo.barAt (from + 1.0e-6)); tempo.barToBeat (bar) < to - 1.0e-6; ++bar)
    {
        const auto& sig = tempo.signatureAt (tempo.barToBeat (bar) + 1.0e-6);

        if (sig.numerator != 4 || sig.denominator != 4)
            silent.push_back (bar);
    }

    return silent;
}

DrumPart DrummerEditor::makePart (RegionId id) const
{
    auto* clip = findClip (owner.getEdit(), id);

    if (clip == nullptr || ! isDrummerClip (*clip))
        return {};

    const auto settings = readSettings (drummerStateOf (*clip));
    const auto recipe = makeRecipe (clip->getStartBeat().inBeats(), clip->getEndBeat().inBeats(), settings,
                                    owner.getTempoMap(), sections.getSections());
    return drummer::generate (GrooveLibrary::get(), settings, recipe.context);
}

std::array<int, numDrumPieces> DrummerEditor::getDrumKeys (TrackId id) const
{
    auto* t = findTrack (owner.getEdit(), id);
    return detail::getDrumKeys (owner.getSounds(), t != nullptr ? t->state.getProperty (soundIdProp).toString() : fallbackKit);
}

//==============================================================================
// Keeping the notes up to date
void DrummerEditor::regenerate (te::MidiClip& clip, juce::UndoManager* um)
{
    auto state = drummerStateOf (clip);
    const auto settings = readSettings (state);
    const auto recipe = makeRecipe (clip.getStartBeat().inBeats(), clip.getEndBeat().inBeats(), settings,
                                    owner.getTempoMap(), sections.getSections());
    const auto part = drummer::generate (GrooveLibrary::get(), settings, recipe.context);

    auto* track = dynamic_cast<te::AudioTrack*> (clip.getClipTrack());
    const auto keys = detail::getDrumKeys (owner.getSounds(), track != nullptr ? track->state.getProperty (soundIdProp).toString()
                                                                               : fallbackKit);

    const juce::ScopedValueSetter<bool> busy (refreshing, true);   // sets it back when this returns
    writeNotes (clip, part, keys, um);
    state.setProperty (madeForProp, recipe.key, um);
}

void DrummerEditor::refreshClips (const std::vector<te::Clip*>& clips)
{
    auto* edit = owner.getEdit();

    if (edit == nullptr || clips.empty())
        return;

    const auto tempo = owner.getTempoMap();
    const auto sectionList = sections.getSections();

    for (auto* clip : clips)
    {
        auto* midi = dynamic_cast<te::MidiClip*> (clip);

        if (midi == nullptr || ! isDrummerClip (*midi))
            continue;

        auto state = drummerStateOf (*midi);
        const auto key = makeRecipe (midi->getStartBeat().inBeats(), midi->getEndBeat().inBeats(),
                                     readSettings (state), tempo, sectionList).key;

        if (key != state.getProperty (madeForProp).toString())
            regenerate (*midi, &edit->getUndoManager());   // into the undo step of the edit that moved it
    }
}

void DrummerEditor::refresh (const ModelChanges& changes)
{
    auto* edit = owner.getEdit();

    // Opening (or replacing) a session never remakes notes: they're as
    // they were saved. Nor does an undo or redo (which brings back notes
    // and key together), or a drag on the XY pad (which keeps its own).
    if (edit == nullptr || refreshing || preview.has_value() || changes.session
         || edit->getUndoManager().isPerformingUndoRedo())
        return;

    std::vector<te::Clip*> clips;

    // A region added or removed, or the markers, time signatures or tracks
    // changed: any region might be affected. Otherwise only the ones touched.
    if (changes.regions || changes.transport || changes.tracks)
    {
        for (auto* t : getUserTracks (*edit))
            if (detail::isDrummerTrack (*t))
                for (auto* clip : t->getClips())
                    clips.push_back (clip);
    }
    else
    {
        for (auto id : changes.changedRegions)
            if (auto* clip = findClip (edit, id))
                clips.push_back (clip);
    }

    refreshClips (clips);
}

void DrummerEditor::refreshAll()
{
    if (auto* edit = owner.getEdit())
    {
        std::vector<te::Clip*> clips;

        for (auto* t : getUserTracks (*edit))
            for (auto* clip : t->getClips())
                clips.push_back (clip);

        refreshClips (clips);
    }
}

} // namespace ikaros
