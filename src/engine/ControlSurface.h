#pragma once

#include "engine/EngineTypes.h"
#include "engine/MackieControl.h"
#include <juce_audio_devices/juce_audio_devices.h>
#include <array>
#include <atomic>
#include <functional>
#include <map>
#include <memory>

namespace ikaros
{

/** A Mackie Control surface: a controller's "MCU" port (see isMackiePort),
    e.g. a MiniLab 3 in DAW mode set to Mackie Control. Its 8 channel strips
    control 8 tracks at a time; Bank and Channel move them along the list.
    Its lights and motor faders follow the session.

    Opens the first Mackie Control port it finds, and looks again whenever
    MIDI devices come or go. (The same port's notes never reach a track: see
    InputRouter::routeMidiInputs.)

    Just MIDI, no Tracktion: what a button does is up to the app (see
    SurfaceHandler), which calls update() on its timer. */
class ControlSurface  : private juce::MidiInputCallback
{
public:
    /** False for tests: no device is opened (see receiveForTests). */
    explicit ControlSurface (bool openDevices = true);
    ~ControlSurface() override;

    /** The open port's name, or empty if there's none. */
    juce::String getPortName() const;

    /** True once the surface has sent anything since its port was opened.
        (A controller's Mackie port can be there yet silent: not in Mackie mode.) */
    bool hasHeardFromSurface() const noexcept    { return heardFromSurface; }

    // A track, as the lights and motor faders show it.
    struct Strip
    {
        TrackId id = 0;
        bool selected = false, muted = false, solo = false, armed = false;
        float volume = 0.0f;   // fader position 0..1
        float pan = 0.0f;      // -1..1
    };

    // What the surface shows.
    struct State
    {
        bool playing = false, recording = false, looping = false, metronome = false;
        std::vector<Strip> tracks;   // every track, top to bottom
        float masterVolume = 0.0f;   // fader position 0..1
    };

    // Something the user pressed or moved.
    struct Action
    {
        mackie::Event event;
        TrackId track = 0;   // a strip control's track (0 if the strip has none, and for the rest)
    };

    /** Call on the message thread, often (the UI's timer): returns what was
        pressed or moved since the last call, and shows `state` on the
        surface. Bank and Channel are handled here and not returned. */
    std::vector<Action> update (const State&);

    /** The track list index of the first strip's track. */
    int getFirstTrackIndex() const noexcept      { return firstTrack; }

    /** How many faders the surface has, if fewer than Mackie Control's 8
        (the MiniLab 3 has 4): the strips are that many tracks, Bank moves
        that many, and the strips follow the selection in groups that size.
        The strips past them have no track. */
    void setNumStrips (int);
    int getNumStrips() const noexcept            { return numStrips; }

    /** Shows a group of tracks: 0 = the first getNumStrips(), and so on
        (a controller's own bank switch, see ControllerAction::faderBank). */
    void showGroup (int group);

    /** For tests: pretend the surface sent a message, and see what's sent
        back to it. */
    void receiveForTests (const juce::MidiMessage& m)   { handleIncomingMidiMessage (nullptr, m); }
    void setOutputForTests (std::function<void (const juce::MidiMessage&)> f)   { testOutput = std::move (f); }

private:
    // juce::MidiInputCallback: on the MIDI thread.
    void handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage&) override;

    void openPorts();
    void forgetWhatWasShown();
    void send (const juce::MidiMessage&);
    void show (const State&);

    // From the MIDI thread to update(), guarded by the lock.
    juce::CriticalSection lock;
    std::vector<mackie::Event> received;
    std::atomic<bool> heardFromSurface { false };   // atomic: set on the MIDI thread

    std::unique_ptr<juce::MidiInput> input;
    std::unique_ptr<juce::MidiOutput> output;
    juce::MidiDeviceListConnection deviceListConnection;
    std::function<void (const juce::MidiMessage&)> testOutput;

    int firstTrack = 0;
    int numStrips = mackie::numStrips;
    TrackId lastSelected = 0;   // to follow the selection when it changes

    // What the surface shows now, so only changes are sent.
    std::map<int, bool> lit;                                  // by note number
    std::array<int, mackie::numStrips + 1> faderShown {};     // 14-bit, -1 = unknown
    std::array<int, mackie::numStrips> ringShown {};          // -1 = unknown

    // A fader the user is moving isn't moved by us at the same time.
    std::array<bool, mackie::numStrips + 1> touched {};
    std::array<double, mackie::numStrips + 1> lastMovedMs {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ControlSurface)
};

} // namespace ikaros
