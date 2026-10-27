# isohypse

Contour lines of real worlds, swept through by a line climbing from the lowest ground to
the highest, for Resolume Arena/Avenue, as two FFGL plugins from one core. C++/GLSL,
CMake MODULE → universal `.bundle` (macOS) + Windows `.dll`. MIT (the code); the maps
are NASA, USGS, NOAA and the Terrarium tiles' sources, credited in `data/SOURCES.md`.

| | id | display name | bundle | type |
| --- | --- | --- | --- | --- |
| a world's contours on black, paper or nothing | `IH01` | `SW Isohypse` | `Isohypse.bundle`, `com.stoatworks.ffgl.isohypse` | source |
| the same over the clip, or the clip as the map | `IH02` | `SW Isohypse Over` | `Isohypse Over.bundle`, `com.stoatworks.ffgl.isohypse.over` | effect |

Read `AGENTS.md` before touching the level arithmetic (`Datum`, `levelOf`), the line
distance (`|level − k| / |∇level|`), the sweep's ages (`SweepClock::AgeOf` and the
shader's `ageOf` are one function in two languages), or the World/Place dropdowns
(append-only).

## Commands (CMake)
- Configure: `cmake -B build -DCMAKE_BUILD_TYPE=Release`
- Fast dev build: add `-DCMAKE_OSX_ARCHITECTURES=arm64`
- Build: `cmake --build build -j8`
- Install to Resolume: `cmake --install build` (both bundles into Arena's Extra Effects)
- Render offline: `./build/ihtest --out /tmp/frame.png` (the Moon, one second in). The
  summary line gives the mean, the peak, where the sweep is, the lines and the datum.
- Another world: `--set "World=Mars"`, `--set "World=Place" --set "Place=Everest"`,
  `--set "World=Generated" --set "Terrain=Craters"`; a look: `--set "Style=Plotter"`
- The Over on the harness's card: `./build/ihtest --over --out /tmp/over.png`
- List parameters (and every dropdown's elements): `./build/ihtest --list` (`--over`)
- Press something later: `--at 90 "Restart=1"`; render longer: `--frames 600`
- Film: `./build/ihtest --film 600 --size 1280x720 --script cues | ffmpeg -f rawvideo -pix_fmt rgba -s 1280x720 -r 60 -i - out.mp4`
- A clip through it: `ffmpeg -i in.mov -vf scale=1280:720,fps=60 -f rawvideo -pix_fmt rgba - | ./build/ihtest --over --pipe --size 1280x720 | ffmpeg …`
- Remake the maps: `python3 tools/make-worlds.py` (downloads ~1 GB; writes `data/` and
  `source/WorldTable.inc` together, never one without the other)

## Verify
- Everything: `tools/verify.sh` (set `TMPDIR` to a scratch directory in a sandboxed
  session or `glslc.sh` hangs on its heredoc; `OXBOW=` points at an oxbow binary if
  `~/Projects/resolume/oxbow/build/oxbow` isn't built).
- **No GL** (`--offline`): the sweep's ages and clock, the mappings, the gap filler, every
  world decoding at its table's size, dropdown names ≤ 20 characters.
- **The contours**: `--lines` (a ramp crosses Lines contours, each at ( k − ½ ) W / Lines),
  `--width` (one width on slopes 8:1 apart), `--flat` (a flat clip draws nothing).
- **The sweep**: `--sweep` (Manual's line at its level), `--trail` (every contour's
  strength from `AgeOf`, every mode), `--clock` (Sweep Time, Restart, Sweep Bars).
- **The worlds**: `--geo` (Everest, the Mariana Trench, Olympus Mons, Hellas, Maxwell
  Montes… at their heights, and where the globe and the map put them), `--worlds` (every
  globe and place loads and draws), `--generated`, `--async` (the plugin's own loading:
  the first world on the first frame, no blank frame while a new one decodes).
- **The rest**: `--globe` (the disc's radius and limb), `--alpha`, `--state`, `--resize`,
  `--controls` (no dead control).
- The checks can fail: `./build/ihtest --negative` (5 wrong models), `tools/mutate.sh`
  (13 mutants).
- Render cost: `./build/ihtest --bench`.
- **CI** (`.github/workflows/ci.yml`): macOS builds arm64 and runs `--list`, `--offline`,
  the geometry checks on the software renderer if the runner has one, and `glslc`;
  Windows builds both DLLs and runs `tools/wincheck.ps1` on each (loads it, reads
  plugMain's info and parameters, reads every world back out of the DLL's RCDATA).
  `release.yml` builds the archives (each with ATTRIBUTIONS.md and SOURCES.md) and
  publishes on a `v*` tag.
- `IHTEST_RENDERER=software` runs any of it on Apple's software renderer.

## Notes
- **Three passes and a glow**: map (MRT: the picture premultiplied, the light for the
  glow), glow (quarter raster, 4×4 down, Gaussian twice), composite. Generated and Clip
  first draw their heightfield into an RG16 mipmapped target. No state survives a frame
  in GPU buffers but the loaded world.
- **A world is a file**: `data/<name>.png`, 16-bit grey, linear between the table's min
  and max metres. Globes equirectangular, column 0 at −180°, row 0 the north pole; places
  square Web Mercator tiles, north up. The first is decoded before the first frame; later
  ones off the GL thread, the old one on screen until the new one is up.
- All host parameters are 0..1 and mapped in `Controls.cpp`; options hold the element
  index; Restart acts on the rising edge.
- GLSL reserved words (`patch sample input output filter common active half layout flat
  packed smooth`, `noise1`–`noise4`) must not be identifiers; `verify.sh` greps for them.
  Every shader piece stays under 15 000 bytes (MSVC C2026).
- Override `SetTextParameter` to return `FF_SUCCESS` for the About block, or no host can
  instantiate the plugin.
- `isohypse_core` is an OBJECT library: each plugin registers itself from a file-scope
  constructor nothing references.
- The macOS build must be universal. Check with `lipo`, never the build log.
- **Generated, do not edit here:** `source/StoatworksAbout.h` (stoatworks-backend's
  `sync-about.py`, from the website's projects.json), `ATTRIBUTIONS.md` (the backend's
  `sync-attributions.py`; the map credits are its `data` entries in
  `attributions/derived.json`), `.github/FUNDING.yml` and `.github/ISSUE_TEMPLATE/` (the
  backend's sync scripts), and `source/WorldTable.inc` with `data/*.png`
  (`tools/make-worlds.py`). A new map's credit goes in `data/SOURCES.md` and in
  derived.json's `data` list together.
