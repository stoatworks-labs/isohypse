# AGENTS.md — isohypse

The why. `CLAUDE.md` has the commands.

Built 2026-10-27 at Allan's request, from a showmode session: "a new resolume plugin that
emulates the countour sweeps as shown in https://youtu.be/qyNWDzO6kKQ using the eath (or a
spesific city/map), the moon, Middle Earth, the rest of the solar system etc... as maps,
posssibly also with a self-height-map generating mode too to allow arbitrary
maps/applying to live video under". The video is Sebastian Lague's *I Made a Pen Plotter
to Draw Maps of the Moon*: marching-squares contours of the Moon, Mars and Earth, drawn
level by level. Nothing of his is used here but the idea; the code and the look are new.

An isohypse is a line of equal height: the cartographer's word for a contour.

## The one idea

A contour is where the height is a whole number of intervals. Call the height in
intervals the LEVEL, `level( h ) = ( h − Datum ) · Lines`, with h normalised to the
world's own range (0 its lowest point, 1 its highest), so Lines is the number of
contours from the bottom of the world to the top. A pixel is on contour k when its level
is within half a line width of k — measured in PIXELS: the distance is
`|level − k| / |∇level|`, the gradient in levels per pixel by central differences one
pixel either side, taken through the projection (so it holds on a globe's limb and a
turned map alike). That makes a contour the same width on a cliff and on a plain, which
is what a pen does and what a threshold of the level alone does not (`--width`, and the
negative control that measures in levels instead).

The SWEEP is one more contour, at a level that moves: up through the world's range (Rise),
down (Fall), both (Bounce), or where Sweep Level says (Manual). Every other contour is
drawn at the strength of how recently the sweep crossed it: `( 1 − age / Trail )²` over a
Floor. `SweepClock::AgeOf` (C++) and `ageOf` (GLSL) are the same function; `--trail`
holds every contour on a ramp to the CPU's copy, in every mode and both ways through a
bounce. A Rise with Floor 0 draws the map from the bottom up, Lague's plotter in reverse
order of pen; with Flood up, it is the sea rising.

## Decisions, and why

- **Datum.** A world with a sea puts a contour on sea level (the coast, always drawn at
  index weight). A world without one puts its contours half an interval off the bottom:
  otherwise the lowest point, a black clip, or a flat floor lies exactly on a contour and
  a plateau draws a solid sheet. A second guard refuses a line where the five samples are
  all one height. `--flat`; the negative control drops both.
- **Cubic B-spline heights, mipmapped.** A contour through bilinear heights kinks at every
  texel edge, which is what an enlarged map shows first. The B-spline is C2 (GPU Gems 2,
  ch. 20, four bilinear taps), and the level of detail is the pixel's footprint plus
  Smooth, so a whole world at 1080p reads a pre-averaged map rather than aliasing.
- **Lines fade when they crowd.** Closer than 2.5 px (or 1.5 widths) a contour fades out;
  index contours (every Index Every-th) survive five times closer. A steep limb or a
  zoomed-out mountain range keeps its index contours and loses the rest, as a printed map
  does.
- **Bars.** Sweep Bars locks a pass to the host's bars, counted from the bar phase
  wrapping (FFGL's `SetBeatInfo`); two layers on the same setting agree, and Restart moves
  the start to the bar it is pressed in. A host that sends no tempo runs 120 BPM on the
  plugin's own clock. `--clock` drives a synthetic host a third of a bar out.
- **Worlds load off the GL thread** — all but the first. Earth is 4096 × 2048 and takes a
  moment to decode; the last world stays on screen until the new one is uploaded, so a
  change of World mid-show never goes black. The first world is decoded before the first
  frame: async from the start, oxbow's 120-frame selftest finished before the Moon had
  decoded and drew nothing, which in a host is a black layer and a black thumbnail. The
  harness loads synchronously; `--async` runs the plugin's own way.
- **The dropdowns are append-only.** A composition stores the element's index.
- **Places cover the frame at Zoom 1**, and a tile's edge fades over 2% of it, so a city
  turned by Spin reads as a sheet laid down. Spin turns a place; it turns a globe or scrolls
  a map.
- **Over, Clip.** The clip's luma (of the sRGB values: the height is what the eye reads as
  bright) into an RG16 heightfield at the clip's size, mipmapped every frame, so Smooth
  takes the video's noise out before it is contoured. The default Smooth is higher on the
  Over for that reason.
