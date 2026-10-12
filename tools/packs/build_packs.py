#!/usr/bin/env python3
"""Builds the downloadable sound packs from free sample libraries.

    python3 -I tools/packs/build_packs.py --work ~/ikaros-packs [pack ...]

The source libraries are downloaded by hand (see SOURCES below for where
each comes from) and unpacked into <work>/extracted/<source>/. This script
then makes each pack in <work>/build/<Pack name>/ and packages it with
make_pack.py into <work>/dist/ (zips and index.json, ready to upload).

For each pack it:
  - reads the library's SFZ programs (following #include and #define) to
    find the samples they actually play, and copies only those
  - converts WAV samples to 16-bit FLAC (about half the size; sfizz plays
    FLAC directly) and points the SFZ files at the FLAC files
  - applies a few small fixes so the programs load in sfizz without the
    commercial Sforzando player (see each pack's `patches`)
  - writes pack.json (with credits), the license, and one instrument.json
    per instrument, with its Sound tab knobs (see SoundLibrary.h)

To check that every instrument loads and plays, run the engine smoke test
with IKAROS_CHECK_PACKS pointing at the build folder:

    IKAROS_CHECK_PACKS=<work>/build build/IkarosEngineSmokeTest_artefacts/Debug/IkarosEngineSmokeTest

Nothing here is used by the app itself.
"""

import argparse
import json
import re
import shutil
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

HERE = Path(__file__).resolve().parent
MAKE_PACK = HERE.parent / "make_pack.py"
VERSION = "1.0.0"

# Where each library comes from, and its license. All were checked on the
# source's own page or repository.
SOURCES = {
    "upright-piano-kw": ("https://freepats.zenvoid.org/Piano/acoustic-grand-piano.html",
                         "UprightPianoKW-SFZ+FLAC-20220221.7z"),
    "salamander-grand": ("https://freepats.zenvoid.org/Piano/acoustic-grand-piano.html",
                         "SalamanderGrandPianoV3+20161209_44khz16bit.tar.xz"),
    "big-rusty-drums": ("https://github.com/sfzinstruments/karoryfer.big-rusty-drums", "Big_Rusty_Drums_1100.zip"),
    "growlybass": ("https://github.com/sfzinstruments/karoryfer.growlybass", "Karoryfer.Growlybass.v1.002.zip"),
    "shinyguitar": ("https://github.com/sfzinstruments/karoryfer.shinyguitar", "Karoryfer.Shinyguitar.v1.002.zip"),
    "vsco2-ce": ("https://github.com/sgossner/VSCO-2-CE", "the SFZ branch"),
    "gregsullivan-epianos": ("https://github.com/sfzinstruments/GregSullivan.E-Pianos", "the master branch"),
}


STRINGS_FILES = [('Violins', 'ViolinEnsSusVib.sfz'), ('Violas', 'ViolaEnsSusVib.sfz'), ('Cellos', 'CelloEnsSusVib.sfz'), ('Double Basses', 'ContrabassSusVB.sfz'), ('Solo Violin', 'SViolinVib.sfz'), ('Pizzicato Strings', 'ViolinEnsPizz.sfz')]


def knob(name, control, default, cc=None):
    k = {"name": name, "control": control, "default": default}
    if cc is not None:
        k["cc"] = cc
    return k


