#!/usr/bin/env python3
"""Makes tests/fixtures/pedals/di_guitar.wav, the guitar the pedal null tests
play through each pedal: a low E, then a strummed E major chord, like an
electric guitar plugged straight into an audio interface ("DI").

It's made from scratch (plucked strings, Karplus-Strong), so it holds no
third-party samples. The same seed always gives the same file, but there's
no need to run this again: the tests and the reference renders use the
committed file.

Usage: tools/pedals/make_di_fixture.py [output.wav]
"""

import math
import random
import struct
import sys
from pathlib import Path

RATE = 48000
SECONDS = 2.0


def pluck(out, start, freq, level, rng, damping=0.996):
    """Adds one plucked string to `out`, starting at sample `start`."""
    period = RATE / freq
    n = int(period)
    frac = period - n

    # The pick's burst of noise, a little low-passed (a soft pick).
    line = [rng.uniform(-1.0, 1.0) for _ in range(n + 1)]
    for i in range(1, len(line)):
        line[i] = 0.5 * (line[i] + line[i - 1])

    pos = 0
    previous = 0.0
    for i in range(start, len(out)):
        a = line[pos]
        b = line[(pos + 1) % len(line)]
        value = a + frac * (b - a)          # fractional delay, for exact tuning
        out[i] += level * value
        averaged = damping * 0.5 * (value + previous)
        previous = value
        line[pos] = averaged
        pos = (pos + 1) % len(line)


def main():
    path = Path(sys.argv[1]) if len(sys.argv) > 1 else \
        Path(__file__).resolve().parents[2] / "tests/fixtures/pedals/di_guitar.wav"
    rng = random.Random(8)
    out = [0.0] * int(RATE * SECONDS)

    # Low E (82.4 Hz), then an E major chord strummed down over 60 ms.
    pluck(out, int(0.05 * RATE), 82.41, 0.5, rng)
    chord = [82.41, 123.47, 164.81, 207.65, 246.94, 329.63]
    for k, f in enumerate(chord):
        pluck(out, int((1.0 + 0.012 * k) * RATE), f, 0.22, rng)

    # A pickup's level: peaks around -6 dBFS.
    peak = max(abs(v) for v in out)
    out = [v * 0.5 / peak for v in out]

    write_float_wav(path, out)
    print(f"wrote {path} ({len(out)} samples at {RATE} Hz)")


def write_float_wav(path, samples):
    """A mono 32-bit float WAV."""
    data = struct.pack(f"<{len(samples)}f", *samples)
    fmt = struct.pack("<HHIIHH", 3, 1, RATE, RATE * 4, 4, 32)
    path.parent.mkdir(parents=True, exist_ok=True)
    with open(path, "wb") as f:
        f.write(b"RIFF" + struct.pack("<I", 4 + 8 + len(fmt) + 8 + len(data)) + b"WAVE")
        f.write(b"fmt " + struct.pack("<I", len(fmt)) + fmt)
        f.write(b"data" + struct.pack("<I", len(data)) + data)


if __name__ == "__main__":
    main()