- **Middle-earth is not bundled.** Every Middle-earth heightmap there is is derived from
  Tolkien's maps, which are the Tolkien Estate's, and the name is Middle-earth
  Enterprises' mark. The Over's Clip takes any greyscale heightmap — a fan map of Arda, a
  D&D campaign's, a photograph — as a still clip, and Generated makes worlds of its own.
- **Generated** is noise on the unit sphere drawn into an equirectangular map (2048 × 1024,
  every frame only while Morph moves it), so it is a globe or a map like the real ones,
  with no seam and no pinched pole. Continents has a sea, at Sea; Ridges and Craters don't.
- **The data**: see `data/SOURCES.md` for every product, its URL, its checksum, its
  processing and its credit. Globes are area-averaged (never decimated), which lowers the
  peaks: Earth at 4096 × 2048 is ~10 km a pixel and Everest's pixel is 6,364 m. Judgement
  calls in `tools/make-worlds.py`, each a constant there:
  - Ceres and Vesta heights are above their reference ELLIPSOIDS (482 × 446 km, DLR;
    285 × 229 km, Jaumann 2012), not spheres: the files store radius, and on a squashed
    body a radius's contours are lines of latitude.
  - Vesta is in the IAU Claudia Double Prime system (150° east of the older Dawn
    "Claudia" maps); Ceres in the Kait system.
  - Venus's altimetry slivers up to ~9 output pixels wide are filled from their edges;
    wider swaths and the poles stay unmeasured.
  - The Terrarium tiles carry bad-value clusters along coasts (New York −1,665 m, Sydney
    +3,173 m): replaced by their local 7 × 7 median. Mountain tiles come through untouched.
  - A place has a sea where at least 2% of the tile is at or below 0.5 m and reaches the
    tile's edge. Most coastal tiles' sea is exactly 0 m and flat, so **the coast is drawn at
    +0.5 m** (`WorldInfo::kCoastMetres`), between the sea and the land.
- **pds-geosciences.wustl.edu is blocked from this machine** (its firewall answers
  "Gateway BOTNET Filter Alert"): the Moon came from imbrium.mit.edu (the LOLA team's copy
  of the same PDS volume), Mars from the USGS MOLA mosaic, range-read row pairs only.

## Open questions

- Loaded in Resolume only on macOS (Arena 7.28.1, 27 October 2026, over REST: see the
  README's Status). On Windows the DLLs are built and loaded in CI (`tools/wincheck.ps1`:
  info, parameters, every RCDATA world byte for byte) but have never drawn a frame, there
  being no GL on the runner, nor met Resolume. In Arena a source clip keeps the resolution it was made at when the
  composition is resized, and a clip smaller than the composition reaches an effect inset
  in a composition-sized frame: both Arena's, both look like plugin bugs at first sight.
- The data is 40.5 MB (Earth alone 11.9 MB) and each bundle carries all of it. Earth at
  2048 × 1024 would make it about 31 MB, the other globes at 1024 × 512 about 25 MB; or one
  shared data folder.
