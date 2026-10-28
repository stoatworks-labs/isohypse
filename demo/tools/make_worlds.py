"""The browser demo's maps, made from the plugin's own in data/.

    python3 demo/tools/make_worlds.py           write demo/worlds/
    python3 demo/tools/make_worlds.py --check   exit 1 if demo/worlds/ is not
                                                what this would write

Called with --check from `tools/verify.sh`, so a map remade by
tools/make-worlds.py and not carried across here fails verify.

------------------------------------------------------------------- what it writes

- The nine GLOBES at half the plugin's resolution: Earth 2048 x 1024 (the
  plugin's is 4096 x 2048), the rest 1024 x 512 (2048 x 1024). Each output
  pixel is the exact mean of a 2 x 2 block, rounded half up -- the same box the
  plugin's own mipmap takes one level down, so the page at its own level 0 is
  close to the plugin at its level 1. On the three with gaps (Venus, Pluto,
  Charon: 0 is "never measured", the data 1..65535) a pixel is measured where at
  least two of its four were, and its value is the mean of those alone: the rule
  tools/make-worlds.py used to make the plugin's maps from the missions' files.
  Why half: the plugin carries 40 MB of maps, Earth alone 11.9 MB, and a page
  should not download that to show a globe 500 pixels across.
- The twenty-two PLACES unchanged: byte for byte the plugin's files (512 x 512,
  0.2 to 0.4 MB each). git stores one copy of a file it already has.
- `table.js`: the plugin's table (source/WorldTable.inc) as an ES module, in
  its order, with each file's size as served here beside the plugin's.

Every PNG is what the plugin reads: 16-bit greyscale, linear in height between
the table's minimum and maximum. The heights are not rescaled, so a level, the
datum and the coast are the plugin's whatever the resolution.

The licences allow this: the globes are public domain or released without
restriction (NASA, USGS, NOAA); the places' sources require the attribution in
data/SOURCES.md to travel with the data, and the page carries it.

Needs numpy and Pillow, as tools/make-worlds.py does.
"""
import hashlib
import io
import os
import re
import sys

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
DATA = os.path.join(REPO, "data")
OUT = os.path.join(REPO, "demo", "worlds")
sys.dont_write_bytecode = True

GLOBE = re.compile(r'^ISOHYPSE_GLOBE\( (\w+), "([^"]*)", "([^"]+)", (\d+), (\d+), (-?[\d.]+), (-?[\d.]+), ([01]), ([01]) \)$')
PLACE = re.compile(r'^ISOHYPSE_PLACE\( (\w+), "([^"]*)", "([^"]+)", (\d+), (\d+), (-?[\d.]+), (-?[\d.]+), ([01]), (-?[\d.]+), (-?[\d.]+), ([\d.]+) \)$')


def table():
    globes, places = [], []
    with open(os.path.join(REPO, "source", "WorldTable.inc")) as handle:
        for line in handle:
            line = line.strip()
            g = GLOBE.match(line)
            p = PLACE.match(line)
            if g:
                globes.append(dict(name=g[2], file=g[3], width=int(g[4]), height=int(g[5]), lo=g[6], hi=g[7],
                                   nodata=g[8] == "1", sea=g[9] == "1"))
            elif p:
                places.append(dict(name=p[2], file=p[3], width=int(p[4]), height=int(p[5]), lo=p[6], hi=p[7],
                                   sea=p[8] == "1", lat=p[9], lon=p[10], mpp=p[11]))
            elif line.startswith("ISOHYPSE_"):
                raise SystemExit(f"make_worlds: cannot read WorldTable.inc line: {line}")
    if len(globes) != 9 or len(places) != 22:
        raise SystemExit(f"make_worlds: {len(globes)} globes and {len(places)} places in WorldTable.inc, expected 9 and 22")
    return globes, places


def read16(path):
    with Image.open(path) as im:
        a = np.asarray(im)
        if a.dtype == np.uint8 or a.ndim != 2:
            raise SystemExit(f"make_worlds: {path} is not 16-bit greyscale ({im.mode})")
        return a.astype(np.uint16)


