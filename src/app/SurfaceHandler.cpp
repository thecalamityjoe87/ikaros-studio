#include "app/SurfaceHandler.h"
#include "app/CommandIDs.h"
#include "engine/EffectsController.h"

namespace ikaros
{

namespace
{
    // A fader or knob that rests this long has finished its move.
    constexpr double gestureEndMs = 500.0;

    // startGesture's control numbers: a Mackie strip's controls, then a
    // mapped controller's (by MIDI controller number).
    int mackieControl (const mackie::Event& e)  { return e.strip * 64 + (int) e.type; }
    int mappedControl (const ControllerEvent& e) { return 10000 + e.controller; }

    const TrackInfo* findTrack (const std::vector<TrackInfo>& tracks, TrackId id)
    {
        for (auto& t : tracks)
            if (t.id == id)
                return &t;

        return nullptr;
    }
}

SurfaceHandler::SurfaceHandler (AudioEngine& e, Selection& s, juce::ApplicationCommandManager& c)
    : engine (e), selection (s), commands (c)
{
}

void SurfaceHandler::update()
{
    auto mapped = engine.getControllerMap().takeEvents();
    const bool hasSurface = surface.getPortName().isNotEmpty();

    // Nothing to do (and no track list to build) when no controller did anything.
    if (mapped.empty() && heldBack.empty() && ! hasSurface)
        return;

    const auto transport = engine.getTransportState();
    const auto tracks = engine.getTracks().getTracks();

    if (hasSurface)
    {
        // A controller with fewer faders than Mackie Control's 8 says so in its profile.
        if (surface.getPortName() != surfacePort)
        {
            surfacePort = surface.getPortName();
            const auto profile = engine.getControllerMap().getProfileForMackiePort (surfacePort);
            surface.setNumStrips (profile ? profile->faders : mackie::numStrips);
        }

        const auto selected = selection.getTrack();

        ControlSurface::State state;
        state.playing = transport.playing;
        state.recording = transport.recording;
        state.looping = transport.looping;
        state.metronome = transport.metronome;
        state.masterVolume = engine.getTracks().getMasterVolume();

        for (auto& t : tracks)
            state.tracks.push_back ({ t.id, t.id == selected, t.muted, t.solo, t.armed, t.volume, t.pan });

        for (auto& action : surface.update (state))
            apply (action, transport, tracks);
    }

    // A control that also sends Mackie Control (see ControllerProfile): while
    // the Mackie port is talking, its Mackie message is the one that counts,
    // so a press isn't done twice. Until that port has said anything, the
    // plain message waits a tick: both arrive together, so by then the Mackie
    // one would have been heard.
    const bool mackieTalking = hasSurface && surface.hasHeardFromSurface();
    std::vector<ControllerEvent> waited;
    std::swap (waited, heldBack);

    for (auto& event : waited)
        if (! mackieTalking)
            apply (event, transport, tracks);

    for (auto& event : mapped)
    {
        if (! event.alsoMackie || ! hasSurface)
            apply (event, transport, tracks);
        else if (! mackieTalking)
            heldBack.push_back (event);
    }
}

//==============================================================================
void SurfaceHandler::run (juce::CommandID id)
{
    commands.invokeDirectly (id, false);   // as its menu item or key would
}

void SurfaceHandler::play (const TransportState& transport)
{
    if (! transport.playing)
        run (CommandIDs::togglePlay);
}

void SurfaceHandler::stop (const TransportState& transport)
{
    // Stop when stopped goes back to the start, as in most apps.
    run (transport.playing ? CommandIDs::togglePlay : CommandIDs::goToStart);
}

void SurfaceHandler::moveBars (int bars, const TransportState& transport)
{
    // Back: to the start of this bar first (or the one before, if it's
    // already there). Forward: to the next bar line.
    const auto tempo = engine.getTempoMap();

    if (bars < 0)
        moveTo (tempo.barToBeat (tempo.barAt (transport.positionBeats - 0.25) + bars + 1), transport);
    else if (bars > 0)
        moveTo (tempo.barToBeat (tempo.barAt (transport.positionBeats) + bars), transport);
}

void SurfaceHandler::moveTo (double beat, const TransportState& transport)
{
    // Moving the playhead mid-take would cut the take short.
    if (! transport.recording)
        engine.setPositionBeats (juce::jmax (0.0, beat));
}

void SurfaceHandler::selectNext (int direction, const std::vector<TrackInfo>& tracks)
{
    const auto selected = selection.getTrack();

    for (size_t i = 0; i < tracks.size(); ++i)
    {
        if (tracks[i].id == selected)
        {
            const auto next = (int) i + direction;

            if (next >= 0 && next < (int) tracks.size())
                selection.selectTrack (tracks[(size_t) next].id);

            return;
        }
    }

    if (! tracks.empty())
        selection.selectTrack (tracks.front().id);
}

// Mute and solo aren't undo steps (they're how you listen, not song edits).
void SurfaceHandler::toggleMute (const TrackInfo* t)
{
    if (t != nullptr)
        engine.getTracks().setMute (t->id, ! t->muted);
}

void SurfaceHandler::toggleSolo (const TrackInfo* t)
{
    if (t != nullptr)
        engine.getTracks().setSolo (t->id, ! t->solo);
}

void SurfaceHandler::toggleArm (const TrackInfo* t)
{
    if (t != nullptr)
        engine.getTracks().inputs().setArmed (t->id, ! t->armed);
}

void SurfaceHandler::startGesture (int control, const juce::String& undoName)
{
    // One undo step per move, as when dragging the on-screen control: a new
    // one when another control moves, or this one has rested.
    const auto now = juce::Time::getMillisecondCounterHiRes();

    if (control != gestureControl || now - lastGestureMs > gestureEndMs)
        engine.beginUndoStep (undoName);

    gestureControl = control;
    lastGestureMs = now;
}

//==============================================================================
void SurfaceHandler::apply (const ControlSurface::Action& action, const TransportState& transport,
                            const std::vector<TrackInfo>& tracks)
{
    using T = mackie::Event::Type;
    const auto& e = action.event;
    auto& trackController = engine.getTracks();

    switch (e.type)
    {
        case T::play:           play (transport); break;
        case T::stop:           stop (transport); break;
        case T::record:         run (CommandIDs::toggleRecord); break;
        case T::cycle:          run (CommandIDs::toggleLoop); break;
        case T::click:          run (CommandIDs::toggleMetronome); break;
        case T::undo:           run (CommandIDs::undo); break;
        case T::save:           run (CommandIDs::saveSession); break;
        case T::rewind:         moveBars (-1, transport); break;
        case T::fastForward:    moveBars (1, transport); break;

        // The jog wheel moves the playhead a beat per click.
        case T::jog:            moveTo (std::round (transport.positionBeats) + e.steps, transport); break;

        case T::up:             selectNext (-1, tracks); break;
        case T::down:           selectNext (1, tracks); break;

        case T::select:
            if (action.track != 0)
                selection.selectTrack (action.track);
            break;

        case T::mute:           toggleMute (findTrack (tracks, action.track)); break;
        case T::solo:           toggleSolo (findTrack (tracks, action.track)); break;
        case T::recArm:         toggleArm (findTrack (tracks, action.track)); break;

        case T::fader:
            if (e.strip == mackie::masterStrip)
            {
                startGesture (mackieControl (e), "Master Volume");
                trackController.setMasterVolume (e.position);
            }
            else if (action.track != 0)
            {
                startGesture (mackieControl (e), "Volume");
                trackController.setVolume (action.track, e.position);
            }
            break;

        case T::faderTouch:
            if (e.pressed)
                gestureControl = -1;   // a new touch is a new move
            break;

        case T::vpot:
            // The pan now, not from `tracks`: a fast turn sends several clicks at once.
            if (auto t = trackController.getTrack (action.track))
            {
                startGesture (mackieControl (e), "Pan");
                trackController.setPan (t->id, juce::jlimit (-1.0f, 1.0f, t->pan + 0.02f * (float) e.steps));
            }
            break;

        // Pushing a knob centres the pan.
        case T::vpotPush:
            if (action.track != 0)
            {
                engine.beginUndoStep ("Pan");
                trackController.setPan (action.track, 0.0f);
            }
            break;

        case T::bankLeft: case T::bankRight: case T::channelLeft: case T::channelRight:
            break;   // the surface handles these itself
    }
}

void SurfaceHandler::apply (const ControllerEvent& e, const TransportState& transport,
                            const std::vector<TrackInfo>& tracks)
{
    using A = ControllerAction;
    auto& trackController = engine.getTracks();
    const auto selected = selection.getTrack();
    const auto* track = findTrack (tracks, selected);

    switch (e.action)
    {
        case A::play:           play (transport); break;
        case A::stop:           stop (transport); break;
        case A::playStop:       run (CommandIDs::togglePlay); break;
        case A::record:         run (CommandIDs::toggleRecord); break;
        case A::loop:           run (CommandIDs::toggleLoop); break;
        case A::metronome:      run (CommandIDs::toggleMetronome); break;
        case A::undo:           run (CommandIDs::undo); break;
        case A::redo:           run (CommandIDs::redo); break;
        case A::goToStart:      run (CommandIDs::goToStart); break;
        case A::rewind:         moveBars (-1, transport); break;
        case A::fastForward:    moveBars (1, transport); break;
        case A::previousTrack:  selectNext (-1, tracks); break;
        case A::nextTrack:      selectNext (1, tracks); break;
        case A::muteTrack:      toggleMute (track); break;
        case A::soloTrack:      toggleSolo (track); break;
        case A::armTrack:       toggleArm (track); break;

        case A::selectTrack:
            // One track per click (selectNext reads the selection again each time).
            for (int i = 0; i < std::abs (e.steps); ++i)
                selectNext (e.steps > 0 ? 1 : -1, tracks);
            break;

        case A::moveBars:       moveBars (e.steps, transport); break;
        case A::faderBank:      surface.showGroup (e.index); break;

        case A::masterVolume:
            startGesture (mappedControl (e), "Master Volume");
            trackController.setMasterVolume (e.value);
            break;

        case A::volume:
            if (track != nullptr)
            {
                startGesture (mappedControl (e), "Volume");
                trackController.setVolume (track->id, e.value);
            }
            break;

        case A::pan:
            if (track != nullptr)
            {
                startGesture (mappedControl (e), "Pan");
                trackController.setPan (track->id, e.value * 2.0f - 1.0f);
            }
            break;

        case A::reverbSend:
        case A::delaySend:
            if (track != nullptr)
            {
                const int bus = e.action == A::reverbSend ? SendBuses::reverb : SendBuses::delay;
                startGesture (mappedControl (e), SendBuses::getBusName (bus) + " Send");
                trackController.sends().setSendLevel (track->id, bus, e.value);
            }
            break;

        case A::soundKnob:
            if (track != nullptr)
            {
                // Some sounds have fewer knobs (drum kits), plug-ins none.
                const auto controls = trackController.effects().getSoundControls (track->id);

                if (e.index < (int) controls.size())
                {
                    startGesture (mappedControl (e), controls[(size_t) e.index].name);
                    trackController.effects().setSoundControl (track->id, e.index, e.value);
                }
            }
            break;
    }
}

} // namespace ikaros
