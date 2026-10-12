#!/usr/bin/env python3
"""Makes the tiny test sound packs the engine smoke test loads.

Everything is generated here: short sine waves, no third-party audio.

    Test Pack/                 the current pack format (2)
        pack.json
        Instruments/Test Keys/
            instrument.json    names the SFZ file, with five valid knobs
            Test Keys.sfz      two velocity layers; the loud one has two round-robin samples
            soft.wav, loud1.wav, loud2.wav

    Old Pack/                  the format before pack ids (1): it must still load
        pack.json              no id or version, credits as one line of text
        Instruments/Old Keys/
            instrument.json    simple key zones, and only four knobs (so they're ignored)
            tone.wav

The three samples are the same note at different levels (0.2, 0.8 and 0.6),
so the test can tell from how loud a note plays which sample it used.

It also makes the local pack store the app and the test download from
until real packs are hosted (see src/engine/PackStore.h):

    ../store/index.json                       made by tools/make_pack.py
    ../store/ikaros-studio.store-test-1.0.0.zip   "Store Pack", one sine-wave instrument
    ../store/index-damaged.json               the same pack with a wrong SHA-256
"""

import json
import math
import shutil
import struct
import subprocess
import sys
import tempfile
import wave
from pathlib import Path

HERE = Path(__file__).resolve().parent
PACK = HERE / "Test Pack"
INSTRUMENT = PACK / "Instruments" / "Test Keys"
OLD_PACK = HERE / "Old Pack"
OLD_INSTRUMENT = OLD_PACK / "Instruments" / "Old Keys"
STORE = HERE.parent / "store"
MAKE_PACK = HERE.parent.parent / "tools" / "make_pack.py"
RATE = 22050
SECONDS = 0.5


def write_sine(path, level):
    """A middle C sine wave at a fixed level, with short fades so it doesn't click."""
    frames = int(RATE * SECONDS)
    fade = int(RATE * 0.01)
    data = bytearray()

    for i in range(frames):
        envelope = min(1.0, i / fade, (frames - 1 - i) / fade)
        sample = level * envelope * math.sin(2 * math.pi * 261.63 * i / RATE)
        data += struct.pack("<h", int(sample * 32767))

    with wave.open(str(path), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes(bytes(data))


def main():
    INSTRUMENT.mkdir(parents=True, exist_ok=True)
    write_sine(INSTRUMENT / "soft.wav", 0.2)
    write_sine(INSTRUMENT / "loud1.wav", 0.8)
    write_sine(INSTRUMENT / "loud2.wav", 0.6)

    (INSTRUMENT / "Test Keys.sfz").write_text(
        "// Test instrument for the engine smoke test (see generate_test_pack.py).\n"
        "<global> lokey=0 hikey=127 pitch_keycenter=60 amp_veltrack=0\n"
        "ampeg_attack_oncc73=2 ampeg_release_oncc72=3\n"
        "<group> lovel=1 hivel=63\n"
        "<region> sample=soft.wav\n"
        "<group> lovel=64 hivel=127 seq_length=2\n"
        "<region> sample=loud1.wav seq_position=1\n"
        "<region> sample=loud2.wav seq_position=2\n")

    (INSTRUMENT / "instrument.json").write_text(json.dumps({
        "name": "Test Keys",
        "category": "Keyboards",
        "icon": "piano",
        "sfz": "Test Keys.sfz",
        "knobs": [
            {"name": "Tone", "control": "tone", "default": 0.5},
            {"name": "Attack", "control": "cc", "cc": 73, "default": 0.0},
            {"name": "Release", "control": "cc", "cc": 72, "default": 0.3},
            {"name": "Width", "control": "width", "default": 0.5},
            {"name": "Room", "control": "room", "default": 0.2},
        ],
    }, indent=4) + "\n")

    (PACK / "pack.json").write_text(json.dumps({
        "format": 2,
        "id": "ikaros-studio.test",
        "name": "Test Pack",
        "version": "1.2.3",
        "description": "Sine waves for the engine smoke test.",
        "author": "Ikaros Studio",
        "license": "CC0-1.0",
        "installedSize": 66282,
        "credits": [{"what": "Test tones", "who": "generate_test_pack.py", "license": "CC0-1.0",
                     "url": "", "attributionRequired": True}],
    }, indent=4) + "\n")

    # The old format: no id, no version, credits as text; a zones instrument.
    OLD_INSTRUMENT.mkdir(parents=True, exist_ok=True)
    write_sine(OLD_INSTRUMENT / "tone.wav", 0.5)
    (OLD_INSTRUMENT / "instrument.json").write_text(json.dumps({
        "name": "Old Keys",
        "category": "Keyboards",
        "samples": [{"file": "tone.wav", "root": 60, "low": 0, "high": 127}],
        "knobs": [{"name": "Tone", "control": "tone"}] * 4,
    }, indent=4) + "\n")
    (OLD_PACK / "pack.json").write_text(json.dumps({
        "name": "Old Pack",
        "description": "A pack in the format before ids.",
        "credits": "Made for the engine smoke test.",
    }, indent=4) + "\n")

    make_store()


def make_store():
    """A downloadable pack, packaged the way real ones are."""
    with tempfile.TemporaryDirectory() as tmp:
        pack = Path(tmp) / "Store Pack"
        instrument = pack / "Instruments" / "Store Keys"
        instrument.mkdir(parents=True)
        write_sine(instrument / "tone.wav", 0.7)
        (instrument / "instrument.json").write_text(json.dumps({
            "name": "Store Keys",
            "category": "Keyboards",
            "samples": [{"file": "tone.wav", "root": 60, "low": 0, "high": 127}],
        }, indent=4) + "\n")
        (pack / "pack.json").write_text(json.dumps({
            "format": 2,
            "id": "ikaros-studio.store-test",
            "name": "Store Pack",
            "version": "1.0.0",
            "description": "A tiny pack for testing downloads: one sine-wave instrument.",
            "author": "Ikaros Studio",
            "license": "CC0-1.0",
        }, indent=4) + "\n")

        shutil.rmtree(STORE, ignore_errors=True)
        subprocess.run([sys.executable, str(MAKE_PACK), str(pack), "--category", "Keyboards", "--out", str(STORE)],
                       check=True)

    index = json.loads((STORE / "index.json").read_text())
    index["packs"][0]["sha256"] = "0" * 64
    (STORE / "index-damaged.json").write_text(json.dumps(index, indent=4) + "\n")


if __name__ == "__main__":
    main()