def halve(v, nodata):
    """2 x 2 box mean, rounded half up; gaps as tools/make-worlds.py treats them."""
    h, w = v.shape
    if h % 2 or w % 2:
        raise SystemExit("make_worlds: a globe with an odd side")
    blocks = v.reshape(h // 2, 2, w // 2, 2).astype(np.int64)
    if not nodata:
        return ((blocks.sum(axis=(1, 3)) + 2) // 4).astype(np.uint16)
    measured = blocks != 0
    count = measured.sum(axis=(1, 3))
    total = np.where(measured, blocks - 1, 0).sum(axis=(1, 3))
    safe = np.maximum(count, 1)
    # Rounded half up: floor( total / count + 1/2 ), in integers.
    mean = (2 * total + safe) // (2 * safe)
    return np.where(count >= 2, mean + 1, 0).astype(np.uint16)


def encode(v):
    buffer = io.BytesIO()
    Image.fromarray(v).save(buffer, format="PNG", compress_level=9)
    return buffer.getvalue()


def table_js(globes, places, sizes):
    def num(s):
        return s

    lines = [
        "// GENERATED from source/WorldTable.inc by demo/tools/make_worlds.py. Do not edit:",
        "// tools/verify.sh fails if it is not what that script would write.",
        "//",
        "// The plugin's table, in its order (the World and Place dropdowns are",
        "// append-only, so the order is the index a composition stores). width and",
        "// height are the plugin's file; servedWidth and servedHeight are the file",
        "// this page fetches from ./worlds/, half the size for a globe.",
        "",
        "export const GLOBES = [",
    ]
    for g in globes:
        sw, sh = sizes[g["file"]]
        lines.append(
            f"  {{ name: '{g['name']}', file: '{g['file']}', kind: 'globe', width: {g['width']}, height: {g['height']}, "
            f"servedWidth: {sw}, servedHeight: {sh}, minMetres: {num(g['lo'])}, maxMetres: {num(g['hi'])}, "
            f"hasNodata: {str(g['nodata']).lower()}, hasSea: {str(g['sea']).lower()} }},")
    lines += ["];", "", "export const PLACES = ["]
    for p in places:
        sw, sh = sizes[p["file"]]
        name = p["name"].replace("'", "\\'")
        lines.append(
            f"  {{ name: '{name}', file: '{p['file']}', kind: 'place', width: {p['width']}, height: {p['height']}, "
            f"servedWidth: {sw}, servedHeight: {sh}, minMetres: {num(p['lo'])}, maxMetres: {num(p['hi'])}, "
            f"hasNodata: false, hasSea: {str(p['sea']).lower()}, centreLat: {p['lat']}, centreLon: {p['lon']}, "
            f"metresPerPixel: {p['mpp']} }},")
    lines += ["];", ""]
    return "\n".join(lines)


def build():
    """Every file demo/worlds/ should hold: name -> (bytes or array, kind)."""
    globes, places = table()
    files, sizes = {}, {}
    for g in globes:
        v = read16(os.path.join(DATA, g["file"]))
        if v.shape != (g["height"], g["width"]):
            raise SystemExit(f"make_worlds: data/{g['file']} is {v.shape[1]}x{v.shape[0]}, the table says {g['width']}x{g['height']}")
        half = halve(v, g["nodata"])
        files[g["file"]] = ("pixels", half)
        sizes[g["file"]] = (half.shape[1], half.shape[0])
    for p in places:
        with open(os.path.join(DATA, p["file"]), "rb") as handle:
            raw = handle.read()
        files[p["file"]] = ("bytes", raw)
        sizes[p["file"]] = (p["width"], p["height"])
    files["table.js"] = ("bytes", table_js(globes, places, sizes).encode())
    return files


def main(argv):
    files = build()
    if "--check" in argv:
        problems = 0
        present = set(os.listdir(OUT)) if os.path.isdir(OUT) else set()
        for name, (kind, want) in files.items():
            path = os.path.join(OUT, name)
            if name not in present:
                print(f"FAIL  demo/worlds/{name} is missing")
                problems += 1
                continue
            if kind == "pixels":
                got = read16(path)
                same = got.shape == want.shape and np.array_equal(got, want)
            else:
                with open(path, "rb") as handle:
                    same = handle.read() == want
            if same:
                what = f"{want.shape[1]}x{want.shape[0]}, the plugin's halved" if kind == "pixels" else (
                    "the plugin's file, byte for byte" if name.endswith(".png") else "the plugin's table")
                print(f"ok    {name:<24} {what}")
            else:
                print(f"FAIL  demo/worlds/{name} is not what demo/tools/make_worlds.py makes from data/")
                problems += 1
        for extra in sorted(present - set(files)):
            print(f"FAIL  demo/worlds/{extra} is not made by this script")
            problems += 1
        print()
        if problems:
            print(f"{problems} problem(s) -- rerun python3 demo/tools/make_worlds.py")
            return 1
        print(f"all {len(files)} files in demo/worlds/ are made from data/ and source/WorldTable.inc")
        return 0

    os.makedirs(OUT, exist_ok=True)
    for name in os.listdir(OUT):
        if name not in files:
            os.remove(os.path.join(OUT, name))
    total = 0
    for name, (kind, want) in files.items():
        data = encode(want) if kind == "pixels" else want
        with open(os.path.join(OUT, name), "wb") as handle:
            handle.write(data)
        total += len(data)
        digest = hashlib.sha256(data).hexdigest()[:12]
        print(f"wrote {name:<24} {len(data):>10,} bytes  {digest}")
    print(f"{len(files)} files, {total:,} bytes")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
