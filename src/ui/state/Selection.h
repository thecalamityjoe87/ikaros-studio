#pragma once

#include "engine/AudioEngine.h"
#include "engine/TrackController.h"

namespace ikaros
{

//==============================================================================
/**
    What the user has selected: one track and (optionally) one region.

    Components that care (track headers, regions, the bottom panel) listen for
    change messages. Track selection is forwarded to the engine because it
    decides which instrument the keyboards play.
*/
class Selection  : public juce::ChangeBroadcaster
{
public:
    explicit Selection (AudioEngine& e) : engine (e) {}

    TrackId getTrack() const      { return engine.getTracks().getSelectedTrack(); }
    RegionId getRegion() const    { return region; }

    void selectTrack (TrackId id)
    {
        if (id != getTrack())
        {
            engine.getTracks().setSelectedTrack (id);
            region = 0;
            sendChangeMessage();
        }
    }

    void selectRegion (TrackId track, RegionId r)
    {
        if (track != getTrack() || r != region)
        {
            engine.getTracks().setSelectedTrack (track);
            region = r;
            sendChangeMessage();
        }
    }

    /** Drops selections that no longer exist (after undo, delete, or loading a session). */
    void validate()
    {
        bool changed = false;
        auto& tracks = engine.getTracks();

        if (! tracks.getTrack (getTrack()).has_value())
        {
            auto all = tracks.getTracks();
            tracks.setSelectedTrack (all.empty() ? 0 : all.front().id);
            changed = true;
        }

        if (region != 0 && ! tracks.getRegion (region).has_value())
        {
            region = 0;
            changed = true;
        }

        if (changed)
            sendChangeMessage();
    }

private:
    AudioEngine& engine;
    RegionId region = 0;
};

} // namespace ikaros