# The packs. "library" is the folder (inside the source's extracted folder)
# that's copied into the pack as Library/; each instrument plays one of its
# SFZ programs.
PACKS = [
    {
        "id": "ikaros-studio.piano", "name": "Piano", "category": "Keyboards", "starter": True,
        "source": "upright-piano-kw", "library": "UprightPianoKW-SFZ+FLAC-20220221",
        "description": "A Kawai upright piano in a living room, with two velocity layers.",
        "license": "CC0-1.0", "licenseFile": "cc0.txt",
        "credits": [{"what": "Upright Piano KW", "who": "FreePats (Gonzalo and Roberto)", "license": "CC0-1.0",
                     "url": "https://freepats.zenvoid.org/Piano/acoustic-grand-piano.html", "attributionRequired": False}],
        # A Release knob: how long notes ring on after the key comes up.
        "patches": {"UprightPianoKW-20220221.sfz": [(" ampeg_release=0.6\n", " ampeg_release=0.6\n ampeg_release_oncc72=3\n")]},
        "instruments": [
            {"name": "Upright Piano", "icon": "piano", "sfz": "UprightPianoKW-20220221.sfz",
             "knobs": [knob("Tone", "tone", 0.5), knob("Release", "cc", 0.0, 72), knob("Width", "width", 0.5),
                       knob("Room", "room", 0.2), knob("Pitch", "tune", 0.5)]},
        ],
    },
    {
        "id": "ikaros-studio.electric-pianos", "name": "Electric Pianos", "category": "Keyboards", "starter": False,
        "source": "gregsullivan-epianos", "library": "GregSullivan.E-Pianos-master",
        "description": "Three classic electric pianos: a Yamaha CP80, a Hohner Pianet T and a Wurlitzer EP200.",
        "license": "CC-BY-3.0", "licenseFile": "LICENSE",
        "credits": [{"what": "Electric piano samples (CP80, Pianet T, Wurlitzer EP200)", "who": "Greg Sullivan",
                     "license": "CC-BY-3.0", "url": "https://github.com/sfzinstruments/GregSullivan.E-Pianos",
                     "attributionRequired": True}],
        "instruments": [
            {"name": name, "icon": "piano", "sfz": f"{folder}/{folder}.sfz",
             "knobs": [knob("Tone", "tone", 0.5), knob("Drive", "drive", 0.0), knob("Release", "cc", 0.05, 72),
                       knob("Width", "width", 0.5), knob("Room", "room", 0.15)]}
            for name, folder in (("CP80 Electric Grand", "CP80"), ("Pianet", "Pianet T"), ("Wurlitzer", "Wurlitzer EP200"))
        ],
    },
    {
        "id": "ikaros-studio.drums", "name": "Drums", "category": "Drums", "starter": True,
        "source": "big-rusty-drums", "library": ".",
        "description": "A big, rough acoustic drum kit played with sticks: kick, snare, hi-hat, three toms, ride and crash.",
        "license": "CC0-1.0", "licenseFile": "LICENSE",
        "credits": [{"what": "Big Rusty Drums", "who": "Karoryfer Samples", "license": "CC0-1.0",
                     "url": "https://shop.karoryfer.com/pages/free-big-rusty-drums", "attributionRequired": False}],
        "instruments": [
            {"name": "Rusty Kit", "icon": "drums", "sfz": "Programs/02-basic.sfz",
             "knobs": [knob("Tone", "tone", 0.5), knob("Kick Punch", "cc", 0.0, 74), knob("Snare Snap", "cc", 0.0, 83),
                       knob("Crunch", "drive", 0.0), knob("Room", "room", 0.1)]},
        ],
    },
    {
        "id": "ikaros-studio.bass", "name": "Bass", "category": "Bass", "starter": True,
        "source": "growlybass", "library": "Growlybass",
        "description": "A Squier Jazz Bass, with four velocity layers and four takes of every note.",
        "license": "CC0-1.0", "licenseFile": "LICENSE",
        "credits": [{"what": "Growlybass", "who": "Karoryfer Samples", "license": "CC0-1.0",
                     "url": "https://github.com/sfzinstruments/karoryfer.growlybass", "attributionRequired": False}],
        "instruments": [
            {"name": "Electric Bass", "icon": "bass", "sfz": "growlybass_dirty.sfz"},
            {"name": "Staccato Bass", "icon": "bass", "sfz": "growlybass_vicious.sfz"},
        ],
    },
    {
        "id": "ikaros-studio.guitar", "name": "Guitar", "category": "Guitar", "starter": True,
        "source": "shinyguitar", "library": "Shinyguitar",
        "description": "An archtop guitar, recorded both through its pickup and with a microphone.",
        "license": "CC0-1.0", "licenseFile": "LICENSE",
        "credits": [{"what": "Shinyguitar", "who": "Karoryfer Samples (played and mapped by D. Smolken)", "license": "CC0-1.0",
                     "url": "https://github.com/sfzinstruments/karoryfer.shinyguitar", "attributionRequired": False}],
        # The sample folder is defined in the Sforzando bank file, which sfizz
        # doesn't read. And only the one-voice layout (the library's default,
        # cc107 = 0) is kept: the three- and five-voice layouts repeat every
        # note, and loading all 13,000 regions takes sfizz far too long.
        "patches": {"Programs/main.sfz": [("default_path=$sample_dir/", "default_path=../Samples/")]
                    + [(f'#include "{kind}_{n}.sfz"\n', "") for n in ("two", "three", "four", "five")
                       for kind in ("electric", "acoustic")]},
        "instruments": [
            # Blend (cc100): 0 = the pickup (electric), 1 = the microphone (acoustic).
            {"name": "Electric Guitar", "icon": "guitar", "sfz": "Programs/main.sfz",
             "knobs": [knob("Tone", "tone", 0.5), knob("Drive", "drive", 0.0), knob("Mic", "cc", 0.0, 100),
                       knob("Width", "width", 0.5), knob("Room", "room", 0.2)]},
            {"name": "Hollow Body Guitar", "icon": "guitar", "sfz": "Programs/main.sfz",
             "knobs": [knob("Tone", "tone", 0.5), knob("Drive", "drive", 0.0), knob("Mic", "cc", 1.0, 100),
                       knob("Width", "width", 0.5), knob("Room", "room", 0.25)]},
        ],
    },
    {
        "id": "ikaros-studio.strings", "name": "Strings", "category": "Strings", "starter": True,
        "source": "vsco2-ce", "library": "VSCO-2-CE-SFZ",
        "description": "Violin, viola, cello and double bass sections, a solo violin and pizzicato strings, from a chamber orchestra.",
        "license": "CC0-1.0", "licenseFile": "LICENSE",
        "credits": [{"what": "VSCO 2 Community Edition", "who": "Versilian Studios (Sam Gossner)", "license": "CC0-1.0",
                     "url": "https://github.com/sgossner/VSCO-2-CE", "attributionRequired": False}],
        # Attack and Release knobs for the bowed instruments.
        "patches": {f: [("<global>", "<global>\nampeg_attack_oncc73=1.5\nampeg_release_oncc72=3", 1)]
                    for f in ("ViolinEnsSusVib.sfz", "ViolaEnsSusVib.sfz", "CelloEnsSusVib.sfz", "ContrabassSusVB.sfz",
                              "SViolinVib.sfz")},
        "instruments": [
            {"name": name, "icon": "strings", "sfz": sfz,
             "knobs": ([knob("Tone", "tone", 0.5), knob("Attack", "cc", 0.0, 73), knob("Release", "cc", 0.0, 72),
                        knob("Width", "width", 0.5), knob("Room", "room", 0.3)] if "Pizz" not in sfz else
                       [knob("Tone", "tone", 0.5), knob("Drive", "drive", 0.0), knob("Width", "width", 0.5),
                        knob("Room", "room", 0.3), knob("Pitch", "tune", 0.5)])}
            for name, sfz in STRINGS_FILES
        ],
    },
    {
        "id": "ikaros-studio.grand-piano", "name": "Grand Piano", "category": "Keyboards", "starter": False,
        "source": "salamander-grand", "library": "SalamanderGrandPianoV3_44.1khz16bit",
        "description": "A Yamaha C5 grand piano with 16 velocity layers, hammer noise and string resonance.",
        "license": "CC-BY-3.0", "licenseFile": "README",
        "credits": [{"what": "Salamander Grand Piano", "who": "Alexander Holm", "license": "CC-BY-3.0",
                     "url": "https://freepats.zenvoid.org/Piano/acoustic-grand-piano.html", "attributionRequired": True}],
        # A Release knob (the file has no <global> header of its own).
        "patches": {"SalamanderGrandPianoV3.sfz": [("", "<global>\nampeg_release_oncc72=2\n\n", "prepend")]},
        "instruments": [
            {"name": "Grand Piano", "icon": "piano", "sfz": "SalamanderGrandPianoV3.sfz",
             "knobs": [knob("Tone", "tone", 0.5), knob("Release", "cc", 0.0, 72), knob("Width", "width", 0.5),
                       knob("Room", "room", 0.2), knob("Pitch", "tune", 0.5)]},
        ],
    },
]


