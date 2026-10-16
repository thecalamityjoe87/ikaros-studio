#!/usr/bin/env python3
"""Makes Ikaros Studio's default sound pack ("Ikaros Basics").

Every sample and loop is synthesised here from scratch, so the pack is our
own work, with no third-party samples. The sounds are released under CC0
(public domain), so anyone can use them in their music with no conditions.
Run it from the repository root:

    python3 tools/generate_sounds.py                  # the sounds and pack.json
    python3 tools/generate_sounds.py --manifest-only  # just pack.json

It writes resources/sounds/Ikaros Basics. With ffmpeg installed, the audio is
saved as FLAC (about half the size of WAV); without it, as WAV.

pack.json follows the pack format described in src/engine/SoundLibrary.h.

Only the Python standard library is used, so it's slow-ish (a minute or so),
but it only needs running when the sounds change.
"""

import json
import math
import random
import shutil
import struct
import subprocess
import sys
import tempfile
import wave
from pathlib import Path

RATE = 44100
PACK = Path("resources/sounds/Ikaros Basics")
PACK_ID = "ikaros-studio.basics"     # never changes: sessions refer to the pack by it
PACK_VERSION = "1.0.0"               # bump when the sounds change
USE_FLAC = shutil.which("ffmpeg") is not None

random.seed(87)   # same output every run


# ------------------------------------------------------------------------------
# Small helpers

def midi_hz(note):
    return 440.0 * 2.0 ** ((note - 69) / 12.0)


def note_name(note):
    names = ["C", "Cs", "D", "Ds", "E", "F", "Fs", "G", "Gs", "A", "As", "B"]
    return f"{names[note % 12]}{note // 12 - 1}"


def silence(seconds):
    return [0.0] * int(seconds * RATE)


def mix_into(dest, src, offset=0, gain=1.0):
    end = min(len(dest), offset + len(src))
    for i in range(offset, end):
        dest[i] += src[i - offset] * gain


def one_pole_lowpass(samples, cutoff_hz):
    a = math.exp(-2.0 * math.pi * cutoff_hz / RATE)
    out, y = [], 0.0
    for x in samples:
        y = (1.0 - a) * x + a * y
        out.append(y)
    return out


def one_pole_highpass(samples, cutoff_hz):
    low = one_pole_lowpass(samples, cutoff_hz)
    return [x - l for x, l in zip(samples, low)]


def fade_out(samples, seconds):
    n = min(len(samples), int(seconds * RATE))
    start = len(samples) - n
    for i in range(n):
        samples[start + i] *= 0.5 * (1.0 + math.cos(math.pi * i / n))
    return samples


def fade_in(samples, seconds):
    n = min(len(samples), max(1, int(seconds * RATE)))
    for i in range(n):
        samples[i] *= i / n
    return samples


def normalise(samples, peak=0.7):
    biggest = max(1e-9, max(abs(s) for s in samples))
    return [s * peak / biggest for s in samples]


def write_audio(path, samples):
    """Writes 16-bit mono, as FLAC if ffmpeg is available. Returns the file name used."""
    path.parent.mkdir(parents=True, exist_ok=True)
    frames = b"".join(struct.pack("<h", max(-32767, min(32767, int(s * 32767)))) for s in samples)

    if not USE_FLAC:
        target = path.with_suffix(".wav")
        with wave.open(str(target), "wb") as w:
            w.setnchannels(1)
            w.setsampwidth(2)
            w.setframerate(RATE)
            w.writeframes(frames)
        return target.name

    target = path.with_suffix(".flac")
    with tempfile.TemporaryDirectory() as tmp:
        wav = Path(tmp) / "x.wav"
        with wave.open(str(wav), "wb") as w:
            w.setnchannels(1)
            w.setsampwidth(2)
            w.setframerate(RATE)
            w.writeframes(frames)
        subprocess.run(["ffmpeg", "-loglevel", "error", "-y", "-i", str(wav),
                        "-compression_level", "8", str(target)], check=True)
    return target.name


# ------------------------------------------------------------------------------
# Instruments

