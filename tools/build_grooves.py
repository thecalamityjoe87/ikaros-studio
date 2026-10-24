#!/usr/bin/env python3
"""Builds the Drummer's groove library from the Groove MIDI Dataset.

    python3 -I tools/build_grooves.py path/to/groove

The Groove MIDI Dataset (GMD) is about 13 hours of drumming recorded by ten
drummers on a Roland TD-11 electronic kit, published by Google's Magenta
team under the Creative Commons Attribution 4.0 license:

    https://magenta.tensorflow.org/datasets/groove

Download the MIDI-only version (groove-v1.0.0-midionly.zip, about 3 MB;
the audio isn't needed) and unzip it anywhere. The folder you pass is the
one holding info.csv and the drummer1 ... drummer10 folders. Nothing here
needs a third-party Python package.

The script writes resources/drummer/grooves.json, which the app compiles in
(see src/engine/GrooveLibrary.h for how it's read). The dataset itself is not
part of the repository; only this library made from it is.

What it does:
  - keeps the 4/4 files of the styles the five drummers use, with each
    file's style, beat or fill, tempo, time signature and drummer
  - maps the TD-11's notes to the app's drum pieces, grouping them the way
    the dataset's documentation does (rim shots and cross-stick count as
    snare, both crashes as crash...), except that pedal and open hi-hat
    and the ride bell are kept apart, because the app plays them differently
  - keeps every note's velocity and its human timing, in beats
  - cuts each beat performance into one-bar slices (kept in order, so the
    app can play several bars of one performance in a row), and each fill
    into half-bar and one-bar slices. Bars of beat performances that end
    in a fill become one-bar fills instead.
  - keeps only the beat bars that play their style: the snare's backbeat
    on 2 and 4, the kick on (or just before) the downbeat, steady
    time-keeping, few toms, and for rock, punk, pop and dance a straight
    (not swung) feel. The dataset's drummers often improvise, and those
    bars don't sound like the style they're filed under. Crashes are taken
    out of beat bars too: the app adds them where they belong (on a new
    section, after a fill).
  - scores every slice's complexity (how many notes, how syncopated, how
    many pieces) and loudness (its velocities), each normalised 0..1
    within its drummer by rank, so the sliders spread evenly
"""

import csv
import json
import statistics
import struct
import sys
from collections import Counter, defaultdict
from pathlib import Path

OUT = Path("resources/drummer/grooves.json")
FORMAT = 1
TICKS = 480                 # per beat (quarter note) in the library
BAR = 4 * TICKS

# Our drum pieces, in the order the app numbers them (DrumPiece in
# src/engine/DrummerTypes.h).
PIECES = ["kick", "snare", "hihatClosed", "hihatPedal", "hihatOpen", "crash",
          "ride", "rideBell", "tomHigh", "tomMid", "tomFloor"]

# The Roland TD-11's notes, as the dataset documents them, and the piece
# each one plays here.
TD11 = {
    36: "kick",
    38: "snare", 40: "snare", 37: "snare",      # head, rim, cross-stick
    48: "tomHigh", 50: "tomHigh",               # tom 1: head, rim
    45: "tomMid", 47: "tomMid",                 # tom 2: head, rim
    43: "tomFloor", 58: "tomFloor",             # tom 3: head, rim
    46: "hihatOpen", 26: "hihatOpen",           # open: bow, edge
    42: "hihatClosed", 22: "hihatClosed",       # closed: bow, edge
    44: "hihatPedal",
    49: "crash", 55: "crash", 57: "crash", 52: "crash",   # crash 1 and 2: bow, edge
    51: "ride", 59: "ride",                     # bow, edge
    53: "rideBell",
}

TOMS = {"tomHigh", "tomMid", "tomFloor"}
KICK_SNARE = {"kick", "snare"}

