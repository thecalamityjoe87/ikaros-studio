#include "engine/ControlSurface.h"
#include "engine/MidiPorts.h"

namespace ikaros
{

namespace
{
    // How long after the user last moved a fader before the session may
    // move it again (a fader without touch sensing can't say it's let go).
    constexpr double faderSettleMs = 500.0;

    int fourteenBits (float position)
    {
        return juce::jlimit (0, 0x3fff, juce::roundToInt (position * 0x3fff));
    }
}

ControlSurface::ControlSurface (bool openDevices)
{
    forgetWhatWasShown();

    if (openDevices)
    {
        // Called on the message thread whenever a MIDI device comes or goes.
        deviceListConnection = juce::MidiDeviceListConnection::make ([this] { openPorts(); });
        openPorts();
    }
}

ControlSurface::~ControlSurface()
{
    if (input != nullptr)
        input->stop();   // no more callbacks after this
}

juce::String ControlSurface::getPortName() const
{
    return input != nullptr ? input->getName().trim() : juce::String();
}

void ControlSurface::openPorts()
{
    juce::MidiDeviceInfo found;

    for (auto& device : juce::MidiInput::getAvailableDevices())
        if (isMackiePort (device.name))
        {
            found = device;
            break;
        }

    if (input != nullptr && input->getIdentifier() == found.identifier)
        return;   // still there

    if (input != nullptr)
        input->stop();

    input.reset();
    output.reset();

    if (found.identifier.isEmpty())
        return;

    input = juce::MidiInput::openDevice (found.identifier, this);

    if (input == nullptr)
        return;

    // The output port of the same name, for the lights and motor faders.
    for (auto& device : juce::MidiOutput::getAvailableDevices())
        if (device.name == found.name)
            output = juce::MidiOutput::openDevice (device.identifier);

    forgetWhatWasShown();   // a surface that was just plugged in shows nothing yet
    heardFromSurface = false;
    input->start();
}

void ControlSurface::forgetWhatWasShown()
{
    lit.clear();
    faderShown.fill (-1);
    ringShown.fill (-1);
    touched.fill (false);
}

void ControlSurface::handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage& m)
{
    heardFromSurface = true;

    if (auto event = mackie::decode (m))
    {
        const juce::ScopedLock sl (lock);
        received.push_back (*event);
    }
}

void ControlSurface::send (const juce::MidiMessage& m)
{
    if (testOutput)
        testOutput (m);
    else if (output != nullptr)
        output->sendMessageNow (m);
}

void ControlSurface::setNumStrips (int n)
{
    numStrips = juce::jlimit (1, mackie::numStrips, n);
}

void ControlSurface::showGroup (int group)
{
    firstTrack = juce::jmax (0, group) * numStrips;
}

std::vector<ControlSurface::Action> ControlSurface::update (const State& state)
{
    std::vector<mackie::Event> events;

    {
        const juce::ScopedLock sl (lock);
        std::swap (events, received);   // take them all, leaving it empty
    }

    const int numTracks = (int) state.tracks.size();
    std::vector<Action> actions;

    // A newly selected track the strips don't show: show its group. (Not
    // on every update, so Bank and Channel can still move away from it.)
    for (int i = 0; i < numTracks; ++i)
    {
        if (state.tracks[(size_t) i].selected && state.tracks[(size_t) i].id != lastSelected)
        {
            lastSelected = state.tracks[(size_t) i].id;

            if (i < firstTrack || i >= firstTrack + numStrips)
                firstTrack = (i / numStrips) * numStrips;
        }
    }

    for (auto& e : events)
    {
        using T = mackie::Event::Type;

        // Bank moves the strips 8 tracks along, Channel one. The first
        // strip can go as far as the last track, so a surface with fewer
        // faders (the MiniLab has 4) can still reach every track.
        const int move = e.type == T::bankLeft ? -numStrips : e.type == T::bankRight ? numStrips
                       : e.type == T::channelLeft ? -1 : e.type == T::channelRight ? 1 : 0;

        if (move != 0)
        {
            firstTrack = juce::jlimit (0, juce::jmax (0, numTracks - 1), firstTrack + move);
            continue;
        }

        if (e.type == T::fader)
        {
            // The surface's own fader is where the user put it. A motor
            // fader that isn't told so would spring back.
            faderShown[(size_t) e.strip] = fourteenBits (e.position);
            send (mackie::faderPosition (e.strip, e.position));
            lastMovedMs[(size_t) e.strip] = juce::Time::getMillisecondCounterHiRes();
        }
        else if (e.type == T::faderTouch)
        {
            touched[(size_t) e.strip] = e.pressed;
        }

        Action a;
        a.event = e;

        if (e.strip >= 0 && e.strip < numStrips)
        {
            const int index = firstTrack + e.strip;

            if (index < numTracks)
                a.track = state.tracks[(size_t) index].id;
        }

        actions.push_back (a);
    }

    // Tracks may have been deleted (or a bank chosen past the last track):
    // back to the last group with a track in it.
    if (firstTrack >= numTracks)
        firstTrack = numTracks > 0 ? ((numTracks - 1) / numStrips) * numStrips : 0;
    show (state);
    return actions;
}

void ControlSurface::show (const State& state)
{
    auto setLight = [this] (int number, bool on)
    {
        auto shown = lit.find (number);

        if (shown == lit.end() || shown->second != on)
        {
            lit[number] = on;
            send (mackie::light (number, on));
        }
    };

    namespace note = mackie::note;
    setLight (note::play, state.playing);
    setLight (note::stop, ! state.playing);
    setLight (note::record, state.recording);
    setLight (note::cycle, state.looping);
    setLight (note::click, state.metronome);

    const double now = juce::Time::getMillisecondCounterHiRes();

    auto setFader = [&] (int strip, float position)
    {
        const auto s = (size_t) strip;

        if (touched[s] || now - lastMovedMs[s] < faderSettleMs)
            return;

        // A couple of steps' difference is just rounding. (-1: not shown yet.)
        if (const int value = fourteenBits (position); faderShown[s] < 0 || std::abs (value - faderShown[s]) > 2)
        {
            faderShown[s] = value;
            send (mackie::faderPosition (strip, position));
        }
    };

    for (int i = 0; i < mackie::numStrips; ++i)
    {
        const int index = firstTrack + i;
        const auto* track = i < numStrips && index < (int) state.tracks.size() ? &state.tracks[(size_t) index] : nullptr;
        Strip none;
        const auto& s = track != nullptr ? *track : none;

        setLight (note::recArm + i, s.armed);
        setLight (note::solo + i, s.solo);
        setLight (note::mute + i, s.muted);
        setLight (note::select + i, s.selected);
        setFader (i, s.volume);

        // 0 = ring off, else 1-11 (see mackie::panRing).
        const auto ring = track != nullptr ? mackie::panRing (i, s.pan) : mackie::ringOff (i);

        if (ring.getControllerValue() != ringShown[(size_t) i])
        {
            ringShown[(size_t) i] = ring.getControllerValue();
            send (ring);
        }
    }

    setFader (mackie::masterStrip, state.masterVolume);
}

} // namespace ikaros