# ------------------------------------------------------------------------------
# Reading SFZ files: which samples does a program play?

OPCODE_RE = re.compile(r"\s+[A-Za-z0-9_$]+=")
DEFINE_RE = re.compile(r"^\s*#define\s+(\$\w+)\s+(.*?)\s*$")
INCLUDE_RE = re.compile(r'^\s*#include\s+"([^"]+)"')


def apply_patches(text, patches, check=True, name=""):
    """Each patch is (old, new) to replace every occurrence, (old, new, 1) to
    replace the first only, or ("", text, "prepend") to add text at the top."""
    for patch in patches:
        old, new = patch[0], patch[1]
        how = patch[2] if len(patch) > 2 else None
        if how == "prepend":
            text = new + text
            continue
        if check and old not in text:
            sys.exit(f"patch for {name} doesn't apply: {old!r}")
        text = text.replace(old, new, 1) if how == 1 else text.replace(old, new)
    return text


def opcode_value(line, opcode):
    """An opcode's value on a line: it may contain spaces, and runs up to the
    next opcode or a comment."""
    m = re.search(r"\b" + opcode + r"=(.+)", line.split("//")[0])
    if not m:
        return None
    value = m.group(1)
    nxt = OPCODE_RE.search(value)
    return (value[:nxt.start()] if nxt else value).strip()


