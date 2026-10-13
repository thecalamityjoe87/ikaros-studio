#pragma once

#include "engine/AudioEngine.h"
#include <functional>

namespace ikaros
{

/*  At launch, after a crash: for each session that was open with unsaved
    work (newest first), asks whether to Recover it or Discard it, showing
    its name and when it was autosaved. Discarding asks once more, since it
    can't be undone (and Escape picks Discard in a two-button alert).

    Uses the same alert boxes as "Save changes?", so it looks like the rest
    of the app.
*/
namespace RecoveryPrompt
{
    /** Calls `done` when it's finished: with true if a session was recovered
        (it's now open, so the session chooser isn't needed), false if there
        was nothing to recover or everything was discarded. If nothing needs
        asking, `done` is called straight away. */
    void show (juce::Component& parent, AudioEngine&, std::function<void (bool recovered)> done);
}

} // namespace ikaros