- Pluto and Charon are only half seen: their unmeasured ground is left blank. Pluto's
  side to look at is Longitude 180° (Sputnik Planitia straddles the map's seam; the globe
  doesn't mind).
- The sweep covers the world's whole range; zoomed into a region, most of a pass is spent
  out of view. A "Sweep Range: view" would need the visible range, a reduction per frame.
- No presets.

## The browser demo (2026-10-28)

`demo/` is <https://isohypse-demo.stoatworks-labs.com>, built to the fleet's `resolume-demo`
kit rules (`~/Projects/resolume/specs/DEMO-BRIEF.md`) in the demo sweep of 28 October 2026,
from slipstream's. Both plugins (`kind: ['source', 'effect']`, a Plugin dropdown). What a
reader of it must know:

- **The shaders are the plugin's**, all nine pieces plus kVersion, spliced by
  `demo/tools/check_shaders.py --write` into `demo/shaders.js` and compared character for
  character, with the JOIN on both sides (`Assemble()` over an initializer list, the map pass
  as kMapLibrary + kMapFragment, InitGL's six stages, plugin.js's copy). All six programs
  compile and link in WebGL2 as spliced: nothing needed `port()` beyond its usual version and
  precision lines. `tools/verify.sh` runs the check.
- **The maps are the plugin's, the globes halved.** `demo/tools/make_worlds.py` writes
  `demo/worlds/`: the places byte for byte (git stores them once), the globes as the exact
  2 × 2 mean (Earth 2048 × 1024, the rest 1024 × 512; on Venus, Pluto and Charon a pixel is
  measured where two of its four were, the rule make-worlds.py used), and `table.js` from
  `source/WorldTable.inc`. 16 MB in all, 3.1 MB the largest file. `--check` (in verify.sh)
  fails when `data/` or the table changes and `demo/worlds/` does not. Remake both together.
- **The PNGs are decoded in plugin.js**, a 16-bit greyscale decoder over
  `DecompressionStream('deflate')`: the browser's own decoder hands back 8 bits, which would
  terrace every map. It refuses anything make-worlds.py does not write.
- **The heights are RG16 where the browser has `EXT_texture_norm16`** (desktop Chrome does),
  else RG32F (exact, needs `OES_texture_float_linear`), else RG16F (half float: on a Grand
  Canyon test 99.4% of pixels within a level of RG16, where RG32F was 99.999%). The note
  under the picture says which.
- **The CPU half is a port**: Controls.cpp, SweepClock.cpp, Worlds.cpp's dropdowns, gap filler
  and texels, Isohypse.cpp's styles, orientation and the CPU half of ProcessOpenGL (between
  the PORT and FRAME markers in `demo/plugin.js`). Change those and you change the port too.
  The plugin's table size is the texture's size there; here the texture is the served file,
  so HeightSize is the served size (half for a globe) -- the one deliberate departure.
- **No first-frame load.** Every world is fetched, the first included, so the page draws its
  ground alone until the Moon arrives; a new world leaves the old one up, as the plugin does.
- **No host tempo**: Sweep Bars runs on the plugin's own 120 BPM fallback. Restart is a toggle
  the page releases after one frame; Index Every and Seed are dropdowns of every value.
- **The page's own `differences` list says all of this on the page.** Keep it true.

How it was verified, and how to again (it needs this Mac's GPU, Chrome and node, so verify.sh
does not run it):

- `demo/tools/crosscheck/crosscheck.py --half` (after `cmake --build build --target ihtest`):
  twelve cases at 1280 × 720, frame 60 -- the Moon at the defaults, Mars Neon map, Earth
  Survey map with Tints and Shade, Earth globe flooding on a Bounce, Pluto's far side, Venus
  Manual in Height Colour, Everest in Plotter with Wobble, Sydney zoomed and turned, Generated
  Craters morphing and Continents in Survey, the Over on the harness's card as Clip and with
  the Moon in Neon over it -- rendered by `ihtest` and by the page (served with the plugin's
  full-size globes for the comparison) and compared pixel by pixel. 28 October 2026, on the
  M4 Max through Chrome's ANGLE/Metal: every case had at least 99.979% of pixels within one
  level of 255 of the plugin's (nine of the twelve 99.99% or more, five 100.000% to three
  places), at most 56
  pixels in a case off by more than four (the Earth globe, on contour edges), mean difference
  under 0.003. The negative control, the page one frame late, came out 88.5% within one level
  and fails. The page as it ships, on its half-size globes, was 99.68% (Venus) to 99.98% (the
  Moon) within one level of the plugin at full size, at 1280 × 720.
- Headless by hand, through `wrangler dev` (so `_headers`' CSP applied) on this machine: no
  console messages at all; Style, Zoom, World, Place, Terrain and Restart each moved the
  picture or the sweep; the Plugin dropdown gave the Over its own defaults (World Clip, Lines
  24), its World list with Clip, its Clip group and no Background; `?embed=1` drew the Moon
  bare at 1280 × 720 and `?embed=1&plugin=over&world=0&clip=ramp&style=2` Earth in Neon over
  the ramps; a 390 px wide window had no sideways scroll; `tools/` was not served.