def sample_value(line):
    """The sample= value on a line: it may contain spaces, and runs up to the
    next opcode or a comment."""
    return opcode_value(line, "sample")


def referenced_samples(program: Path, patches=None):
    """Every sample file the program (and the files it includes) plays, and
    those SFZ files. Includes are relative to the program's own folder, as
    sfizz does it."""
    root = program.parent
    defines = {}
    default_path = ""
    found = []
    files = []

    def read(path: Path):
        nonlocal default_path
        files.append(path.resolve())
        text = path.read_text(encoding="utf-8", errors="replace")

        text = apply_patches(text, (patches or {}).get(path.resolve(), []), check=False)

        for raw in text.splitlines():
            line = raw
            for name, value in sorted(defines.items(), key=lambda kv: -len(kv[0])):
                line = line.replace(name, value)

            if (m := DEFINE_RE.match(raw)):
                defines[m.group(1)] = m.group(2)
                continue
            if (m := INCLUDE_RE.match(line)):
                read(root / m.group(1))
                continue
            if (dp := opcode_value(line, "default_path")) is not None:
                default_path = dp.replace("\\", "/")

            value = sample_value(line)
            if value and not value.startswith("*"):   # *silence, *sine: built-in, no file
                found.append((root / default_path / value.replace("\\", "/")).resolve())

    read(program)
    return found, files


# ------------------------------------------------------------------------------
# Building a pack

def to_flac(src: Path, dst: Path):
    dst.parent.mkdir(parents=True, exist_ok=True)
    # 16-bit with triangular dither: smaller, and nothing anyone could hear in playback.
    subprocess.run(["ffmpeg", "-loglevel", "error", "-y", "-i", str(src),
                    "-af", "aresample=osf=s16:dither_method=triangular", "-c:a", "flac", "-compression_level", "8",
                    str(dst)], check=True)


def fix_sfz_text(text):
    """Points sample= lines at the FLAC files, with forward slashes."""
    out = []
    for line in text.splitlines(keepends=True):
        if (dp := opcode_value(line, "default_path")) is not None:
            line = line.replace(dp, dp.replace("\\", "/"), 1)

        value = sample_value(line)
        if value and not value.startswith("*"):
            fixed = value.replace("\\", "/")
            fixed = re.sub(r"\.wav$", ".flac", fixed, flags=re.IGNORECASE)
            line = line.replace(value, fixed, 1)
        out.append(line)
    return "".join(out)


