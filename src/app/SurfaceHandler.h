#pragma once

#include "engine/ControlSurface.h"
#include "engine/ControllerMap.h"
#include "ui/state/Selection.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace ikaros
{

/** What hardware controllers do in the app:

    - a Mackie Control surface (see ControlSurface): its transport buttons,
      and its strips as the mixer's faders (volume), knobs (pan) and Mute /
      Solo / Record / Select buttons;
    - a controller the app has a map for (see ControllerMap), e.g. the
      MiniLab 3: its mapped knobs and buttons work on the selected track.

    The transport buttons run the same commands as the menus and keys.
    MainComponent calls update() on its timer. */
class SurfaceHandler
{
public:
    SurfaceHandler (AudioEngine&, Selection&, juce::ApplicationCommandManager&);

    void update();

private:
    void apply (const ControlSurface::Action&, const TransportState&, const std::vector<TrackInfo>&);
    void apply (const ControllerEvent&, const TransportState&, const std::vector<TrackInfo>&);

    // Shared by both kinds of controller.
    void run (juce::CommandID);
    void play (const TransportState&);
    void stop (const TransportState&);
    void moveBars (int bars, const TransportState&);
    void moveTo (double beat, const TransportState&);
    void selectNext (int direction, const std::vector<TrackInfo>&);
    void toggleMute (const TrackInfo*);
    void toggleSolo (const TrackInfo*);
    void toggleArm (const TrackInfo*);
    void startGesture (int control, const juce::String& undoName);

    AudioEngine& engine;
    Selection& selection;
    juce::ApplicationCommandManager& commands;
    ControlSurface surface;
    std::vector<ControllerEvent> heldBack;   // see update()
    juce::String surfacePort;                // the port the surface's strip count is set for

    // The fader or knob being moved, for one undo step per move (see startGesture).
    int gestureControl = -1;
    double lastGestureMs = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SurfaceHandler)
};

} // namespace ikaros
