# isohypse

> **AI-assisted project.** This codebase was created with [Claude](https://claude.com/claude-code)
> (Anthropic), directed and reviewed by a human author. It has been loaded into
> **Resolume Arena 7.28.1 on macOS** and driven there over Arena's REST API: both plugins
> register, every one of the source's controls changes Arena's output, Sweep Bars follows
> Arena's own tempo, and the Over at Mix 0 hands back its clip bit for bit. On Windows the
> fleet's Arena gate ran the release build in **Resolume Arena 7.27.1**, on software
> rendering: 15 checks passed, none failed, and every testable control but Spin changed the
> picture. Everything else
> below is measured by an offline harness that drives the real plugin classes in a
> headless GL context on this Mac's GPU: a ramp crosses **exactly** the number of contours it should,
> each **within 0.013 px** of where the arithmetic puts it; a contour is the same width,
> **within 3%**, on slopes eight times apart; every contour's strength behind the sweep
> matches the CPU's own model of the sweep **within 0.0005**, in all five modes; and
> Everest, the Mariana Trench, Olympus Mons, Hellas, Maxwell Montes and the Moon's far-side
> highlands are at their heights, and where the globe and the map put them. `ihtest
> --negative` re-runs the checks against five deliberately wrong models and
> `tools/mutate.sh` changes one character of the shipped shaders and code thirteen times;
> every one is caught. A control sweep fails if any parameter does nothing.

Contour lines of real worlds, for Resolume Arena/Avenue, swept through by a line climbing
from the lowest ground to the highest: the Moon, Mars, Earth and the rest of the rocky
solar system from their missions' own height maps, twenty-two places on Earth, a world it
makes up, or your clip. Two FFGL plugins: **SW Isohypse**, a source, and **SW Isohypse
Over**, an effect that draws the same over the clip or reads the clip itself as the map.

An isohypse is a line of equal height. It is the proper word for a contour, and it was
free.

![Four frames: the Moon as a globe in white contours on black with an orange sweep line climbing through its basins; Earth as a survey map with green land, blue sea, brown contours and hill-shading; the Everest massif as a pen plot on cream paper, the sweep outlining the summit ridge in red; and a video clip of a circuit board turned into white contour lines over the dimmed picture](docs/hero.png)

<sub>Rendered by the plugin's offline harness (`ihtest`), not captured from Resolume. The
Moon at the defaults; Earth in Survey with tints and shade; Everest in Plotter with a
little Wobble; the Over on Resolume's own *Beat 003* clip.</sub>

<!-- downloads:start -->

## Download

**[v0.1.0](https://github.com/stoatworks-labs/isohypse/releases/tag/v0.1.0)** — prebuilt for macOS and Windows. Pick your platform:

<details>
<summary><b>macOS</b> — Universal (Apple Silicon + Intel)</summary>

| Build | Download | Size |
| --- | --- | --- |
| Universal (Apple Silicon + Intel) · .dmg disk image | [`isohypse-0.1.0-macos-universal.dmg`](https://github.com/stoatworks-labs/isohypse/releases/download/v0.1.0/isohypse-0.1.0-macos-universal.dmg) | 82 MB |
| Universal (Apple Silicon + Intel) · .zip archive | [`isohypse-macos-universal.zip`](https://github.com/stoatworks-labs/isohypse/releases/latest/download/isohypse-macos-universal.zip) | 82 MB |

</details>

<details>
<summary><b>Windows</b> — x64</summary>

| Build | Download | Size |
| --- | --- | --- |
| x64 · .exe installer | [`isohypse-0.1.0-windows-x86_64-setup.exe`](https://github.com/stoatworks-labs/isohypse/releases/download/v0.1.0/isohypse-0.1.0-windows-x86_64-setup.exe) | 82 MB |
| x64 · .zip archive | [`isohypse-windows-x86_64.zip`](https://github.com/stoatworks-labs/isohypse/releases/latest/download/isohypse-windows-x86_64.zip) | 81 MB |

</details>

All builds, checksums and release notes: [github.com/stoatworks-labs/isohypse/releases](https://github.com/stoatworks-labs/isohypse/releases).

macOS builds are signed and notarised and open normally. The Windows builds are unsigned, so SmartScreen warns once.

<!-- downloads:end -->

## The one idea

A contour is where the height is a whole number of intervals. The plugin counts height in
intervals — call it the level — so **Lines** is how many contours there are from the
world's lowest point to its highest, and a pixel is on contour k when its level is within
half a line of k. Half a line in *pixels*: the distance is the level's distance from k over
how fast the level changes across the screen. That is what keeps a line the same width on
a cliff as on a plain, which a pen does without thinking about it and a threshold does
not.

The **sweep** is one more contour whose level moves: up through the world (Rise), down
(Fall), both (Bounce), or where you put it (Manual). Every other contour is drawn as bright
as how recently the sweep crossed it, so with **Floor** at 0 a Rise draws the map from the
bottom up, one level at a time, the way Sebastian Lague's
[pen plotter](https://youtu.be/qyNWDzO6kKQ) draws the Moon. Turn **Flood** up and it is
the sea rising instead.

## Worlds

- **Earth** (NOAA ETOPO 2022, land and sea floor, 4096 × 2048), **Moon** (LRO LOLA),
  **Mars** (MGS MOLA), **Mercury** (MESSENGER), **Venus** (Magellan), **Ceres** and
  **Vesta** (Dawn), **Pluto** and **Charon** (New Horizons), each 2048 × 1024 but Earth.
  Venus has Magellan's wider gaps (the slivers are filled from their edges); Pluto and
  Charon only the side New Horizons saw (Pluto's is round Longitude 180°). The unmeasured
  ground is left blank rather than made up. Ceres and Vesta are squashed, so their heights
  are measured from their ellipsoids, or every contour would be a line of latitude.
- **Place**: twenty-two tiles of Earth from the Terrarium elevation tiles, each 512 × 512 —
  London, Edinburgh, Snowdon, New York, San Francisco, Los Angeles, Vancouver, Mount
  Rainier, the Grand Canyon, Crater Lake, Rio de Janeiro, Cape Town, Lisbon, Naples &
  Vesuvius, the Matterhorn, Everest, Kilimanjaro, Mount Fuji, Hong Kong, Wellington,
  Sydney and Hawaii's Big Island.
- **Generated**: a world of its own, from noise on a sphere — *Continents* (with a sea),
  *Ridges* (mountain chains) or *Craters* (a battered moon), with a Seed, a Feature Size, a
  Roughness and a **Morph** that keeps it changing.
- **Clip** (the Over): the clip's brightness as height. Live video becomes terrain; a still
  of any greyscale heightmap becomes a map. That is also the way to Middle-earth, which is
  not bundled: every heightmap of it is drawn from Tolkien's maps, and those are the
  Tolkien Estate's to share, not mine.

Gas giants have no ground to contour. Jupiter was asked.

## Controls

- **World:** World, Place, View (*Map*, *Globe*), Longitude, Latitude, Zoom (1 to 64),
  Spin (±60° a second: a globe turns, a map scrolls, a place rotates), Rotation.
- **Contours:** Lines (4 to 300 across the world), Index Every (a heavier contour every
  n-th; 0 for none), Line Width (0.4 to 10 px at 1080 lines, scaled with the raster),
  Smooth (blurs the heights before they are contoured), Sea Fill and Depths (Earth's sea
  and its contours).
- **Sweep:** Sweep (*Off*, *Rise*, *Fall*, *Bounce*, *Manual*), Sweep Time (0.5 to 120 s a
  pass), Sweep Bars (*Free*, or a pass locked to 1 to 64 of the host's bars), Sweep Level
  (Manual), Sweep Glow, Trail (how long a line stays lit after the sweep, in passes), Floor
  (how bright a line is when it hasn't been swept lately), Flood, Flow (every contour
  drifting up or down, ±4 levels a second), Restart.
- **Look:** Style (*Ink*, *Plotter*, *Neon*, *Survey*), Hue (the sweep's colour, and the
  lines' in Neon), Height Colour, Glow, Shade (hill-shading), Tints (each band between two
  contours in its height's colour), Wobble (a hand on the pen), Background (the source
  only: 0 leaves the world on transparency, to layer; the Over's ground is its clip).
- **Generated:** Terrain, Seed, Feature Size, Roughness, Sea, Morph.
- **Clip** (the Over only): Clip Show (how much of the picture stays under the lines),
  Invert, Mix.

The first world is decoded before the first frame (a moment's wait as the plugin is
added); after that a new world is decoded in the background and the old one stays on
screen until it is ready, so changing World mid-show doesn't drop to black. Earth takes a moment longer than the
others; it has a sea floor.

## Status

**v0.1.0, released 28 October 2026: early days.**

### Measured offline, on macOS

`tools/verify.sh` passes on this machine (Apple M4 Max, macOS 26.4.1) against a fresh
universal Release build (27 October 2026). What it establishes:

| check | result |
| --- | --- |
| `--lines` | a ramp 0..1 at Lines 8, 20 and 50: exactly 8, 20 and 50 contours, each within **0.013 px** of ( k − ½ ) W / Lines |
| `--width` | slope rising 8:1 across the frame, Line Width 1 to 6: every contour within **3%** of its width |
| `--flat` | seven flat clips, black and white included: no contour drawn |
| `--sweep` | Manual at four levels: the sweep's line within **0.013 px** of Level × W |
| `--trail` | Rise, Fall, Bounce both ways, Manual: every contour's strength within **0.0005** of ( 1 − age / Trail )², the age from `SweepClock::AgeOf` |
| `--geo` | eleven features of Earth, Mars, the Moon and Venus at their heights from the data, and flooded or dry as their height says at the centre of a globe, of a map, and at their own pixel of the whole map |
| `--worlds` | all 9 globes and 22 places load, match their table and draw |
| `--generated` | each terrain draws; a seed is a different world; Morph moves it, no Morph holds it still |
| `--async` | the plugin's own loading: the Moon on the very first frame; switched to Earth, every frame until Earth is up (~110 ms) still draws the Moon |
| `--globe` | the disc's radius within **0.001 px** of 0.92 × Zoom × half the height; its limb antialiased |
| `--clock` | a pass in Sweep Time to **1e-9**; Restart; a pass on 1 and 4 bars follows a synthetic host's bar phase to **3e-8** of a pass |
| `--alpha`, `--state`, `--resize` | transparent off the world with Background 0, premultiplied; the host's GL state comes back; a new raster under a running instance |
| `--controls` | all **41** controls change the picture where they should, and each of the **37** the Over shares with the source changes the Over's picture too |
| `--negative` | **5** deliberately wrong models, **5** caught |
| mutation | **13** one-character mutants of the shipped GLSL and C++, **13** caught |
| shaders | all 7 compile through `glslc`, not merely through Apple's driver |
| software renderer | `--lines`, `--width`, `--flat`, `--sweep`, `--trail`, `--globe`, `--alpha`, `--state` and `--clock` pass again on Apple's software renderer |
| the bundles | both universal (`x86_64 arm64`), export `plugMain`, ad-hoc sign, carry all 31 maps; `oxbow` reports `SW Isohypse` / `IH01` / `source` and `SW Isohypse Over` / `IH02` / `effect` and renders 120 frames through both, reading the maps from the bundle |

### In Resolume Arena, on macOS

Arena 7.28.1 (revision 26535) on this Mac, 27 October 2026, the universal bundles from
`tools/verify.sh` installed into Extra Effects, driven over Arena's REST API and read back
from its own snapshots of the composition and of each layer:

| check | result |
| --- | --- |
| load | both bundles load; Arena registers `SW Isohypse` / `IH01` as a source and `SW Isohypse Over` / `IH02` as an effect; nothing in Arena's log or the plugin's |
| params | the source's 42 controls as Arena lists them match the plugin's, name for name, in order, with their defaults; Index Every and Seed arrive with their real ranges (0..10, 0..999); the Over adds Arena's own Opacity to its 45 |
| controls | all **38** of the source's controls change Arena's output (static controls against a still frame, whose floor is 0.000; Spin, Flow, Sweep, Sweep Time, Sweep Bars and Morph by motion; Restart by its jump) |
| bars | Sweep Bars at 1 bar and 120 BPM moves; with Arena's tempo paused it stops dead (0.000) while Free carries on; at 30 BPM frames a whole bar apart match (0.21) where half a bar apart differ (5.95) |
| Over | on Resolume's own *Beat 003*: World defaults to Clip; the clip contoured, the Moon over it, Neon with a Rise; at Mix 0 the output is the bare clip to the bit |
| raster | the source clip's own resolution changed under the running instance: the globe 664 px across at 720 lines (0.92 × 720 = 662, the limb's antialiasing either side) and 994 at 1080 (994) |

Two things that are Arena's and not the plugin's: a source clip keeps the resolution it was
made at when the composition changes size (its resize is *Original*, so it shows at that
size), and a clip smaller than the composition reaches an effect as the composition's
frame with the clip inset in it, so the Over's worlds draw past the clip's edges.

Those runs were of the build before registration. Since then the About block has gained
its *User guide* button, and the Over no longer lists Background, which it never read (its
ground is the clip, mixed in by Clip Show): the source has 43 parameters and the Over 45,
and Arena hasn't seen that build.

**Cost** on this Mac's GPU (`ihtest --bench`, ms a frame, `glFinish` timed):

| plugin | what | 1080p | 4K |
| --- | --- | --- | --- |
| SW Isohypse | the Moon, defaults | 0.44 | 1.35 |
| SW Isohypse | Earth, map, Glow 1 | 0.46 | 1.76 |
| SW Isohypse | Generated, morphing | 1.87 | 2.91 |
| SW Isohypse | Generated craters, morphing | 3.31 | 4.28 |
| SW Isohypse Over | the clip as the map | 0.49 | 1.88 |

### On Windows, in CI

`ci.yml` builds both DLLs on GitHub's `windows-2022` (MSVC, GLEW static from vcpkg) on every
push to main and every pull request, and `tools/wincheck.ps1` loads each one as a host would:
`SW Isohypse` / `IH01` / source with 43 parameters and `SW Isohypse Over` / `IH02` / effect
with 45 (42 and 45 before the changes above), the
names read back as the sixteen characters a host reads, and all 31 maps read out of each
DLL's own resources, byte for byte the files in `data/`. First green 27 October 2026. A
Windows runner has no GL, so nothing there draws a frame. `release.yml` runs the same check
on `windows-latest`, whose build left the count's high half non-zero and showed the check
was reading it whole; it reads the low 32 bits now, as a host does.

### In Resolume Arena, on Windows

The fleet's Arena gate (`plugin-bench`) ran CI's release build of main 9665a6a in Resolume
Arena 7.27.1 on win-lab, 28 October 2026: software rendering (Mesa llvmpipe), no GPU. Both
DLLs load; `SW Isohypse` registers as `IH01`, a source, and `SW Isohypse Over` as `IH02`, an
effect, each in Arena's own list; all 43 and 46 controls (the Over's counting Arena's
Opacity) match their names, order, types, ranges and defaults; the two 16-character names
arrive whole; both render; Arena's log stayed clean and Arena stayed up. **15 passed, 0
failed, 2 skipped.** Against still pictures, 34 of the source's 35 testable controls and all
38 of the Over's changed the picture; Spin was inconclusive, as a turning globe never holds
still, and Sweep Time and Sweep Bars set the pace of a moving sweep, which two stills can't
show (Arena on macOS followed them by motion). Software rendering says nothing about a GPU or
about speed.

**Not tested anywhere else.** No GPU but this Mac's has run it. The user guide is
[docs/USER-GUIDE.md](docs/USER-GUIDE.md); there is no browser demo or preset yet.
Each bundle carries every map, 40 MB of them.

## Build

Needs CMake 3.15+, a C++17 compiler and the FFGL SDK submodule:

```bash
git submodule update --init --recursive
```

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
```

```bash
cmake --build build -j8
```

```bash
cmake --install build
```

The last one puts both bundles into `~/Documents/Resolume Arena/Extra Effects`. The macOS
build is universal (Apple Silicon and Intel). On Windows, GLEW comes from vcpkg
(`vcpkg.json`).

The maps are in `data/`, made by `tools/make-worlds.py` (about a gigabyte of downloads,
mostly Earth and Mars); `data/SOURCES.md` has every source, its checksum and what was
done to it.

## Building and testing

The harness is `build/ihtest`:

```bash
./build/ihtest --out /tmp/frame.png --set "World=Mars" --set "Style=Neon"
```

```bash
./build/ihtest --over --out /tmp/over.png
```

```bash
tools/verify.sh
```

`tools/verify.sh` is everything: shader hygiene, `glslc`, a fresh universal build with no
warnings, both bundles through `lipo` and their plists, every map inside each, every check
on this GPU and most of them again on Apple's software renderer, the pipe, the negative
controls, the mutants and the bench. `CLAUDE.md` lists what each check measures; `AGENTS.md` says why
the plugin is built the way it is.

## License

MIT for the code. See [LICENSE](LICENSE) and [ATTRIBUTIONS.md](ATTRIBUTIONS.md). The maps
are public-domain NASA, USGS and NOAA products, and the Terrarium tiles' sources, some
under CC BY and Open Government Licences: their credits are in
[data/SOURCES.md](data/SOURCES.md) and ATTRIBUTIONS.md, and travel with the plugin.

<!-- attributions:start -->
This project is built on other people's work — see [ATTRIBUTIONS.md](ATTRIBUTIONS.md).
<!-- attributions:end -->