# The five drummers: their dataset styles, the subdivision their swing
# moves (8 = eighths, 16 = sixteenths), and their kit (a library sound id).
KIT = "sampler:ikaros-studio.avl-red-zeppelin/Red Zeppelin Kit"
DRUMMERS = [
    {"id": "rock", "name": "Rock", "styles": ["rock", "punk"], "swing": 8,
     "description": "Straight-ahead rock and punk, from a steady eighth-note beat to driving tom fills."},
    {"id": "pop", "name": "Pop", "styles": ["pop", "dance"], "swing": 16,
     "description": "Tight, catchy pop and dance grooves that sit nicely under a vocal."},
    {"id": "funk-soul", "name": "Funk & Soul", "styles": ["funk", "soul", "gospel"], "swing": 16,
     "description": "Ghost notes, busy sixteenths and a deep pocket, from funk, soul and gospel."},
    {"id": "hiphop", "name": "Hip-Hop", "styles": ["hiphop"], "swing": 16,
     "description": "Laid-back hip-hop beats with a heavy kick and a crisp snare."},
    {"id": "songwriter", "name": "Songwriter", "styles": ["country", "blues"], "swing": 8,
     "description": "Simple, gentle beats for songs: country, blues and the softer side of pop and rock."},
]

# Songwriter also gets the softest, simplest stretches of pop and rock
# beats: bars in the lowest SOFT_SHARE of their style for complexity plus
# loudness, in runs of at least SOFT_MIN_RUN bars.
SOFT_STYLES = ["pop", "rock"]
SOFT_SHARE = 0.35
SOFT_MIN_RUN = 2

# Styles whose beats are straight: their swung bars are left out.
STRAIGHT_STYLES = {"rock", "punk", "pop", "dance"}

# What makes a stretch a fill: snare and tom hits while the time-keeping
# (hi-hat and ride; the pedal hat often carries on) mostly stops.
TIMEKEEPERS = {"hihatClosed", "hihatOpen", "ride", "rideBell"}


def is_fill_half(notes):
    """A half bar that's a fill: at least three snare or tom hits (one of
    them a tom, or four snares) and at most one hi-hat or ride note."""
    toms = sum(1 for p, _, _ in notes if p in TOMS)
    snares = sum(1 for p, _, _ in notes if p == "snare")
    keeping = sum(1 for p, _, _ in notes if p in TIMEKEEPERS)
    return toms + snares >= 3 and keeping <= 1 and (toms >= 1 or snares >= 4)


def is_fill_bar(notes, second_half):
    """A bar that ends in a fill (its second half is one) with at least
    five snare or tom hits in all, or a bar of toms (four or more) with
    little time-keeping (a beat played on the floor tom isn't a fill)."""
    hits = sum(1 for p, _, _ in notes if p in TOMS or p == "snare")
    toms = sum(1 for p, _, _ in notes if p in TOMS)
    keeping = sum(1 for p, _, _ in notes if p in TIMEKEEPERS)
    return (is_fill_half(second_half) and hits >= 5) or (toms >= 4 and keeping <= 4)


def is_whole_fill(first_half, second_half):
    """A bar of fill all the way through (what the Drummer plays as a
    one-bar fill): its second half is a fill, and its first half already
    has snare or tom hits while the time-keeping mostly stops."""
    hits = sum(1 for p, _, _ in first_half if p in TOMS or p == "snare")
    keeping = sum(1 for p, _, _ in first_half if p in TIMEKEEPERS)
    return is_fill_half(second_half) and hits >= 2 and keeping <= 2


def sixteenth_of(tick):
    return round(tick / (TICKS / 4)) % 16


