"""The browser demo against the plugin: the same settings, two renderers, one picture.

    python3 demo/tools/crosscheck/crosscheck.py [--port 8718] [--keep DIR] [--half]

Renders each case below twice, at 1280 x 720 after 60 frames a sixtieth of a
second apart (ihtest's own default):

  - by the PLUGIN: build/ihtest, the real plugin classes in a GL 4.1 context
    (build it first: cmake -B build && cmake --build build --target ihtest),
    reading the maps from data/;
  - by the PAGE: demo/ served on --port and loaded in headless Chrome (page.mjs),
    through window.isohypseDemo.renderCase, which makes a fresh instance, loads
    the world before frame 0 as the harness does, and runs the same frames.

and compares them pixel by pixel. For a like-for-like comparison the page is
served from a scratch copy of demo/ whose globes are the plugin's own full-size
files (and whose table says so); --half also compares the page as it ships,
on its half-size globes, and reports how far that moves the picture. The
Over's clip is the harness's own card, taken from the Over at Mix 0 (which
hands its clip back to the bit) and fed to both.

The page's GPU half is the plugin's GLSL (check_shaders.py), so what this
mostly tests is the PORT: every mapping, the sweep's clock, the styles, the
orientation, the PNG decoder and the gap filler, and the frame logic. A
difference here is a port bug or a difference between GL 4.1 on this machine
and WebGL2 through ANGLE on the same GPU; the summary says which cases differ
and by how much, and a negative control (the page one frame late) shows the
comparison can see a sixtieth of a second.

Not run by tools/verify.sh: it needs a GPU, Chrome and node. Exit 1 if a case
falls under its threshold or the negative control is not caught.
"""
import argparse
import functools
import http.server
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import threading

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
W, H, FRAMES = 1280, 720, 60
sys.dont_write_bytecode = True

# name, plugin ("source" or "over"), settings as ihtest --set takes them.
CASES = [
    ("moon-defaults", "source", {}),
    ("mars-map-neon", "source", {"World": "Mars", "View": "Map", "Style": "Neon"}),
    ("earth-survey", "source", {"World": "Earth", "View": "Map", "Style": "Survey", "Tints": "0.6", "Shade": "0.5"}),
    ("earth-globe-flood", "source", {"World": "Earth", "Flood": "0.7", "Sweep": "Bounce", "Lines": "0.7"}),
    ("pluto-far-side", "source", {"World": "Pluto", "Longitude": "1"}),
    ("venus-manual", "source", {"World": "Venus", "Sweep": "Manual", "Sweep Level": "0.3", "Height Colour": "1"}),
    ("everest-plotter", "source", {"World": "Place", "Place": "Everest", "Style": "Plotter", "Wobble": "0.3"}),
    ("sydney-sea", "source", {"World": "Place", "Place": "Sydney", "Flood": "0.5", "Zoom": "0.2", "Rotation": "0.6"}),
    ("generated-craters", "source", {"World": "Generated", "Terrain": "Craters", "Morph": "0.6"}),
    ("generated-continents", "source", {"World": "Generated", "Style": "Survey", "Tints": "0.5", "Seed": "42"}),
    ("over-clip", "over", {}),
    ("over-moon-neon", "over", {"World": "Moon", "Style": "Neon", "Clip Show": "0.4"}),
]
GLOBE_CASES = {"moon-defaults", "mars-map-neon", "earth-survey", "earth-globe-flood", "pluto-far-side", "venus-manual",
               "over-moon-neon"}
# The share of pixels that must be within one level of 255 of the plugin's.
THRESHOLD = 0.995


def run(cmd, **kw):
    result = subprocess.run(cmd, capture_output=True, **kw)
    if result.returncode != 0:
        sys.stderr.write(result.stderr.decode(errors="replace"))
        raise SystemExit(f"crosscheck: {' '.join(cmd[:3])} ... exited {result.returncode}")
    return result.stdout


def ihtest(binary, case, card, out_png):
    name, plugin, settings = case
    sets = [a for k, v in settings.items() for a in ("--set", f"{k}={v}")]
    env = dict(os.environ, ISOHYPSE_DATA_DIR=os.path.join(REPO, "data"))
    if plugin == "source":
        run([binary, "--size", f"{W}x{H}", "--frames", str(FRAMES), *sets, "--out", out_png], env=env)
        return np.asarray(Image.open(out_png).convert("RGB"))
    frame = W * H * 4
    raw = run([binary, "--over", "--pipe", "--size", f"{W}x{H}", *sets], input=card * FRAMES, env=env)
    last = np.frombuffer(raw[-frame:], dtype=np.uint8).reshape(H, W, 4)
    return last[:, :, :3]