def piano_note(note, seconds):
    """Two slightly detuned 'strings' of decaying, slightly stretched partials, plus a hammer knock."""
    f0 = midi_hz(note)
    n = int(seconds * RATE)
    out = [0.0] * n
    inharmonicity = 0.00025 * (f0 / 261.6) ** 0.5
    sustain = min(4.0, 2.6 * (261.6 / f0) ** 0.45)

    for detune in (0.9993, 1.0007):
        partial = 1
        while True:
            f = partial * f0 * detune * math.sqrt(1.0 + inharmonicity * partial * partial)
            if f > 9000 or partial > 24:
                break
            amp = 1.0 / partial ** 1.25 * (1.0 if partial > 1 else 1.3)
            fast = sustain * 0.12 / (1.0 + 0.3 * partial)
            slow = sustain / (1.0 + 0.45 * (partial - 1))
            # A rotating phasor is much cheaper than calling sin() per sample.
            c, s = math.cos(2 * math.pi * f / RATE), math.sin(2 * math.pi * f / RATE)
            x, y = 0.0, 1.0
            k_fast, k_slow = math.exp(-1.0 / (fast * RATE)), math.exp(-1.0 / (slow * RATE))
            e_fast, e_slow = 0.6, 0.4
            for i in range(n):
                out[i] += amp * y * (e_fast + e_slow)
                x, y = x * c - y * s, x * s + y * c
                e_fast *= k_fast
                e_slow *= k_slow
            partial += 1

    knock = one_pole_lowpass([random.uniform(-1, 1) * math.exp(-i / (0.004 * RATE)) for i in range(int(0.03 * RATE))], 2500)
    mix_into(out, knock, 0, 0.6)
    return fade_out(fade_in(normalise(out), 0.002), 0.25)


def electric_piano_note(note, seconds):
    """Two-operator FM with a short metallic 'tine' at the start."""
    f0 = midi_hz(note)
    n = int(seconds * RATE)
    decay = min(3.5, 1.9 * (261.6 / f0) ** 0.4)
    out = []
    w = 2 * math.pi * f0 / RATE
    tine = min(14.0 * f0, 12000.0)
    wt = 2 * math.pi * tine / RATE
    for i in range(n):
        t = i / RATE
        index = 1.6 * math.exp(-t / 0.45) + 0.25
        body = math.sin(w * i + index * math.sin(w * i)) * math.exp(-t / decay)
        bell = 0.12 * math.sin(wt * i) * math.exp(-t / 0.025)
        out.append(body + bell)
    return fade_out(fade_in(normalise(out), 0.002), 0.25)


def karplus_strong(note, seconds, brightness, decay, pluck_position=0.2, body=0.0):
    """A plucked string: a noise burst circulating in a delay line that loses a little high end each pass."""
    f0 = midi_hz(note)
    period = RATE / f0
    length = int(period)
    frac = period - length
    excitation = one_pole_lowpass([random.uniform(-1, 1) for _ in range(length)], 400 + brightness * 9000)
    # Picking away from the end of the string removes some harmonics (a comb filter).
    offset = max(1, int(length * pluck_position))
    excitation = [excitation[i] - 0.8 * excitation[i - offset] if i >= offset else excitation[i] for i in range(length)]
    line = excitation[:]
    n = int(seconds * RATE)
    out = []
    # How much each pass keeps, so the note fades over roughly `decay` seconds.
    loss = math.exp(-period / (decay * RATE) * 3.0)
    previous = 0.0
    pos = 0
    for i in range(n):
        current = line[pos]
        nxt = line[(pos + 1) % length]
        value = current + frac * (nxt - current)    # fractional delay keeps it in tune
        filtered = loss * (0.5 * value + 0.5 * previous)
        previous = value
        line[pos] = filtered
        out.append(value)
        pos = (pos + 1) % length
    if body > 0:
        resonance = one_pole_lowpass(out, 220)
        out = [o + body * r for o, r in zip(out, resonance)]
    return fade_out(fade_in(normalise(out), 0.001), 0.15)


# ------------------------------------------------------------------------------
# Drums

def noise(seconds):
    return [random.uniform(-1, 1) for _ in range(int(seconds * RATE))]


def envelope(samples, decay, attack=0.0005):
    out = []
    for i, s in enumerate(samples):
        t = i / RATE
        a = min(1.0, t / attack) if attack > 0 else 1.0
        out.append(s * a * math.exp(-t / decay))
    return out


