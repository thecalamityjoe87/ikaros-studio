#!/usr/bin/env python3
"""Packages sound packs for download, and writes the pack index the app reads.

    tools/make_pack.py "path/to/My Pack" --category Drums --out dist/

makes dist/<pack id>-<version>.zip (the pack folder, zipped) and adds or
updates the pack's entry in dist/index.json, with its download size, the
size it takes once installed, and the SHA-256 the app checks after
downloading. With --collection, the packs are also added to that collection
(e.g. "starter", the set new users are offered on first run). Upload the zip
and index.json together. If the zips live
somewhere else than the index, pass --base-url (otherwise each entry's url is
just the zip's name, relative to the index).

The pack must have a format 2 pack.json (with an id and a version); see
src/engine/SoundLibrary.h for the pack layout and src/engine/PackStore.h for
the index format. Loop keys in Loops/loops.json are checked first.

The zip is made the same way every time (sorted entries, fixed dates), so
packaging the same files again gives the same SHA-256.
"""

import argparse
import hashlib
import json
import re
import sys
import zipfile
from pathlib import Path


# Loop keys, as SoundLibrary::parseKey reads them: "C", "C major", "F# minor", "Bbm".
KEY_PATTERN = re.compile(r"^[A-Ga-g](#|b|\u266f|\u266d)?\s*(|M|maj|major|m|min|minor)$", re.IGNORECASE)
DRUM_TAGS = {"drums", "drum", "drumloop", "drum-loop", "percussion", "beat", "beats",
             "breakbeat", "hihat", "hi-hat", "hats", "kick", "snare"}


def check_loops(pack: Path):
    """Stops on a loop key the app can't read, and warns about drum loops with a key."""
    loops_file = pack / "Loops" / "loops.json"
    if not loops_file.exists():
        return

    for loop in json.loads(loops_file.read_text(encoding="utf-8")):
        name = loop.get("name", loop.get("file"))
        key = loop.get("key")
        unpitched = loop.get("unpitched")

        if unpitched is not None and not isinstance(unpitched, bool):
            sys.exit(f"{loops_file}: {name!r} has \"unpitched\": {unpitched!r}, which should be true or false")
        if key and not KEY_PATTERN.match(str(key).strip()):
            sys.exit(f"{loops_file}: {name!r} has the key {key!r}; write it like \"C\", \"F# minor\" or \"Bbm\"")
        if key and unpitched:
            print(f"warning: {name!r} has a key but is unpitched, so its key is ignored", file=sys.stderr)
        if key and unpitched is None and DRUM_TAGS & {t.lower() for t in loop.get("tags", [])}:
            print(f"warning: {name!r} is tagged as drums, so the app treats it as unpitched and ignores its key "
                  f"(add \"unpitched\": false if it should follow the song's key)", file=sys.stderr)


def package(pack: Path, out: Path, category: str, base_url: str) -> dict:
    info = json.loads((pack / "pack.json").read_text(encoding="utf-8"))

    for field in ("id", "name", "version"):
        if not info.get(field):
            sys.exit(f"{pack}/pack.json has no {field!r}: packs for download need format 2 (see SoundLibrary.h)")

    check_loops(pack)

    files = sorted(p for p in pack.rglob("*") if p.is_file() and not p.name.startswith("."))
    zip_name = f"{info['id']}-{info['version']}.zip"
    zip_path = out / zip_name

    with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for f in files:
            entry = zipfile.ZipInfo(f"{pack.name}/{f.relative_to(pack).as_posix()}", date_time=(2020, 1, 1, 0, 0, 0))
            entry.external_attr = 0o644 << 16
            entry.compress_type = zipfile.ZIP_DEFLATED
            z.writestr(entry, f.read_bytes())

    data = zip_path.read_bytes()

    return {
        "id": info["id"],
        "name": info["name"],
        "version": info["version"],
        "category": category,
        "description": info.get("description", ""),
        "author": info.get("author", ""),
        "license": info.get("license", ""),
        "downloadSize": len(data),
        "installedSize": sum(f.stat().st_size for f in files),
        "url": base_url + zip_name,
        "sha256": hashlib.sha256(data).hexdigest(),
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("packs", nargs="+", type=Path, help="pack folders")
    parser.add_argument("--out", type=Path, required=True, help="folder for the zips and index.json")
    parser.add_argument("--category", default="", help="the category shown in the store (e.g. Drums)")
    parser.add_argument("--base-url", default="", help="where the zips will be, if not next to the index")
    parser.add_argument("--collection", help="also add the packs to this collection (e.g. starter)")
    parser.add_argument("--collection-name", help="the collection's name, if it's new (e.g. \"Starter Sounds\")")
    parser.add_argument("--collection-description", default="", help="the collection's description, if it's new")
    args = parser.parse_args()

    args.out.mkdir(parents=True, exist_ok=True)
    index_path = args.out / "index.json"
    index = json.loads(index_path.read_text(encoding="utf-8")) if index_path.exists() else {"format": 1, "packs": []}
    base_url = args.base_url if not args.base_url or args.base_url.endswith("/") else args.base_url + "/"

    for pack in args.packs:
        entry = package(pack, args.out, args.category, base_url)
        index["packs"] = [p for p in index["packs"] if p["id"] != entry["id"]] + [entry]
        print(f"{entry['name']} {entry['version']}: {entry['url']} ({entry['downloadSize']} bytes)")

        if args.collection:
            collections = index.setdefault("collections", [])
            collection = next((c for c in collections if c["id"] == args.collection), None)

            if collection is None:
                collection = {"id": args.collection, "name": args.collection_name or args.collection.title(),
                              "description": args.collection_description, "packs": []}
                collections.append(collection)

            if entry["id"] not in collection["packs"]:
                collection["packs"].append(entry["id"])

    index["packs"].sort(key=lambda p: p["name"].lower())
    index_path.write_text(json.dumps(index, indent=4) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