def plays_style(notes, style):
    """Whether a beat bar plays its style's basics (see the top)."""
    snares = [v for p, _, v in notes if p == "snare"]
    loudest = max(snares) if snares else 0
    backbeats = {sixteenth_of(t) for p, t, v in notes if p == "snare" and v >= 0.6 * loudest}
    backbeat = bool(backbeats & {3, 4, 5}) and bool(backbeats & {11, 12, 13})
    kick_on_one = any(p == "kick" and sixteenth_of(t) in (0, 15) for p, t, _ in notes)

    # Time-keeping on most of the eighths, or on every beat.
    eighths = {sixteenth_of(t) // 2 for p, t, _ in notes if p in TIMEKEEPERS and sixteenth_of(t) % 2 == 0}
    beats = {sixteenth_of(t) // 4 for p, t, _ in notes if p in TIMEKEEPERS and sixteenth_of(t) % 4 == 0}
    steady = len(eighths) >= 6 or len(beats) == 4

    # Swung: the off-beat time-keeping comes late in the beat (straight
    # eighths are halfway; a triplet feel two thirds of the way).
    offbeats = [(t % TICKS) / TICKS for p, t, _ in notes if p in TIMEKEEPERS and 0.3 <= (t % TICKS) / TICKS < 0.9]
    swung = bool(offbeats) and statistics.mean(offbeats) > 0.6

    toms = sum(1 for p, _, _ in notes if p in TOMS)
    return backbeat and kick_on_one and steady and toms <= 1 and not (style in STRAIGHT_STYLES and swung)


# ------------------------------------------------------------------------------
# Reading MIDI files (just what the dataset uses: notes, tempo, time signature)

def read_vlq(data, i):
    value = 0
    while True:
        byte = data[i]
        i += 1
        value = (value << 7) | (byte & 0x7F)
        if not byte & 0x80:
            return value, i


def read_midi(path):
    """Returns (ticks per beat, [(tick, note, velocity)], [bpm], [(num, den)])."""
    data = path.read_bytes()
    if data[:4] != b"MThd":
        sys.exit(f"{path}: not a MIDI file")
    header_length, _, track_count, division = struct.unpack(">IHHH", data[4:14])
    i = 8 + header_length
    notes, tempos, signatures = [], [], []

    for _ in range(track_count):
        if data[i:i + 4] != b"MTrk":
            sys.exit(f"{path}: damaged track")
        length = struct.unpack(">I", data[i + 4:i + 8])[0]
        j, end, tick, status = i + 8, i + 8 + length, 0, 0

        while j < end:
            delta, j = read_vlq(data, j)
            tick += delta
            byte = data[j]

            if byte == 0xFF:                       # meta event
                kind = data[j + 1]
                size, k = read_vlq(data, j + 2)
                payload = data[k:k + size]
                j = k + size
                if kind == 0x51:
                    tempos.append(60_000_000 / int.from_bytes(payload, "big"))
                elif kind == 0x58:
                    signatures.append((payload[0], 2 ** payload[1]))
                continue

            if byte in (0xF0, 0xF7):               # sysex
                size, k = read_vlq(data, j + 1)
                j = k + size
                continue

            if byte & 0x80:                        # a new status byte (otherwise running status)
                status = byte
                j += 1

            kind = status & 0xF0
            if kind in (0xC0, 0xD0):               # one data byte
                j += 1
            else:
                if kind == 0x90 and data[j + 1] > 0:
                    notes.append((tick, data[j], data[j + 1]))
                j += 2
        i = end

    return division, sorted(notes), tempos, signatures


# ------------------------------------------------------------------------------
# Slices and their scores

def slice_notes(notes, start, length):
    """The notes of one slice, relative to its start. A note played a little
    early (up to a 32nd note before the slice) belongs to it: that's a
    drummer pushing the beat, not the end of the previous bar."""
    early = TICKS // 8
    return [(piece, t - start, v) for piece, t, v in notes if start - early <= t < start + length - early]


def raw_complexity(notes, length):
    """Note density, syncopation and the number of pieces, per bar."""
    bars = length / BAR
    density = len(notes) / bars
    syncopation = 0.0
    for piece, t, _ in notes:
        sixteenth = round(t / (TICKS / 4)) % 16
        weight = 0.0 if sixteenth % 4 == 0 else 1.0 if sixteenth % 2 == 0 else 2.0
        if piece in KICK_SNARE:
            weight *= 1.5          # a kick or snare off the beat is what makes a beat sound busy
        syncopation += weight
    syncopation /= bars
    pieces = len({p for p, _, _ in notes})
    return density, syncopation, pieces


def raw_loudness(notes):
    if not notes:
        return 0.0
    velocities = sorted(v for _, _, v in notes)
    loud = velocities[int(0.9 * (len(velocities) - 1))]
    return statistics.mean(velocities) * 0.6 + loud * 0.4


def combine_complexity(items):
    """Each item's (density, syncopation, pieces) as one number, each part
    measured against its average in the group."""
    if not items:
        return
    means = [statistics.mean(it["raw"][k] for it in items) or 1.0 for k in range(3)]
    for it in items:
        d, s, p = it["raw"]
        it["complexity_raw"] = 0.45 * d / means[0] + 0.35 * s / means[1] + 0.2 * p / means[2]


def rank_normalise(items, key, out):
    """0..1 by rank: the lowest gets 0, the highest 1, the rest evenly between."""
    ordered = sorted(items, key=lambda it: it[key])
    n = len(ordered)
    for i, it in enumerate(ordered):
        it[out] = round(i / (n - 1), 4) if n > 1 else 0.5


def flatten(notes):
    flat = []
    for piece, t, v in sorted(notes, key=lambda n: (n[1], PIECES.index(n[0]))):
        flat += [PIECES.index(piece), int(t), int(v)]
    return flat


# ------------------------------------------------------------------------------

def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    if not Path("CMakeLists.txt").exists():
        sys.exit("Run this from the repository root.")

    root = Path(sys.argv[1])
    info = root / "info.csv"
    if not info.exists():
        sys.exit(f"{info} not found: download the MIDI-only Groove MIDI Dataset "
                 f"from https://magenta.tensorflow.org/datasets/groove and unzip it")

    wanted_styles = {s for d in DRUMMERS for s in d["styles"]} | set(SOFT_STYLES)
    seen_notes = Counter()
    unmapped = Counter()

    # Per dataset style: beat performances (lists of bar slices) and fills.
    performances = defaultdict(list)
    fills = defaultdict(list)

    for row in csv.DictReader(info.open(encoding="utf-8")):
        style = row["style"].split("/")[0]
        if row["time_signature"] != "4-4" or style not in wanted_styles:
            continue

        division, raw_notes, tempos, signatures = read_midi(root / row["midi_filename"])
        if any(s != (4, 4) for s in signatures):
            continue

        notes = []
        for tick, note, velocity in raw_notes:
            seen_notes[note] += 1
            piece = TD11.get(note)
            if piece is None:
                unmapped[note] += 1
                continue
            notes.append((piece, round(tick * TICKS / division), velocity))

        if not notes:
            continue

        bpm = round(tempos[0] if tempos else float(row["bpm"]), 2)
        drummer = int(row["drummer"].replace("drummer", ""))
        source = row["id"]
        last = max(t for _, t, _ in notes)
        bar_count = last // BAR + 1

        if row["beat_type"] == "beat":
            bars = []
            for b in range(bar_count):
                slice_ = slice_notes(notes, b * BAR, BAR)
                first_half = slice_notes(notes, b * BAR, BAR // 2)
                second_half = slice_notes(notes, b * BAR + BAR // 2, BAR // 2)
                # The last bar is often cut short or just a final crash.
                complete = b < bar_count - 1
                fill = is_fill_bar(slice_, second_half)
                beat = [n for n in slice_ if n[0] != "crash"]
                bars.append({"notes": beat, "whole": slice_, "fill": fill, "complete": complete,
                             "wholeFill": fill and is_whole_fill(first_half, second_half),
                             "style": not fill and plays_style(beat, style)})

            # Split the performance at its fills and at bars that don't play
            # the style, into runs of plain beat bars.
            run = []
            for bar in bars:
                if bar["wholeFill"] and bar["complete"]:
                    fills[style].append({"src": source, "drummer": drummer, "bpm": bpm, "beats": 4,
                                         "notes": bar["whole"]})
                if bar["fill"] or not bar["style"] or not bar["complete"] or len(bar["notes"]) < 3:
                    if run:
                        performances[style].append({"src": source, "drummer": drummer, "bpm": bpm, "bars": run})
                    run = []
                    continue
                run.append({"notes": bar["notes"]})
            if run:
                performances[style].append({"src": source, "drummer": drummer, "bpm": bpm, "bars": run})
        else:
            # Fill files: every bar with a few snare or tom hits is a one-bar
            # fill, and its second half (when that's busy enough) a half-bar one.
            for b in range(bar_count):
                whole = slice_notes(notes, b * BAR, BAR)
                first = slice_notes(notes, b * BAR, BAR // 2)
                half = slice_notes(notes, b * BAR + BAR // 2, BAR // 2)
                if is_whole_fill(first, half):
                    fills[style].append({"src": source, "drummer": drummer, "bpm": bpm, "beats": 4, "notes": whole})
                if is_fill_half(half):
                    fills[style].append({"src": source, "drummer": drummer, "bpm": bpm, "beats": 2, "notes": half})

    if unmapped:
        sys.exit(f"TD-11 notes with no piece: {dict(unmapped)}")

    # Raw scores for every bar and fill.
    for style_items in list(performances.values()):
        for perf in style_items:
            for bar in perf["bars"]:
                bar["raw"] = raw_complexity(bar["notes"], BAR)
                bar["loud_raw"] = raw_loudness(bar["notes"])
    for style_items in fills.values():
        for fill in style_items:
            fill["raw"] = raw_complexity(fill["notes"], fill["beats"] * TICKS)
            fill["loud_raw"] = raw_loudness(fill["notes"])

    # Songwriter's share of pop and rock: their softest, simplest runs.
    soft = []
    for style in SOFT_STYLES:
        bars = [bar for perf in performances[style] for bar in perf["bars"]]
        combine_complexity(bars)
        rank_normalise(bars, "complexity_raw", "c_style")
        rank_normalise(bars, "loud_raw", "l_style")
        for perf in performances[style]:
            run = []
            for bar in perf["bars"] + [None]:
                gentle = bar is not None and (bar["c_style"] + bar["l_style"]) / 2 <= SOFT_SHARE
                if gentle:
                    run.append(bar)
                    continue
                if len(run) >= SOFT_MIN_RUN:
                    soft.append({"src": perf["src"], "drummer": perf["drummer"], "bpm": perf["bpm"],
                                 "style": style, "bars": [{"notes": b["notes"], "raw": b["raw"], "loud_raw": b["loud_raw"]} for b in run]})
                run = []

    library = {
        "format": FORMAT,
        "source": "Groove MIDI Dataset v1.0.0, Google LLC, CC BY 4.0 (https://magenta.tensorflow.org/datasets/groove)",
        "ticksPerBeat": TICKS,
        "pieces": PIECES,
        "td11": {str(k): v for k, v in sorted(TD11.items())},
        "sourceNotes": sorted(seen_notes),
        "drummers": [],
    }

    report = []
    for d in DRUMMERS:
        perfs = [dict(p, style=s) for s in d["styles"] for p in performances[s]]
        drum_fills = [dict(f, style=s) for s in d["styles"] for f in fills[s]]
        if d["id"] == "songwriter":
            perfs += soft
            # Fills to go with them: the gentler half of pop's and rock's.
            gentle = [dict(f, style=s) for s in SOFT_STYLES for f in fills[s]]
            loudness = sorted(f["loud_raw"] for f in gentle)
            cut = loudness[len(loudness) // 2] if loudness else 0
            drum_fills += [f for f in gentle if f["loud_raw"] <= cut]

        bars = [bar for p in perfs for bar in p["bars"]]
        combine_complexity(bars)
        rank_normalise(bars, "complexity_raw", "c")
        rank_normalise(bars, "loud_raw", "l")
        combine_complexity(drum_fills)
        rank_normalise(drum_fills, "complexity_raw", "c")
        rank_normalise(drum_fills, "loud_raw", "l")

        entry = {
            "id": d["id"], "name": d["name"], "description": d["description"], "kit": KIT,
            "swing": d["swing"], "styles": d["styles"],
            "beats": [{"src": p["src"], "style": p["style"], "drummer": p["drummer"], "bpm": p["bpm"],
                       "bars": [{"c": b["c"], "l": b["l"], "n": flatten(b["notes"])} for b in p["bars"]]}
                      for p in perfs],
            "fills": [{"src": f["src"], "style": f["style"], "drummer": f["drummer"], "bpm": f["bpm"],
                       "beats": f["beats"], "c": f["c"], "l": f["l"], "n": flatten(f["notes"])}
                      for f in drum_fills],
        }
        library["drummers"].append(entry)
        halves = sum(1 for f in drum_fills if f["beats"] == 2)
        report.append(f"{d['name']}: {len(bars)} beat bars in {len(perfs)} performances, "
                      f"{len(drum_fills) - halves} one-bar and {halves} half-bar fills")

    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_text(json.dumps(library, separators=(",", ":")) + "\n", encoding="utf-8")
    print("\n".join(report))
    print(f"Wrote {OUT} ({OUT.stat().st_size / 1024:.0f} KB)")


if __name__ == "__main__":
    main()