def kick(low=48.0, high=160.0, decay=0.42, click=0.25):
    n = int(decay * 3.5 * RATE)
    out, phase = [], 0.0
    for i in range(n):
        t = i / RATE
        f = low + (high - low) * math.exp(-t / 0.035)
        phase += 2 * math.pi * f / RATE
        out.append(math.sin(phase) * math.exp(-t / decay))
    mix_into(out, envelope(one_pole_lowpass(noise(0.01), 3000), 0.002), 0, click)
    return fade_out(normalise(out, 0.9), 0.05)


def snare(tone=185.0, decay=0.18, brightness=6000.0):
    n = int(decay * 3.5 * RATE)
    body = [math.sin(2 * math.pi * tone * i / RATE) * 0.7 + math.sin(2 * math.pi * tone * 1.8 * i / RATE) * 0.3 for i in range(n)]
    body = envelope(body, 0.06)
    rattle = envelope(one_pole_lowpass(one_pole_highpass(noise(n / RATE), 1200), brightness), decay)
    out = [b * 0.6 + r for b, r in zip(body, rattle)]
    return fade_out(normalise(out, 0.8), 0.05)


def clap():
    out = silence(0.4)
    for k, gap in enumerate((0.0, 0.011, 0.023)):
        burst = envelope(one_pole_highpass(noise(0.03), 800), 0.008)
        mix_into(out, burst, int(gap * RATE), 0.7)
    tail = envelope(one_pole_lowpass(one_pole_highpass(noise(0.37), 900), 3500), 0.12)
    mix_into(out, tail, int(0.03 * RATE), 0.6)
    return fade_out(normalise(out, 0.75), 0.05)


def metal(seconds, decay, cutoff=7000.0):
    """Six square waves at awkward ratios, the classic drum-machine cymbal."""
    freqs = [205.3, 304.4, 369.6, 522.7, 540.0, 800.0]
    n = int(seconds * RATE)
    out = []
    for i in range(n):
        v = sum(1.0 if math.sin(2 * math.pi * f * 1.7 * i / RATE) > 0 else -1.0 for f in freqs)
        out.append(v / 6)
    out = one_pole_highpass(out, cutoff)
    mix_into(out, one_pole_highpass(noise(seconds), cutoff), 0, 0.5)
    return envelope(out, decay)


def hat(decay):
    seconds = min(1.2, decay * 5)
    return fade_out(normalise(metal(seconds, decay), 0.5), 0.02)


def cymbal(decay):
    return fade_out(normalise(metal(decay * 2.5, decay, 5000.0), 0.55), 0.2)


def tom(freq, decay=0.32):
    n = int(decay * 3.5 * RATE)
    out, phase = [], 0.0
    for i in range(n):
        t = i / RATE
        f = freq * (1.0 + 0.35 * math.exp(-t / 0.05))
        phase += 2 * math.pi * f / RATE
        out.append(math.sin(phase) * math.exp(-t / decay))
    return fade_out(normalise(out, 0.8), 0.05)


def rim():
    n = int(0.06 * RATE)
    out = [math.sin(2 * math.pi * 1700 * i / RATE) * math.exp(-i / (0.008 * RATE)) for i in range(n)]
    mix_into(out, envelope(one_pole_highpass(noise(0.02), 2000), 0.003), 0, 0.5)
    return normalise(out, 0.6)


def cowbell():
    n = int(0.35 * RATE)
    out = []
    for i in range(n):
        a = 1.0 if math.sin(2 * math.pi * 540 * i / RATE) > 0 else -1.0
        b = 1.0 if math.sin(2 * math.pi * 800 * i / RATE) > 0 else -1.0
        out.append((a + b) * 0.5 * math.exp(-i / (0.09 * RATE)))
    return fade_out(normalise(one_pole_lowpass(out, 3500), 0.55), 0.03)


def crush(samples, bits=7, hold=3):
    """Lo-fi: fewer bits and a lower sample rate."""
    steps = 2 ** (bits - 1)
    out, held = [], 0.0
    for i, s in enumerate(samples):
        if i % hold == 0:
            held = round(s * steps) / steps
        out.append(held)
    return one_pole_lowpass(out, 6000)


def electronic_kit():
    return {
        36: ("Kick", kick()),
        37: ("Rim", rim()),
        38: ("Snare", snare()),
        39: ("Clap", clap()),
        41: ("Low Tom", tom(98)),
        42: ("Closed Hat", hat(0.035)),
        43: ("Low Tom 2", tom(110)),
        44: ("Pedal Hat", [s * 0.7 for s in hat(0.025)]),
        45: ("Mid Tom", tom(140)),
        46: ("Open Hat", hat(0.22)),
        47: ("Mid Tom 2", tom(160)),
        48: ("High Tom", tom(195)),
        49: ("Crash", cymbal(0.7)),
        50: ("High Tom 2", tom(220)),
        51: ("Ride", cymbal(0.45)),
        56: ("Cowbell", cowbell()),
    }


