#!/usr/bin/env python3
"""Turns Ardour's controller maps into the app's controller profiles.

Ardour (https://ardour.org) ships maps for many MIDI controllers in
share/midi_maps/*.map (GPL v2 or later). This script writes one profile per
controller listed in CONTROLLERS to resources/controllers/<file>.json (the
format is described in src/engine/ControllerMap.h):

    git clone https://github.com/Ardour/ardour.git /tmp/ardour
    git -C /tmp/ardour checkout d34a51ef6ba2aaefbb4af694a99730a5554c9149
    python3 tools/controllers/convert_ardour_maps.py /tmp/ardour/share/midi_maps

Only what the app has an action for is kept: the transport, moving between
tracks, the selected track's volume and pan, and the master volume. Bindings
to Ardour's own features (markers, mixer strips by number, plug-in
parameters) are left out, and the script lists them.

Ardour's maps don't say which MIDI port a controller has (you pick the map
yourself there), so CONTROLLERS adds the port names, and for some controllers
a knob layout like the MiniLab 3's (Sound knobs, pan, reverb, master).
"""

import json
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

ARDOUR_COMMIT = "d34a51ef6ba2aaefbb4af694a99730a5554c9149"

# Ardour's functions and actions, as ours.
FUNCTIONS = {
    "transport-roll": "play",
    "transport-stop": "stop",
    "toggle-rec-enable": "record",
    "rec-enable": "record",
    "loop-toggle": "loop",
    "transport-start": "goToStart",
    "prev-bank": "previousTrack",
    "next-bank": "nextTrack",
}

ACTIONS = {
    "Transport/Roll": "play",
    "Transport/Stop": "stop",
    "Transport/ToggleRoll": "playStop",
    "Transport/Record": "record",
    "Transport/Loop": "loop",
    "Transport/Rewind": "rewind",
    "Transport/Forward": "fastForward",
    "Transport/ToggleClick": "metronome",
    "Transport/GotoStart": "goToStart",
    "Editor/select-next-route": "nextTrack",
    "Editor/select-prev-route": "previousTrack",
    "Editor/undo": "undo",
    "Editor/redo": "redo",
}

# Ardour URIs for knobs and faders: S1 is the selected track.
URIS = {
    "/bus/gain master": "masterVolume",
    "/route/gain S1": "volume",
    "/route/pandirection S1": "pan",
}

# The knob layout the MiniLab 3 has: Sound knobs 1-5, then pan, reverb, master.
def knob_layout(numbers):
    actions = [("soundKnob", 0), ("soundKnob", 1), ("soundKnob", 2), ("soundKnob", 3), ("soundKnob", 4),
               ("pan", None), ("reverbSend", None), ("masterVolume", None)]
    controls = []

    for number, (action, index) in zip(numbers, actions):
        control = {"cc": number, "type": "knob", "action": action}

        if index is not None:
            control["index"] = index

        controls.append(control)

    return controls

# map file -> our profile. "ports" end in * to match "Name" on Windows/macOS
# and "Name MIDI 1" on Linux. "knobs" are factory knob numbers (from the
# Ardour map) given the layout above.
CONTROLLERS = [
    {"map": "AKAI_MPKmini_mk3.map", "file": "akai-mpk-mini-mk3", "name": "Akai MPK mini mk3",
     "ports": ["MPK mini 3*"], "knobs": [70, 71, 72, 73, 74, 75, 76, 77],
     "notes": "Transport: press PROG CHANGE so the pads send program changes (pads 2-4: play, stop, record; 5 and 8: rewind and forward; in bank B, 12 and 16: previous and next track)."},
    {"map": "AKAI_MPKminiplus.map", "file": "akai-mpk-mini-plus", "name": "Akai MPK mini Plus",
     "ports": ["MPK mini Plus*"], "knobs": [70, 71, 72, 73, 74, 75, 76, 77],
     "notes": "Factory settings."},
    {"map": "akai-mpd-32.map", "file": "akai-mpd32", "name": "Akai MPD32",
     "ports": ["Akai MPD32*", "MPD32*"],
     "notes": "Preset 3 (Cubase) on the MPD32."},
    {"map": "Alesis_VI25.map", "file": "alesis-vi25", "name": "Alesis VI25", "ports": ["VI25*"]},
    {"map": "Alesis_QX25.map", "file": "alesis-qx25", "name": "Alesis QX25", "ports": ["QX25*"]},
    {"map": "Alesis_Q49v2.map", "file": "alesis-q49-mkii", "name": "Alesis Q49 MKII", "ports": ["Q49*"]},
    {"map": "Arturia_MiniLab_mkII.map", "file": "arturia-minilab-mkii", "name": "Arturia MiniLab mkII",
     "ports": ["Arturia MiniLab mkII*"], "knobs": [74, 71, 76, 77, 93, 73, 75],
     "notes": "Factory settings (preset 1, Analog Lab)."},
    {"map": "Arturia_KeyLab49.map", "file": "arturia-keylab-49", "name": "Arturia KeyLab 49",
     "ports": ["KeyLab 49*"]},
    {"map": "donnerdmk25.map", "file": "donner-dmk25", "name": "Donner DMK25", "ports": ["DMK25*"]},
    {"map": "Korg_nanoKONTROL2.map", "file": "korg-nanokontrol2", "name": "Korg nanoKONTROL2",
     "ports": ["nanoKONTROL2*"],
     "notes": "Factory settings (CC mode, not its DAW modes)."},
    {"map": "Korg_nanoKONTROL_Studio.map", "file": "korg-nanokontrol-studio", "name": "Korg nanoKONTROL Studio",
     "ports": ["nanoKONTROL Studio*"]},
    {"map": "Korg_taktile.map", "file": "korg-taktile", "name": "Korg taktile",
     "ports": ["taktile*"], "notes": "Mode 9 (generic MIDI)."},
    {"map": "M-Audio_Axiom25.map", "file": "m-audio-axiom-25", "name": "M-Audio Axiom 25", "ports": ["Axiom 25*"]},
    {"map": "M-Audio_Axiom_49_mkII.map", "file": "m-audio-axiom-49", "name": "M-Audio Axiom 49", "ports": ["Axiom 49*"]},
    {"map": "M-Audio_Axiom61.map", "file": "m-audio-axiom-61", "name": "M-Audio Axiom 61", "ports": ["Axiom 61*"]},
    {"map": "M-Audio_Axiom_AIR_Mini_32.map", "file": "m-audio-axiom-air-mini-32", "name": "M-Audio Axiom AIR Mini 32",
     "ports": ["Axiom AIR Mini 32*"]},
    {"map": "m-audio_oxygen25_3rdGen.map", "file": "m-audio-oxygen-25", "name": "M-Audio Oxygen 25 (3rd generation)",
     "ports": ["Oxygen 25*"]},
    {"map": "Nektar_Impact_GX.map", "file": "nektar-impact-gx", "name": "Nektar Impact GX", "ports": ["Impact GX*"]},
    {"map": "Nektar_Impact_LX.map", "file": "nektar-impact-lx", "name": "Nektar Impact LX", "ports": ["Impact LX*"]},
    {"map": "Novation_LaunchKey25.map", "file": "novation-launchkey-25", "name": "Novation Launchkey 25",
     "ports": ["Launchkey 25*"]},
    {"map": "Novation_LaunchKey49.map", "file": "novation-launchkey-49", "name": "Novation Launchkey 49",
     "ports": ["Launchkey 49*"], "notes": "The Basic MIDI preset."},
    {"map": "Novation_Impulse61.map", "file": "novation-impulse", "name": "Novation Impulse",
     "ports": ["Impulse*"]},
]


