#pragma once

// Loops that follow the session's key, for the engine's .cpp files.
//
// A loop from the library that knows its key (see SoundLibrary.h) is moved
// to the session's key when it's placed, and again whenever the key
// changes. Tracktion does the actual moving: with auto-pitch on, a clip is
// transposed by (session key's root - the clip's LoopInfo root note),
// folded into -6..+6 semitones so it goes whichever way is nearer.
//
// Tracktion only compares roots, so a minor loop in a major session would
// come out in the wrong mode (A minor in C major -> C minor). We compare
// the keys' relative majors instead (A minor and C major share their notes,
// so that loop isn't moved at all), by giving Tracktion an adjusted root
// note. That depends on the session's mode, so changing the key rewrites
// the root note of every loop clip (followSessionKey (Edit&)).
//
// The loop's own key is kept in two properties on the clip's state, so
// split, copy, duplicate, undo and saving all carry it along with the rest
// of the clip.

#include <tracktion_engine/tracktion_engine.h>

namespace ikaros::detail
{

namespace te = tracktion;

inline const juce::Identifier loopKeyRootProp  ("ikarosLoopKeyRoot");    // 0 = C ... 11 = B
inline const juce::Identifier loopKeyMinorProp ("ikarosLoopKeyMinor");

/** True if the clip is a loop that follows the session's key. */
inline bool followsSessionKey (const te::AudioClipBase& clip)
{
    return clip.state.hasProperty (loopKeyRootProp);
}

/** Points the clip's auto-pitch at the session's key (see the top of the file). */
inline void followSessionKey (te::AudioClipBase& clip)
{
    if (! followsSessionKey (clip))
        return;

    const int loopRoot = clip.state[loopKeyRootProp];
    const bool loopMinor = clip.state[loopKeyMinorProp];

    // Ikaros keeps one key for the whole session, at the start of the pitch sequence.
    const auto& session = clip.edit.pitchSequence.getPitchAt (clip.getPosition().getStart());
    const bool sessionMinor = session.getScale() == te::Scale::minor || session.getScale() == te::Scale::aeolian;

    // A minor key's relative major is 3 semitones up (A minor -> C major).
    const int relativeRoot = loopRoot + (loopMinor ? 3 : 0) - (sessionMinor ? 3 : 0);
    const int rootNote = 60 + (relativeRoot % 12 + 12) % 12;

    // These go through the clip's undo manager, so they're part of the
    // current undo step. Setting a value it already has records nothing.
    clip.getLoopInfo().setRootNote (rootNote);
    clip.setAutoPitchMode (te::AudioClipBase::pitchTrack);
    clip.setAutoPitch (true);
}

/** Marks a newly placed clip as a loop made in this key, and moves it to the session's key. */
inline void setLoopKey (te::AudioClipBase& clip, int root, bool minor)
{
    clip.state.setProperty (loopKeyRootProp, juce::jlimit (0, 11, root), clip.getUndoManager());
    clip.state.setProperty (loopKeyMinorProp, minor, clip.getUndoManager());
    followSessionKey (clip);
}

/** After the session's key changed: re-points every loop in the Edit at it. */
inline void followSessionKey (te::Edit& edit)
{
    for (auto* track : te::getAudioTracks (edit))
        for (auto* clip : track->getClips())
            if (auto* audio = dynamic_cast<te::AudioClipBase*> (clip))
                followSessionKey (*audio);
}

} // namespace ikaros::detail