def lofi_kit():
    return {
        36: ("Kick", crush(kick(low=44, high=110, decay=0.5, click=0.1))),
        37: ("Rim", crush(rim())),
        38: ("Snare", crush(snare(tone=170, decay=0.14, brightness=3500))),
        39: ("Clap", crush(clap())),
        42: ("Closed Hat", crush(hat(0.03))),
        44: ("Pedal Hat", crush([s * 0.7 for s in hat(0.02)])),
        46: ("Open Hat", crush(hat(0.16))),
        49: ("Crash", crush(cymbal(0.5))),
    }


# ------------------------------------------------------------------------------
# Writing instruments and loops

def write_instrument(name, category, zones, one_shot=False):
    """zones: list of (root, low, high, samples, file stem)."""
    folder = PACK / "Instruments" / name
    if folder.exists():
        shutil.rmtree(folder)
    entries = []
    for root, low, high, samples, stem in zones:
        file_name = write_audio(folder / stem, samples)
        entries.append({"file": file_name, "root": root, "low": low, "high": high})
    description = {"name": name, "category": category, "oneShot": one_shot, "samples": entries}
    (folder / "instrument.json").write_text(json.dumps(description, indent=2) + "\n")
    print(f"  {name}: {len(entries)} samples")


def pitched_zones(roots, make):
    """One sample per root note, each covering the keys halfway to its neighbours."""
    zones = []
    for i, root in enumerate(roots):
        low = 0 if i == 0 else (roots[i - 1] + root) // 2 + 1
        high = 127 if i == len(roots) - 1 else (root + roots[i + 1]) // 2
        zones.append((root, low, high, make(root), note_name(root)))
    return zones


def drum_zones(kit):
    return [(note, note, note, samples, f"{note:02d} {label}") for note, (label, samples) in sorted(kit.items())]


def bars_to_samples(bars, bpm):
    return int(bars * 4 * 60.0 / bpm * RATE)


def beat_to_sample(beat, bpm):
    return int(beat * 60.0 / bpm * RATE)


def write_loops():
    folder = PACK / "Loops"
    if folder.exists():
        shutil.rmtree(folder)
    bpm = 120
    length = bars_to_samples(2, bpm)
    kit = electronic_kit()
    loops = []

    # Electro Beat: four-on-the-floor kick, claps on 2 and 4, eighth-note hats.
    beat = [0.0] * length
    for b in range(8):
        mix_into(beat, kit[36][1], beat_to_sample(b, bpm), 0.9)
        if b % 2 == 1:
            mix_into(beat, kit[39][1], beat_to_sample(b, bpm), 0.6)
    for e in range(16):
        mix_into(beat, kit[46 if e % 4 == 3 else 42][1], beat_to_sample(e * 0.5, bpm), 0.35 if e % 2 else 0.25)
    loops.append(("Electro Beat", "Electro Beat", beat, "C"))

    # Hat Groove: sixteenth hats with accents and a little swing.
    hats = [0.0] * length
    for s in range(32):
        swing = 0.04 if s % 2 else 0.0
        accent = 0.45 if s % 4 == 0 else (0.3 if s % 2 == 0 else 0.18)
        mix_into(hats, kit[42][1], beat_to_sample(s * 0.25 + swing, bpm), accent)
    mix_into(hats, kit[56][1], beat_to_sample(1.5, bpm), 0.25)
    mix_into(hats, kit[56][1], beat_to_sample(5.5, bpm), 0.25)
    loops.append(("Hat Groove", "Hat Groove", hats, None))

    # Pluck Bass: a two-bar line in C.
    bass = [0.0] * length
    line = [(0, 36, 0.75), (0.75, 36, 0.5), (1.5, 48, 0.5), (2.0, 36, 1.0), (3.0, 34, 1.0),
            (4.0, 31, 0.75), (4.75, 31, 0.5), (5.5, 43, 0.5), (6.0, 34, 1.0), (7.0, 35, 1.0)]
    for start, note, dur in line:
        pluck = karplus_strong(note, dur * 0.5 + 0.1, brightness=0.25, decay=0.6)
        mix_into(bass, pluck, beat_to_sample(start, bpm), 0.8)
    loops.append(("Pluck Bass", "Pluck Bass", bass, "C"))

    # Glass Chords: electric piano, Cmaj7 to Am7.
    chords = [0.0] * length
    for start, notes in ((0, (48, 55, 59, 64)), (2.5, (48, 55, 59, 64)), (4, (45, 52, 55, 60)), (6.5, (45, 52, 55, 60))):
        for note in notes:
            mix_into(chords, electric_piano_note(note, 1.4), beat_to_sample(start, bpm), 0.28)
    loops.append(("Glass Chords", "Glass Chords", chords, "C"))

    # What the loop browser's instrument, genre and mood filters look for.
    loop_tags = {
        "Electro Beat": ["drums", "electronic", "energetic"],
        "Hat Groove": ["drums", "percussion", "funk"],
        "Pluck Bass": ["bass", "electronic"],
        "Glass Chords": ["keys", "piano", "chill"],
    }

    entries = []
    for stem, name, samples, key in loops:
        file_name = write_audio(folder / stem, fade_out(normalise(samples, 0.8), 0.01))
        entry = {"file": file_name, "name": name, "bpm": bpm}
        if key:
            entry["key"] = key
        entry["tags"] = loop_tags[name]
        entries.append(entry)
        print(f"  loop: {name}")
    (folder / "loops.json").write_text(json.dumps(entries, indent=2) + "\n")