def control_from(binding):
    """One Ardour <Binding> as our control, or (None, why it's left out)."""
    a = binding.attrib
    target = a.get("function") or a.get("action") or a.get("uri")
    action = FUNCTIONS.get(a.get("function")) or ACTIONS.get(a.get("action"))
    knob = URIS.get(a.get("uri"))
    channel = int(a["channel"]) if "channel" in a else 0

    if not action and not knob:
        return None, f"no such action here: {target}"

    control = {}

    if "ctl" in a:
        control = {"cc": int(a["ctl"]), "type": "knob" if knob else "button"}
    elif "msg" in a:
        # "B0 3c 7f": a controller message, sent by a button.
        data = [int(x, 16) for x in a["msg"].split()]

        if len(data) != 3 or not 0xB0 <= data[0] <= 0xBF or knob:
            return None, f"a message we don't read: {a['msg']}"

        control = {"cc": data[1], "type": "button"}
        channel = (data[0] & 0x0F) + 1
    elif "sysex" in a and not knob:
        control = {"sysex": " ".join(x.upper() for x in a["sysex"].split())}
    elif "pgm" in a and not knob:
        control = {"pgm": int(a["pgm"])}
    else:
        kind = next((k for k in a if k not in ("channel", "function", "action", "uri")), "?")
        return None, f"a {kind} binding"

    if channel:
        control["channel"] = channel

    control["action"] = knob or action
    return control, None


def source_of(control):
    # Not the channel: our knob layout says "any channel", Ardour's map channel 1.
    return (control.get("cc"), control.get("sysex"), control.get("pgm"))


def convert(maps_dir, out_dir):
    for c in CONTROLLERS:
        path = maps_dir / c["map"]
        root = ET.parse(path).getroot()   # comments (and commented-out bindings) are dropped
        controls, seen = [], set()

        # Our knob layout first: it wins over the map's use of the same knobs.
        for control in knob_layout(c.get("knobs", [])):
            controls.append(control)
            seen.add(source_of(control))

        for binding in root.iter("Binding"):
            control, why = control_from(binding)

            if control is None:
                print(f"  {c['map']}: left out {why}")
            elif source_of(control) not in seen:
                controls.append(control)
                seen.add(source_of(control))

        about = (f"From Ardour's map for this controller ({c['map']}, Ardour commit {ARDOUR_COMMIT[:7]}, "
                 "GPL v2 or later). " + c.get("notes", "") + " Not tried on the hardware yet.")
        profile = {"name": c["name"], "about": " ".join(about.split()), "ports": c["ports"], "controls": controls}

        # One control per line, like the hand-written profiles.
        lines = ",\n".join("    " + json.dumps(x) for x in controls)
        head = json.dumps({k: v for k, v in profile.items() if k != "controls"}, indent=2)[:-2]
        (out_dir / (c["file"] + ".json")).write_text(head + ',\n  "controls": [\n' + lines + "\n  ]\n}\n")
        print(f"{c['file']}.json: {len(controls)} controls")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit(__doc__)

    convert(Path(sys.argv[1]), Path(__file__).resolve().parents[2] / "resources" / "controllers")