def serve(root, port):
    class Quiet(http.server.SimpleHTTPRequestHandler):
        def log_message(self, *args):
            pass

    handler = functools.partial(Quiet, directory=root)
    server = http.server.ThreadingHTTPServer(("127.0.0.1", port), handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    return server


def full_size_site(tmp):
    """A scratch copy of demo/ whose globes are the plugin's own files."""
    site = os.path.join(tmp, "site")
    shutil.copytree(os.path.join(REPO, "demo"), site, ignore=shutil.ignore_patterns("tools"))
    table = os.path.join(site, "worlds", "table.js")
    text = open(table).read()
    for line in text.splitlines():
        m = re.search(r"file: '([^']+)', kind: 'globe', width: (\d+), height: (\d+), servedWidth: (\d+), servedHeight: (\d+)", line)
        if m:
            shutil.copyfile(os.path.join(REPO, "data", m[1]), os.path.join(site, "worlds", m[1]))
            text = text.replace(m[0], m[0].replace(f"servedWidth: {m[4]}, servedHeight: {m[5]}",
                                                   f"servedWidth: {m[2]}, servedHeight: {m[3]}"))
    open(table, "w").write(text)
    return site


def page(url, cases, card_path, out):
    spec = [dict(name=n, variant=p, settings=s, frames=f, width=W, height=H, clip=card_path if p == "over" else None)
            for n, p, s, f in cases]
    path = os.path.join(out, "cases.json")
    with open(path, "w") as handle:
        json.dump(spec, handle)
    run(["node", os.path.join(HERE, "page.mjs"), url, path, out])
    return {n: np.fromfile(os.path.join(out, f"{n}.rgba"), dtype=np.uint8).reshape(H, W, 4)[:, :, :3] for n, *_ in cases}


def compare(a, b):
    d = np.abs(a.astype(np.int16) - b.astype(np.int16)).max(axis=2)
    n = d.size
    return dict(same=float((d == 0).sum()) / n, within1=float((d <= 1).sum()) / n, within4=float((d <= 4).sum()) / n,
                over4=int((d > 4).sum()), max=int(d.max()), mean=float(d.mean())), d


def line(name, c):
    return (f"{name:<22} identical {100 * c['same']:7.3f}%  within 1 {100 * c['within1']:7.3f}%  "
            f"within 4 {100 * c['within4']:7.3f}%  >4: {c['over4']:6d} px  max {c['max']:3d}  mean {c['mean']:.4f}")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, default=8718)
    parser.add_argument("--ihtest", default=os.path.join(REPO, "build", "ihtest"))
    parser.add_argument("--keep", help="write the pictures and difference maps here")
    parser.add_argument("--half", action="store_true", help="also compare the page as it ships (half-size globes)")
    args = parser.parse_args()
    if not os.path.exists(args.ihtest):
        raise SystemExit(f"crosscheck: {args.ihtest} is missing -- cmake --build build --target ihtest")

    with tempfile.TemporaryDirectory() as tmp:
        out = args.keep or tmp
        os.makedirs(out, exist_ok=True)
        # The Over's clip: the harness's card, as the Over at Mix 0 hands it back.
        card_png = os.path.join(tmp, "card.png")
        run([args.ihtest, "--over", "--size", f"{W}x{H}", "--frames", "1", "--set", "Mix=0", "--out", card_png])
        card = np.asarray(Image.open(card_png).convert("RGBA")).copy()
        card[:, :, 3] = 255
        card_bytes = card.tobytes()
        card_path = os.path.join(tmp, "card.rgba")
        with open(card_path, "wb") as handle:
            handle.write(card_bytes)

        plugin = {c[0]: ihtest(args.ihtest, c, card_bytes, os.path.join(tmp, f"{c[0]}-plugin.png")) for c in CASES}

        site = full_size_site(tmp)
        server = serve(site, args.port)
        url = f"http://127.0.0.1:{args.port}/"
        cases = [(n, p, s, FRAMES) for n, p, s in CASES] + [("negative-one-frame-late", "source", {}, FRAMES + 1)]
        shown = page(url, cases, card_path, out)
        server.shutdown()
        server.server_close()

        failures = 0
        print(f"the page (full-size globes) against the plugin's harness, {W} x {H}, frame {FRAMES}:")
        for name, *_ in CASES:
            c, d = compare(shown[name], plugin[name])
            ok = c["within1"] >= THRESHOLD
            failures += 0 if ok else 1
            print(f"{'ok  ' if ok else 'FAIL'}  {line(name, c)}")
            if args.keep:
                Image.fromarray(np.concatenate([plugin[name], shown[name]], axis=1)).save(os.path.join(out, f"{name}-plugin-page.png"))
                Image.fromarray(np.clip(d * 32, 0, 255).astype(np.uint8)).save(os.path.join(out, f"{name}-diff.png"))
        c, _ = compare(shown["negative-one-frame-late"], plugin["moon-defaults"])
        caught = c["within1"] < THRESHOLD
        failures += 0 if caught else 1
        print(f"{'ok  ' if caught else 'FAIL'}  negative control: the Moon one frame late, {line('', c).strip()}")

        if args.half:
            half_server = serve(os.path.join(REPO, "demo"), args.port)
            half_cases = [(n, p, s, FRAMES) for n, p, s in CASES if n in GLOBE_CASES]
            os.makedirs(os.path.join(out, "half"), exist_ok=True)
            half = page(url, half_cases, card_path, os.path.join(out, "half"))
            half_server.shutdown()
            half_server.server_close()
            print("\nthe page as it ships (globes at half size) against the plugin (not a pass/fail):")
            for name, *_ in half_cases:
                c, _ = compare(half[name], plugin[name])
                print(f"      {line(name, c)}")
                if args.keep:
                    Image.fromarray(np.concatenate([plugin[name], half[name]], axis=1)).save(os.path.join(out, "half", f"{name}-plugin-page.png"))

    print()
    print("PASS" if failures == 0 else f"FAIL ({failures})")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