def write_manifest():
    """pack.json: who made the pack and under what license, its version, and
    its size once installed (every file except pack.json itself)."""
    size = sum(f.stat().st_size for f in PACK.rglob("*") if f.is_file() and f.name != "pack.json")

    (PACK / "pack.json").write_text(json.dumps({
        "format": 2,
        "id": PACK_ID,
        "name": "Ikaros Basics",
        "version": PACK_VERSION,
        "description": "The sounds that come with Ikaros Studio.",
        "author": "Ikaros Studio",
        "license": "CC0-1.0",
        "installedSize": size,
        "credits": [
            {
                "what": "Every instrument and loop",
                "who": "Synthesized for Ikaros Studio by tools/generate_sounds.py",
                "license": "CC0-1.0",
                "url": "https://github.com/thecalamityjoe87/ikaros-studio",
                "attributionRequired": False,
            }
        ],
    }, indent=4) + "\n")


def main():
    if not Path("CMakeLists.txt").exists():
        sys.exit("Run this from the repository root.")

    if "--manifest-only" in sys.argv:
        write_manifest()
        print(f"Wrote {PACK / 'pack.json'}")
        return

    print(f"Writing {PACK} ({'FLAC' if USE_FLAC else 'WAV'})")
    PACK.mkdir(parents=True, exist_ok=True)

    write_instrument("Soft Piano", "Keyboards",
                     pitched_zones([36, 48, 60, 72, 84], lambda n: piano_note(n, 3.0 if n < 60 else 2.2)))
    write_instrument("Electric Piano", "Keyboards",
                     pitched_zones([36, 48, 60, 72, 84], lambda n: electric_piano_note(n, 2.4)))
    write_instrument("Electronic Kit", "Drums", drum_zones(electronic_kit()), one_shot=True)
    write_instrument("Lo-fi Kit", "Drums", drum_zones(lofi_kit()), one_shot=True)
    write_instrument("Picked Bass", "Bass",
                     pitched_zones([28, 40, 52], lambda n: karplus_strong(n, 2.0, brightness=0.3, decay=1.4, pluck_position=0.12, body=0.3)))
    write_instrument("Nylon Guitar", "Guitar",
                     pitched_zones([40, 47, 55, 62, 69], lambda n: karplus_strong(n, 2.4, brightness=0.35, decay=1.6, body=0.4)))
    write_instrument("Clean Electric", "Guitar",
                     pitched_zones([40, 47, 55, 62, 69], lambda n: karplus_strong(n, 2.4, brightness=0.75, decay=2.2, pluck_position=0.1)))
    write_instrument("Pizzicato", "Strings",
                     pitched_zones([43, 50, 57, 64, 71], lambda n: karplus_strong(n, 1.0, brightness=0.2, decay=0.35, pluck_position=0.3, body=0.5)))
    write_loops()
    write_manifest()
    print("Done.")


if __name__ == "__main__":
    main()