def build_pack(pack, work: Path):
    lib_src = (work / "extracted" / pack["source"] / pack["library"]).resolve()
    out = work / "build" / pack["name"]
    lib_dst = out / "Library"

    if not lib_src.is_dir():
        sys.exit(f"{lib_src} is missing: download {SOURCES[pack['source']][1]} from "
                 f"{SOURCES[pack['source']][0]} and unpack it into {work / 'extracted' / pack['source']}")

    shutil.rmtree(out, ignore_errors=True)
    lib_dst.mkdir(parents=True)

    patches = {(lib_src / f).resolve(): p for f, p in pack.get("patches", {}).items()}

    # The samples the instruments play, and the SFZ files that say so.
    samples, sfz_files = set(), set()
    for inst in pack["instruments"]:
        program = lib_src / inst["sfz"]
        refs, files = referenced_samples(program, patches)
        missing = [r for r in refs if not r.exists()]
        if missing:
            sys.exit(f"{program.name}: {len(missing)} samples not found, e.g. {missing[0]}")
        samples.update(refs)
        sfz_files.update(files)

    # Copy those SFZ files, fixed up, and the samples, converted.
    for f in sorted(sfz_files):
        rel = f.relative_to(lib_src)
        text = apply_patches(f.read_text(encoding="utf-8", errors="replace"), patches.get(f, []), name=str(rel))
        (lib_dst / rel).parent.mkdir(parents=True, exist_ok=True)
        (lib_dst / rel).write_text(fix_sfz_text(text), encoding="utf-8")

    jobs = []
    for s in sorted(samples):
        rel = s.relative_to(lib_src)
        if s.suffix.lower() == ".wav":
            jobs.append((s, (lib_dst / rel).with_suffix(".flac")))
        else:
            (lib_dst / rel).parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(s, lib_dst / rel)

    with ThreadPoolExecutor(max_workers=4) as pool:
        list(pool.map(lambda j: to_flac(*j), jobs))

    # The license: who made it, what we changed (CC-BY asks for both), then
    # the original license text.
    credit = pack["credits"][0]
    url, archive = SOURCES[pack["source"]]
    (out / "LICENSE.txt").write_text(
        f"{credit['what']} by {credit['who']}\n"
        f"Source: {url} ({archive})\n"
        f"License: {pack['license']}\n\n"
        "Changes made for Ikaros Studio: only the samples these instruments play are included, WAV samples were\n"
        "converted to 16-bit FLAC, sample paths in the SFZ files were adjusted, and MIDI controllers were added\n"
        "for the Sound tab's knobs where the original had none.\n\n"
        "Original license text:\n\n" + (lib_src / pack["licenseFile"]).read_text(encoding="utf-8", errors="replace"),
        encoding="utf-8")

    for inst in pack["instruments"]:
        folder = out / "Instruments" / inst["name"]
        folder.mkdir(parents=True)
        info = {"name": inst["name"], "category": pack["category"], "icon": inst["icon"],
                "sfz": "../../Library/" + inst["sfz"]}
        if "knobs" in inst:
            info["knobs"] = inst["knobs"]
        (folder / "instrument.json").write_text(json.dumps(info, indent=4) + "\n", encoding="utf-8")

    size = sum(f.stat().st_size for f in out.rglob("*") if f.is_file())
    (out / "pack.json").write_text(json.dumps({
        "format": 2, "id": pack["id"], "name": pack["name"], "version": VERSION,
        "description": pack["description"], "author": pack["credits"][0]["who"] + ", packaged by Ikaros Studio",
        "license": pack["license"], "licenseFile": "LICENSE.txt", "installedSize": size,
        "credits": pack["credits"],
    }, indent=4) + "\n", encoding="utf-8")

    print(f"{pack['name']}: {len(samples)} samples, {size / 1048576:.0f} MB")
    return out


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--work", type=Path, required=True, help="folder with extracted/ (build/ and dist/ go here too)")
    parser.add_argument("--base-url", default="", help="where the zips will be hosted (see make_pack.py)")
    parser.add_argument("packs", nargs="*", help="pack names to build (default: all)")
    args = parser.parse_args()

    for pack in PACKS:
        if args.packs and pack["name"] not in args.packs:
            continue
        folder = build_pack(pack, args.work)
        cmd = [sys.executable, "-I", str(MAKE_PACK), str(folder), "--category", pack["category"],
               "--out", str(args.work / "dist"), "--base-url", args.base_url]
        if pack["starter"]:
            cmd += ["--collection", "starter", "--collection-name", "Starter Sounds",
                    "--collection-description", "Piano, drums, bass, guitar and strings to start making music with."]
        subprocess.run(cmd, check=True)


if __name__ == "__main__":
    main()
