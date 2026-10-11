#!/usr/bin/env python3
"""Makes the tiny test sound pack the engine smoke test loads.

Everything is generated here: short sine waves, no third-party audio.

    Test Pack/Instruments/Test Keys/
        instrument.json    names the SFZ file
        Test Keys.sfz      two velocity layers; the loud one has two round-robin samples
        soft.wav, loud1.wav, loud2.wav

The three samples are the same note at different levels (0.2, 0.8 and 0.6),
so the test can tell from how loud a note plays which sample it used.
"""

import json
import math
import struct
import wave
from pathlib import Path

HERE = Path(__file__).resolve().parent
INSTRUMENT = HERE / "Test Pack" / "Instruments" / "Test Keys"
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
        "<group> lovel=1 hivel=63\n"
        "<region> sample=soft.wav\n"
        "<group> lovel=64 hivel=127 seq_length=2\n"
        "<region> sample=loud1.wav seq_position=1\n"
        "<region> sample=loud2.wav seq_position=2\n")

    (INSTRUMENT / "instrument.json").write_text(
        json.dumps({"name": "Test Keys", "category": "Keyboards", "sfz": "Test Keys.sfz"}, indent=4) + "\n")


if __name__ == "__main__":
    main()
