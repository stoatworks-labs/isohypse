# Isohypse user guide

Isohypse is **contour lines of real worlds, for [Resolume](https://resolume.com) Arena and Avenue**,
swept through by a line that climbs from the lowest ground to the highest. It comes as two FFGL
plugins in one download: **SW Isohypse**, a source, and **SW Isohypse Over**, an effect that draws a
world over your clip, or reads your clip's brightness as height and contours that instead. There are
nine globes built from their missions' own height maps (Earth, the Moon, Mars, Mercury, Venus,
Ceres, Vesta, Pluto and Charon), twenty-two places on Earth, a world it makes up as it goes, and, in
the Over, whatever you feed it.

The idea is Sebastian Lague's. His [pen plotter](https://youtu.be/qyNWDzO6kKQ) draws the Moon's
contours one level at a time, and watching the lines climb out of the basins turns out to be
compelling viewing for what is, mechanically, a very slow printer. Nothing of his is used here but
the idea. An isohypse is a line of equal height: the cartographer's word for a contour.

![Four frames: the Moon as a globe in white contours on black, an orange sweep line lighting the highlands half way up; Mars as a globe in pale blue neon contours, the sweep a white glowing line round the Tharsis volcanoes; the Grand Canyon as an atlas map, green canyon floors, brown plateau and white high ground, with brown contours and hill-shading; a cluster of pale 3D blocks on black turned into contour lines, ringed by an orange glow](guide-hero.png)

*Rendered by `ihtest`, the offline harness, rather than captured from Resolume. Top left, SW
Isohypse at its defaults four seconds in: the Moon, with the sweep half way up. Top right, Mars in
Style Neon with Hue 0.55, Longitude −80° and Spin 0, 3.3 seconds in. Bottom left, the Grand Canyon
(World Place) in Style Survey with Tints 1 and Shade 0.6, Zoom 1.13 and Spin 0, four seconds in.
Bottom right, SW Isohypse Over at its defaults on Resolume's own OrganicMotions_06 clip, two seconds
in. Each panel is a whole 1280 × 720 frame at reduced size.*

> **Before you rely on this:** it's at **v0.1.0**, the first release, and early days. It has been
> loaded into **Resolume Arena 7.28.1 on macOS** (an Apple M4 Max on macOS 26.4.1) and driven there
> over Arena's REST API: both plugins register (`SW Isohypse`, `IH01`, a source; `SW Isohypse
> Over`, `IH02`, an effect), the source's 42 parameters arrive as declared, name for name, in order,
> with their defaults, all 38 of its controls change Arena's output, Sweep Bars follows Arena's tempo
> and stops dead when the tempo is paused, and the Over at Mix 0 hands back its clip bit for bit.
> Arena's log and the plugin's stayed clean. That was the build before the About group gained its
> User guide button and the Over stopped offering a Background control it never read (the source
> now has 43 parameters and the Over 45); Arena hasn't seen this one.
>
> Everything else was measured offline by a harness (`ihtest`) that drives the plugins' own class in
> a headless GL context on the same Mac's GPU, and `tools/verify.sh` passes. A ramp crosses as many
> contours as Lines says, each within 0.013 px of where the arithmetic puts it; a contour keeps its
> width within 3% on slopes eight times apart; every contour's strength behind the sweep matches the
> CPU's own model within 0.0005 in all five sweep modes; and Everest, the Mariana Trench, Olympus
> Mons, Hellas, Maxwell Montes and the Moon's far-side highlands are at their heights, and where the
> globe and the map put them. Five models broken on purpose and thirteen one-character changes to
> the shipped code are all caught, all 9 globes and 22 places load and draw, and all 41 controls
> changed the picture in the harness's sweep, each of the 37 the Over shares with the source
> changing the Over's picture too. Nine of the checks pass again on Apple's software
> renderer.
>
> **On Windows** it has been in Resolume too. The fleet's Arena gate ran CI's release build in
> Resolume Arena 7.27.1 on software rendering, with no GPU: both plugins load, register (`IH01` a
> source, `IH02` an effect) and render, every control matches its name, order, type, range and
> default, and Arena's log stayed clean: 15 checks passed and none failed. Against still pictures,
> 34 of the source's 35 testable controls and all 38 of the Over's changed the picture; Spin turns
> the globe, so a still can't pin it down, and Sweep Time and Sweep Bars only set a moving sweep's
> pace. Software rendering says nothing about a GPU or about speed, and no GPU but this one Mac's
> has run it. Try it on a spare layer before you put it in a show.
>
> This codebase was created with AI assistance, directed and reviewed by a human author.

---

## Installing

Every download has both plugins in it: **SW Isohypse** (a source) and **SW Isohypse Over** (an
effect), as `Isohypse.bundle` and `Isohypse Over.bundle` on macOS and `Isohypse.dll` and
`Isohypse Over.dll` on Windows, along with `ATTRIBUTIONS.md` and `SOURCES.md`, which carry the maps'
credits. Get them from the
[`v0.1.0` release on GitHub](https://github.com/stoatworks-labs/isohypse/releases/tag/v0.1.0) or the
[project page](https://stoatworks-labs.com/software/isohypse/), drop both into Resolume's effects
folder and restart Resolume:

```
macOS    ~/Documents/Resolume Arena/Extra Effects/
Windows  %USERPROFILE%\Documents\Resolume Arena\Extra Effects\
```

Avenue uses the same layout under its own folder name. SW Isohypse shows up among the sources and
SW Isohypse Over in the effects browser.

The maps travel inside the plugins: in each bundle's `Contents/Resources/worlds` on macOS, and
inside each DLL on Windows. So copy a bundle whole, never just the file inside it. It also means
each plugin is about 40 MB, of which Earth is 12.

The macOS download is a universal build (Apple silicon and Intel), as a `.dmg` or a `.zip`. It's
**Developer ID-signed and notarised** by the release pipeline after publication, so the bundles
just load. If macOS refuses a download, it's from before the signing, so download it again. The
Windows download is an x64 installer (`-setup.exe`) or a `.zip`. It isn't code-signed, so the
installer trips SmartScreen once: **More info** → **Run anyway**.

---

## Start here

1. Put **SW Isohypse** in a clip slot and trigger it. That's the Moon as a globe, in white contour
   lines on black, turning slowly (a full turn every 90 seconds). Every eight seconds an orange line
   climbs out of the deepest basins to the highest ground, lighting each contour as it crosses it,
   and the lit lines fade behind it.
2. Change **World** to *Earth*. Earth has a sea, so it gets a navy fill, a white coastline and the sea
   floor's contours in pale blue. Try *Mars* too, and *Pluto*, which starts out nearly blank: New
   Horizons only saw one side of it, round Longitude 180°, and the default Spin takes 45 seconds to
   bring that round.
3. Change **View** to *Map* for the whole world laid flat. Spin now scrolls it sideways.
4. Change **Style** to *Survey*, then turn **Tints** and **Shade** up. Survey on its own is paper and
   a pale sea; Tints colours the ground by height and Shade adds the hill-shading, and between them
   it's an atlas.
5. Set **World** to *Place* and **Place** to *Everest*. A place fills the frame, and at the default
   Spin it slowly turns. Set **Spin** to 0.5 to hold it still and **Zoom** to 0.03 to push the
   tile's soft edge out of shot (0.044 if you'd rather it kept turning).
6. Set **Sweep** to *Manual* and move **Sweep Level**: the sweep goes where you put it. Turn
   **Flood** up and everything below it fills in.
7. For your own footage, put **SW Isohypse Over** on a clip. Its World starts on *Clip*: your clip's
   brightness becomes the ground, white the peaks and black the sea floor, and the contours are drawn
   over the picture. **Clip Show** at 0 leaves the lines on their own.

---

## Contours and the sweep

### The contours

A contour is where the ground is a whole number of intervals up. **Lines** sets how many contours
there are from the world's lowest point to its highest, so at the default 40 the Moon's are about
480 m apart and Earth's about 420 m. The interval follows the world, so 40 lines is 40 lines on Mars
(29 km from the bottom of Hellas to the top of Olympus Mons) and on London (140 m).

A line's width is measured in pixels, through the slope, so a contour keeps its width on a cliff and
on a plain, as a pen's does. Where the lines crowd closer than 2.5 px (or one and a half line widths,
if that's more) they fade out, and the heavier **index contours** (every fifth, at the default
**Index Every**) hold on until they're five times closer. So a steep limb or a far-off mountain range
keeps its index contours and loses the rest, as a printed map does.

Where the contours sit depends on the world:

- **A world with a sea** (Earth, thirteen of the places, and Generated's *Continents*) has a contour
  on sea level. That's the coast, and it's always drawn, at index weight, whatever Index Every says.
  The sea gets its own fill and its contours their own colour (**Sea Fill** and **Depths**).
- **A world without one** has its contours half an interval up from its lowest point. Ground that's
  flat draws nothing, so a black background, a lake or a plateau that happens to sit on a
  contour stays clear instead of filling in solid. (Crater Lake's water is flat in the data, so the
  lake is an empty patch in a ring of contours.)

### The sweep

The sweep is one more contour, in the accent colour, whose height moves:

- **Rise** climbs from the lowest point to the highest, then starts again at the bottom.
- **Fall** comes down from the top.
- **Bounce** goes up, then down, then up.
- **Manual** stays at **Sweep Level**.
- **Off** has no sweep, and every line is drawn at full strength.

A pass (one climb from bottom to top) takes **Sweep Time**, eight seconds by default, or a number of
the host's bars with **Sweep Bars**. Every other line's brightness goes by how recently the sweep
crossed it: a line it has just crossed is at full strength and fades over **Trail** (in passes),
never dimmer than **Floor**. **Sweep Glow** puts a halo round the sweep and a flash on the lines it
has just passed. In Manual nothing moves, so a line's brightness goes by its distance from Sweep
Level instead, either side.

On Earth, sea level is 62% of the way up from the deepest sea floor to the highest ground, so 62% of
every Rise happens under water. That's a lot of sea floor to sit through before anything you'd
recognise lights up. Use a shorter Sweep Time, or Manual, or a place.

### A map drawn from the bottom up

Rise and Fall go round and round: a line's age wraps at the top, so with a long Trail the top lines
from the last pass are still lit on the next pass's first frame. For a clean build, where nothing
above the sweep has been drawn yet:

1. Set **Floor** to 0 and **Trail** to 0, and press **Restart** to start a pass from the bottom.
2. Automate **Trail** from 0 up to 1 over that one pass (Sweep Time, or the bars you've set).

Each line lights as the sweep reaches it and stays lit, the newest brightest, and nothing above the
sweep shows. The oldest lines, at the bottom, fade as the pass goes on, since a line's light falls
with its age. It works best on one big hill, Mount Fuji or Kilimanjaro.

![Four frames of Mount Fuji in white contours on black: first only the low ground at the frame's edges lit, with an orange line along it; then a ring of lit contours round a dark centre; then the ring tighter round the summit; then the whole cone lit, the orange line a small ring at the top](guide-sweep.png)

*A Rise drawing Mount Fuji from the bottom up: World Place, Zoom 1.13, Spin 0, Floor 0, and Trail
automated from 0 to 1 over the first eight-second pass, rendered by `ihtest` rather than captured
from Resolume. Left to right and top to bottom, 1.6, 3.6, 5.6 and 7.6 seconds in, the sweep 0.2,
0.45, 0.7 and 0.95 of the way up. Each panel is a whole 1280 × 720 frame at reduced size.*

### Flood and Flow

**Flood** fills everything below the sweep with the style's flood colour, so with a Rise it's the
sea coming in. In Plotter and Survey the flood is blue; in Ink and Neon it's a dark shade of **Hue**.
On Earth, Manual with Sweep Level 0.6194 puts the sweep on the coast. Raise it slowly from there and
the lowlands go under, the Low Countries and the north German plain among the first. (The Caspian
is blue before you start: its shores are below sea level, and the map can't tell a lake from a sea.)

![Four maps of Europe on cream paper with a blue sea: at sea level, the coastlines as they are; at 100 m, the Low Countries, the north German plain, the Hungarian plain and the Po valley under water; at 300 m, most of eastern Europe gone; at 1,000 m, only the Alps, Pyrenees, Scandinavian mountains, Atlas, Anatolia and Iran left](guide-flood.png)

*Europe drowning: Earth as a Map (Longitude 10°, Latitude 50°, Zoom 3, Spin 0) in Survey with Sweep
Manual and Flood 1, rendered by `ihtest` rather than captured from Resolume. Sweep Level at 0 m
(0.6193), 100 m (0.6253), 300 m (0.6373) and 1,000 m (0.6791). Each panel is a whole 1280 × 720
frame at reduced size.*

**Flow** moves every contour up or down the hillside while the world stays still: above the middle
of the slider they climb, below it they sink, up to four intervals a second at the ends. The coast
stays where it is. With Sweep Off it's a cheap way to make a still map move.

---

## The worlds

**World** picks what to draw: the nine globes, then *Place* (whose **Place** dropdown picks one of
twenty-two), then *Generated*, then, in the Over only, *Clip*. Changing World or Place decodes the
new map in the background, and the old one stays on screen until it's ready, so the layer never
drops to black. Earth takes longest (about 110 ms in the harness, which is a long time in a shader
and no time at all on a stage). The very first world is decoded before the first frame, which is
the moment's pause you get when you add the plugin.

### The globes

![Nine globes in white contour lines on black, each with an orange sweep a quarter of the way up: Earth with Africa in the middle; the Moon's near side; Mars round Tharsis and the Valles Marineris; Mercury; Venus; Ceres; Vesta from below, its south-pole basin outlined; Pluto with its smooth heart-shaped plain ringed in orange, the lower right blank; Charon with its lower third blank](guide-worlds.png)

*The nine globes at SW Isohypse's defaults but Spin 0, two seconds in (the sweep a quarter of the
way up), rendered by `ihtest` rather than captured from Resolume, each frame cropped square round
its globe. Across the rows: Earth (Longitude 20°), the Moon, Mars (Longitude −80°); Mercury
(Longitude 160°, Latitude 20°), Venus (Longitude 30°, Latitude 30°), Ceres (Longitude −120°);
Vesta (Latitude −45°), Pluto (Longitude 180°), Charon. The plain grey on Pluto and Charon is ground
New Horizons never saw.*

| World | from | lowest to highest | notes |
| --- | --- | --- | --- |
| Earth | NOAA ETOPO 2022, land and sea floor, 4096 × 2048 | −10,359 to 6,367 m | has a sea; the only globe that does |
| Moon | LRO LOLA | −8,698 to 10,513 m | |
| Mars | MGS MOLA | −8,122 to 21,142 m | |
| Mercury | MESSENGER | −5,357 to 4,435 m | |
| Venus | Magellan | −2,886 to 11,367 m | the altimeter's wider gaps left blank, the thin slivers filled from their edges |
| Ceres | Dawn | −7,240 to 9,431 m | heights above its squashed ellipsoid |
| Vesta | Dawn | −22,389 to 19,581 m | heights above its ellipsoid; longitudes in the current IAU system |
| Pluto | New Horizons | −4,037 to 6,073 m | the seen side only, round Longitude 180° (Sputnik Planitia) |
| Charon | New Horizons | −14,029 to 6,864 m | the seen side only, round Longitude 0°; the southern third blank |

All but Earth are 2048 × 1024. Every map was averaged down from its source, never thinned, which
softens the peaks: Earth is about 10 km a pixel, so its highest pixel is 6,367 m (Everest's own is
6,364) and its deepest 10,359 m down. Ground nobody measured is left blank. Ceres and Vesta are
squashed, so their heights are measured from their ellipsoids; measured from the centre, every
contour would be a line of latitude. `SOURCES.md` has every source, its checksum and what was done
to it.

Gas giants have no ground to contour, so they're not here.

### Places

*Place* is a square tile of Earth, 512 × 512, from the Terrarium elevation tiles. Each one fills the
frame at Zoom 1, north up until Spin or Rotation turns it.

| Place | lowest to highest | across | a pixel | sea |
| --- | --- | --- | --- | --- |
| London | −5 to 135 m | 24 km | 48 m | |
| Edinburgh | −12 to 224 m | 11 km | 21 m | yes |
| Snowdon | 44 to 1,045 m | 12 km | 23 m | |
| New York | −37 to 124 m | 30 km | 58 m | yes |
| San Francisco | −114 to 464 m | 31 km | 60 m | yes |
| Los Angeles | −27 to 2,434 m | 65 km | 127 m | yes |
| Vancouver | −9 to 1,755 m | 51 km | 100 m | yes |
| Mount Rainier | 582 to 4,386 m | 27 km | 52 m | |
| Grand Canyon | 687 to 2,640 m | 32 km | 62 m | |
| Crater Lake | 1,642 to 2,588 m | 14 km | 28 m | |
| Rio de Janeiro | −62 to 980 m | 36 km | 70 m | yes |
| Cape Town | −29 to 1,079 m | 32 km | 63 m | yes |
| Lisbon | −4 to 216 m | 15 km | 30 m | yes |
| Naples & Vesuvius | −4 to 1,256 m | 30 km | 58 m | yes |
| Matterhorn | 1,616 to 4,353 m | 14 km | 27 m | |
| Everest | 3,418 to 8,744 m | 35 km | 68 m | |
| Kilimanjaro | 1,204 to 5,880 m | 39 km | 76 m | |
| Mount Fuji | 106 to 3,744 m | 32 km | 62 m | |
| Hong Kong | −47 to 948 m | 36 km | 71 m | yes |
| Wellington | −29 to 784 m | 29 km | 57 m | yes |
| Sydney | −60 to 317 m | 65 km | 127 m | yes |
| Hawaii (Big Island) | −5,877 to 4,155 m | 295 km | 576 m | yes |

Edinburgh, San Francisco, Rio and Hawaii have real sea floor; most of the other coastal tiles' sea
is a flat 0 m, so the coast is drawn at half a metre, between the sea and the land. London doesn't
count as having a sea: a tile needs 2% of itself at or below half a metre, reaching its edge, and
London's doesn't.

A tile's edge fades out over the last 2% of it, so a place spun or panned past its edge looks like a
sheet of paper laid on the ground colour. At Zoom 1 that fade sits on the frame's edge. **Zoom**
0.03 (1.13×) takes it out of a 16:9 frame; once the tile is turned by Spin or Rotation, 0.044 (1.2×)
keeps its corners out at any angle.

### Generated

*Generated* is a world of its own, from noise on a sphere, so it's a globe or a map like the real
ones, with no seam and no pinched poles. **Terrain** picks *Continents* (land and a sea),
*Ridges* (mountain chains) or *Craters* (a battered moon), and **Seed** picks one of a thousand of
each. **Feature Size** and **Roughness** shape it, **Sea** sets Continents' sea level, and **Morph**
keeps it changing. At Morph 1 the continents are unrecognisable after five seconds; at 0.5 it takes
ten or twenty.

### Your clip, and Middle-earth

In the Over, *Clip* reads your clip's brightness as height: white is the highest ground and black
the lowest. Live video becomes terrain, and a still of any greyscale heightmap becomes a map.

That's also the way to Middle-earth, which isn't bundled. Every heightmap of Middle-earth is drawn
from Tolkien's maps, and those are the Tolkien Estate's to share, not mine. A fan map of Arda, a
campaign map or a heightmap you've made yourself goes into a clip slot as a still like any other
picture, and the Over contours it. See *Your own heightmap*, under the Over.

---

## The styles

![Snowdon four ways: white contours on black with an orange sweep; faint grey pencil lines on cream paper with a dark orange sweep line; brown-orange neon lines on black with a white glowing sweep; an atlas map, green valleys and brown slopes up to a white summit ridge, with brown contours and hill-shading](guide-styles.png)

*Snowdon (World Place, Zoom 1.13, Spin 0) four seconds in, the sweep half way up, otherwise at the
defaults, rendered by `ihtest` rather than captured from Resolume. Top left, Style Ink. Top right,
Plotter. Bottom left, Neon. Bottom right, Survey with Tints 1 and Shade 0.6. Each panel is a whole
1280 × 720 frame at reduced size.*

**Ink** (the default). Off-white lines and white index contours on black, the sweep in Hue, the sea's
contours pale blue on navy.

**Plotter.** Dark ink on cream paper, the sweep in a darker shade of Hue, the sea pale blue-grey.
At the default Floor of 0.2 the lines the sweep hasn't reached are faint pencil; turn Floor up, or
Sweep Off, for a pen plot.

**Neon.** Coloured light on black. Hue colours the lines here (the index contours paler, the sea's
contours the opposite hue), the sweep is a warm white, and the glow is nearly twice as strong as
Ink's. Mars looks best with Hue moved off the default orange; 0.55 is a pale blue.

**Survey.** The cartographer's look: brown contours on cream paper, a pale blue sea with blue
contours. On its own it's quite plain. **Tints** paints each band between two contours in its
height's colour (blues under the sea, then greens, browns and snow), and **Shade** adds hill-shading
lit from the top left, and with both up it's an atlas.

On paper (Plotter and Survey) the glow is half as strong as Ink's.

---

## The controls

Resolume shows most sliders as 0 to 1; the ranges below are what the slider positions mean. Index
Every and Seed are whole numbers in their own ranges, Restart is a button, Invert a switch, and the
dropdowns hold their names. The source has 38 controls and the Over 40, in these groups, in this
order, with the About group last in both. Where the two plugins' defaults differ, both are given.

### World

**World** (the nine globes, *Place*, *Generated*, and *Clip* in the Over; default *Moon* in the
source, *Clip* in the Over). See *The worlds*.

**Place** (the twenty-two places; default *London*). Used when World is *Place*.

**View** (*Map*, *Globe*; default *Globe*). A globe or Generated as a ball seen from far off, or
laid flat. At Zoom 1 the globe is 0.92 of the frame's height across, and the map fits pole to pole
in the height, which on a 16:9 frame shows 320° of longitude. Places and Clip ignore it.

**Longitude** (−180° to +180°; default 0°, at the middle) and **Latitude** (−90° to +90°; default
0°, at the middle). On a globe or a map, the point in the middle of the frame. On a place they pan
instead, half a tile either way, east and north as they go up.

**Zoom** (1 to 64 times, geometrically; default 1). 0.167 is 2×, 0.333 4×, 0.5 8×. Zoomed far into
a globe you're looking at a handful of its pixels, smoothed: see *Known limits*.

**Spin** (−60° to +60° a second, squared either side of the middle so the slow end has room; default
4° a second, at 0.629, in the source and 0 in the Over). A globe turns, a map scrolls and a place
rotates. 0.75 is 15° a second and 0.5 is still. Above the middle a globe turns the opposite way to
the real Earth, and the same way as Venus; below the middle, the other way.

**Rotation** (−180° to +180°; default 0°, at the middle). Turns the whole view: the globe about the
line of sight, the map or the place in the frame.

### Contours

**Lines** (4 to 300 contours from the world's lowest point to its highest, geometrically; default
40, at 0.533, in the source and 24, at 0.415, in the Over). 0.25 is about 12 and 0.75 about 102.

**Index Every** (0 to 10; default 5). Every n-th contour is drawn 1.8 times as wide, in the index
colour. 0 for none, though a world with a sea still gets its coast at index weight.

**Line Width** (0.4 to 10 pixels at 1080 lines, geometrically, scaled with the frame's height;
default 1.4, at 0.389). 0.5 is 2 pixels. A line thinner than a pixel is drawn a pixel wide and that
much fainter. The sweep is 2.2 times this and the index contours 1.8.

**Smooth** (0 to 6 levels of blur on the heights before they're contoured, each level halving the
map's resolution; default 0.25, a level and a half, in the source and 0.45, 2.7 levels, in the
Over). Low, the contours follow every ripple; at 1 they're round, gentle curves. The Over starts
higher to take a video's noise out.

**Sea Fill** (0 to 1; default 0.5). How solidly the sea is filled with the style's sea colour.

**Depths** (0 to 1; default 0.5). The strength of the contours below sea level. 0 leaves the sea
floor bare.

Sea Fill and Depths only act on a world with a sea: Earth, the places marked *sea* in the table, and
Generated's Continents.

### Sweep

**Sweep** (*Off*, *Rise*, *Fall*, *Bounce*, *Manual*; default *Rise*). See *The sweep*.

**Sweep Time** (0.5 to 120 seconds a pass, geometrically; default 8, at 0.506). 0.25 is 2 seconds,
0.75 is 30 and 0.874 a minute. Ignored while Sweep Bars locks the pass to the tempo.

**Sweep Bars** (*Free*, then *1 Bar*, *2 Bars*, *4 Bars* and so on up to *64 Bars*; default *Free*).
Locks one pass to that many of the host's bars: a pass starts on a downbeat, and two layers on the
same setting agree. See *Time and tempo*.

**Sweep Level** (0 to 1 of the way up the world's range of heights; default 0.5). Where Manual puts
the sweep. The height in metres is the lowest point plus Sweep Level times the range, from the
tables above; Earth's sea level is at 0.6194.

**Sweep Glow** (0 to 1; default 0.6). The halo round the sweep and the flash on the lines it has
just crossed. 0 leaves a plain line.

**Trail** (0 to 1 passes; default 0.35). How long a line stays lit after the sweep crosses it, fading
as it goes. At the defaults, a line the sweep has just crossed is back down to the Floor about 1.5
seconds later.

**Floor** (0 to 1; default 0.2). How bright a line is when the sweep hasn't been near it lately. At
1 every line is at full strength and only the sweep and its glow move.

**Flood** (0 to 1; default 0). Fills everything below the sweep. Does nothing with Sweep Off.

**Flow** (−4 to +4 intervals a second, squared either side of the middle; default 0, at the middle).
Moves every contour uphill (above the middle) or downhill. 0.75 is one interval a second.

**Restart** (a button). Starts the pass again: from the bottom for Rise and Bounce, from the top for
Fall. On Sweep Bars with a tempo, it moves the start of the pass to the bar it's pressed in.

### Look

**Style** (*Ink*, *Plotter*, *Neon*, *Survey*; default *Ink*). See *The styles*.

**Hue** (0 to 1 round the colour wheel from red; default 0.07, an orange). The sweep's colour in
Ink, Plotter and Survey. In Neon it colours the lines instead and the sweep stays white. It also
tints the flood in Ink and Neon.

**Height Colour** (0 to 1; default 0). Moves each line's colour towards its height's: on a world
with a sea, blues below sea level and greens, browns and snow above; in Survey without a sea, the
land colours from the lowest ground up; otherwise a spectrum from violet at the bottom to red at
the top.

**Glow** (0 to 1; default 0.35). A soft bloom of the lines' own light. Neon's is 1.8 times as strong
and Plotter's and Survey's half.

**Shade** (0 to 1; default 0). Hill-shading lit from the top left, in the lines' colour: lit slopes
glow on a dark ground, and shadowed slopes are inked on a pale one.

**Tints** (0 to 1; default 0). Fills each band between two contours with its height's colour, from
the same palette as Height Colour.

**Wobble** (0 to 1; default 0). A hand on the pen: at 1 the lines wander up to 4 pixels (at 1080
lines) over a wave about 90 pixels long. It's subtle, a slightly unsteady hand that shows on a
monitor up close. It's fixed to the screen, so it doesn't jitter.

**Background** (0 to 1; default 1). The style's ground behind the source's world: 1 is opaque
black or paper, 0 leaves the world on transparency to layer over something else (premultiplied, as
Resolume expects of a source). A globe brings its disc with it either way. The source only: the
Over's ground is always opaque, and Clip Show does that job there.

### Generated

These act only when World is *Generated*.

**Terrain** (*Continents*, *Ridges*, *Craters*; default *Continents*).

**Seed** (0 to 999; default 7). Each one a different world.

**Feature Size** (0 to 1; default 0.5). The size of the continents, ranges and craters: bigger to
the right, about ten times bigger from one end to the other.

**Roughness** (0 to 1; default 0.5). How much fine detail sits on the big shapes.

**Sea** (0 to 1 of the way up; default 0.5). Continents' sea level. Ridges and Craters have no sea.

**Morph** (0 to 1; default 0). How fast the world changes. 0 holds it still; 0.5 makes it a different
world in ten or twenty seconds and 1 in about five. While it's moving the map is redrawn every frame,
which costs (see *Performance*).

### Clip (the Over only)

**Clip Show** (0 to 1; default 0.6). How much of your clip shows under the lines. The rest is the
style's ground, so at 0 it's lines on black in Ink and Neon, or on paper in Plotter and Survey.

**Invert** (off). On, dark is high and bright is low.

**Mix** (0 to 1; default 1). The whole effect against your clip. At 1 the output is opaque; at 0 it's
your clip, untouched (Arena's own snapshots matched the bare clip bit for bit).

### What works where

| control | globes | places | Generated | Clip (Over) |
| --- | --- | --- | --- | --- |
| View | yes | nothing | yes | nothing |
| Longitude, Latitude | the middle of the view | pan the tile | the middle of the view | nothing |
| Zoom, Rotation | yes | yes | yes | nothing |
| Spin | turns or scrolls | rotates | turns or scrolls | nothing |
| Sea Fill, Depths | Earth only | places with a sea | Continents only | nothing |
| the Generated group | nothing | nothing | yes (Sea on Continents only) | nothing |

The Clip world is your clip pixel for pixel, so nothing in the World group moves it. Everything in
the Contours, Sweep and Look groups works everywhere.

### About

The last group, **About**, has the plugins' name, version, licence and maker, and buttons that open
this user guide, the project page, the source on GitHub and the support page in your browser.

---

## SW Isohypse Over

The Over has the source's controls, plus the Clip group, and starts out differently: World *Clip*,
Spin 0, Lines 24 and Smooth 0.45.

**The clip as the map.** With World on *Clip*, your clip's brightness (as the eye reads it, from
its colour values) is the height, at the clip's own size, and the contours, the sweep, the tints and
the rest are drawn over it. Black is the bottom of the range and white the top, whatever the clip
holds, so a dark clip only reaches the lower contours; turn Lines up for more of them. Flat areas
draw nothing, so a picture on black keeps its black. Smooth matters more here than anywhere: video
noise makes ragged little rings, and Smooth irons them out before anything is contoured.

**A world over the clip.** Any other World draws that world over your clip, with the clip showing
through between the lines as Clip Show says. A Map or a place covers the frame in lines; a globe
brings its disc, which hides the clip behind it and leaves it showing round the edge.

![The Over on Resolume demo clips: a cluster of blocks turned into orange-ringed contour lines on black; a kaleidoscopic star of light turned into glowing orange and white contours; a map of Mars in contours over the cluster of blocks; the Moon as a contoured globe over the kaleidoscope, hiding its middle](guide-over.png)

*SW Isohypse Over on Resolume's own demo clips, two seconds in, rendered by `ihtest` rather than
captured from Resolume. Top left, OrganicMotions_06 at the defaults but Clip Show 0. Top right,
IntoTheGlow_02 at the defaults. Bottom left, OrganicMotions_06 with World Mars and View Map. Bottom
right, IntoTheGlow_02 with World Moon. Each panel is a whole 1280 × 720 frame at reduced size.*

### Your own heightmap

Any greyscale heightmap works, white high: a fan map, a campaign map, a DEM you've exported
yourself, a photograph if you like what it does. Put it in a clip slot as a still, put SW Isohypse
Over on it, and leave World on *Clip*. Clip Show 0 hides the picture itself, and Floor up keeps the
whole map drawn between sweeps.

A picture arriving at 8 bits a channel has 256 heights in it. In the harness, a heightmap at Lines
around 150 with Smooth at 0 showed those steps as ragged lines on its gentle slopes; Smooth at the
Over's default hides them. Leave a little black round the edge and the map's border comes out as a
cliff, a stack of contours, which may or may not be what you wanted.

### Text and logos

A white logo or word on black becomes a plateau with a cliff round it. At the Over's default Smooth
the cliff is a tight outline; at 0.75 to 1 it spreads into rings round the letters, and with Clip Show
0 and Style Neon it's a neon sign that the sweep runs up and down.

---

## Recipes

**The pen plotter's finished sheet.** The Moon, Style *Plotter*, Sweep *Off*, Glow 0, Wobble 0.5:
every contour in ink on cream, the way Lague's plotter leaves it. For the drawing itself, use
*A map drawn from the bottom up* in Plotter on a single hill, Mount Fuji or Kilimanjaro; on a whole
globe the early lines have faded by the time the sweep reaches the top.

**The sea comes in.** Earth as a Map, Style *Survey*, Sweep *Manual*, Flood 1, Sweep Level 0.6194.
Automate Sweep Level up over a minute or two. Zoom 0.264 (3×) on Europe (Longitude 0.528, Latitude
0.778) gives the figure under *Flood and Flow*. For the full horror, carry on past 0.68.

**A sweep on the bar.** Sweep Bars *4 Bars* with Resolume's BPM: a pass every four bars, starting on
a downbeat, and every layer set to 4 Bars agreeing. Bounce on *2 Bars* goes up for two bars and down
for two.

**Two layers in step without the tempo.** A Free sweep counts from when its instance started, so
two layers each have their own. Map **Restart** on both to the same key or note and press it once.

**A still map that moves.** Sweep *Off* and Flow 0.6 or so. The contours creep uphill while the
world stays put. 0.4 sends them down.

**Neon Mars.** World *Mars*, Style *Neon*, Hue 0.55, Longitude 0.278 (−80°, Tharsis and the Valles
Marineris), Spin 0.56 for a slow turn.

**The atlas.** Earth, View *Map*, Style *Survey*, Tints 1, Shade 0.6. The Grand Canyon at the top of
this guide is the same look on a place, at Zoom 0.03.

**Your footage as terrain.** SW Isohypse Over on the clip, at its defaults. Clip Show 0 for the lines
alone; Floor 1 and Sweep *Off* if you want every contour drawn all the time. On a busy, bright clip,
turn Glow down: the glow piles up fast.

**A logo in neon contours.** SW Isohypse Over on a white logo on black, Smooth 0.75, Clip Show 0,
Style *Neon*. Sweep *Bounce* runs the light up and down it.

**An alien planet.** World *Generated*, Terrain *Continents*, View *Globe*, Morph 0.25 for a world
that changes over a minute or so. Step Seed for another.

---

## Time and tempo

This is what the code does and the harness exercises; Arena has only been seen to follow it as far
as the *Before you rely on this* box says.

The sweep, Spin, Flow and Morph move on by the host's time since the last frame. A jump of more than
a second, or any step backwards (a scrub, or a retrigger that sends the clip's time back to the
start), passes no time, so they carry on from where they were without lurching. To start the sweep
from the bottom, press Restart. While it works out whether the host counts in seconds or
milliseconds (Resolume has been seen sending both), the plugin runs on its own clock for a few
frames.

**Sweep Bars** counts the host's bars from its bar phase coming round, so a pass starts on a downbeat
and two layers on the same setting stay together. Pause Resolume's tempo and a bar-locked sweep stops
dead while a Free one carries on, as it did in Arena. A host that sends no tempo at all gets 120 BPM
in 4/4 on the plugin's own clock, so *1 Bar* is a two-second pass. The plugin keeps its own count
running underneath, so if the tempo goes away mid-show the sweep carries on without a jump.

---

## How it works

1. **The heights.** A real world is a 16-bit greyscale map, decoded off the GL thread and uploaded
   with its mipmaps. Generated draws its world into a 2048 × 1024 map on the GPU, redrawn only when
   Terrain, Seed, Feature Size or Roughness changes, or Morph moves it. The Over's Clip turns the
   clip's brightness into a heightfield at the clip's size every frame.
2. **The map.** For each pixel: where it lands on the world (a map, a globe, a tile or the clip
   itself), the height there through a smooth cubic curve (so a contour stays a smooth line however
   far you zoom), how many intervals up that is, and how far that is from the nearest contour in
   pixels, through the slope. Then the sea, the tints, the flood, the shading, the contours, the
   coast and the sweep, each line as bright as the sweep's last crossing says.
3. **The glow**, at a quarter of the frame's size, blurred.
4. **The picture**: the map over the style's ground (or your clip, in the Over), the glow added.

---

## Performance

Measured by the offline harness (`ihtest --bench`) on an Apple M4 Max, milliseconds a frame with the
GPU finished:

| plugin | what | 1080p | 4K |
| --- | --- | --- | --- |
| SW Isohypse | the Moon, defaults | 0.44 | 1.35 |
| SW Isohypse | Earth, map, Glow 1 | 0.46 | 1.76 |
| SW Isohypse | Generated, morphing | 1.87 | 2.91 |
| SW Isohypse | Generated craters, morphing | 3.31 | 4.28 |
| SW Isohypse Over | the clip as the map | 0.49 | 1.88 |

These are the README's figures. My own runs for this guide came within 0.15 ms of every row but the
first, whose 1080p figure came out at 0.61 and 0.88 ms, so call it under a millisecond. The Moon and
Earth cost about the same. Generated costs more while Morph moves it, because the whole
2048 × 1024 world is redrawn every frame, and Craters most of all; with Morph at 0 it's drawn once.
Nothing was timed inside Resolume, and nothing was timed on Windows.

---

## If it looks wrong

**Nothing appears at all.** That's what a shader that won't compile looks like, or a plugin that
can't find its maps (a bundle copied without its `Resources`). The log says which:

```
macOS    ~/Library/Logs/isohypse/isohypse.YYYY-MM-DD.log
Windows  %LOCALAPPDATA%\isohypse\logs\isohypse.YYYY-MM-DD.log
```

It records the GL vendor, renderer and version at load, where it's reading the maps from, each
world as it loads, a shader that failed and a map it couldn't read.

**It isn't in the browser.** Check the folder under *Installing*, and that Resolume was restarted.

**The new world takes a moment to appear.** It's decoding; the old one stays up meanwhile. Earth is
the slowest.

**On Earth nothing much happens for ages.** The sweep is under the sea for 62% of every pass.

**The sweep doesn't move.** Sweep *Manual* doesn't, and Sweep Bars stops when Resolume's tempo is
paused. With Sweep *Off* there's no sweep at all.

**Sweep Time does nothing.** Sweep Bars is set to something other than *Free*.

**The lines are faint, especially in Plotter.** Floor is 0.2 by default, so the lines the sweep
hasn't reached are dim. Turn Floor up.

**The top of the world lights up as the sweep starts.** That's last pass's trail; Rise and Fall go
round. See *A map drawn from the bottom up*.

**A place has soft edges or dark corners.** Zoom 0.03 for a straight tile, 0.044 for a turning one.

**View, Longitude or Zoom does nothing.** View does nothing on a place, and nothing in the World group
moves the Over's *Clip*, which is your clip pixel for pixel. See *What works where*.

**Sea Fill or Depths does nothing.** The world has no sea: see the tables.

**Half of Pluto is missing.** New Horizons only saw half of it. Longitude 180° faces the half it saw;
on Charon it's Longitude 0°.

**The globe hides the layer under it**, or the clip under the Over. A globe brings its opaque disc.
Use a Map or a place, or a blend mode that ignores dark.

**The Over draws past the edges of a small clip.** A clip smaller than the composition reaches an
effect in Arena as a composition-sized frame with the clip inset in it, so the Over's world fills
the whole frame. That's Arena's doing.

**The source looks the wrong size after resizing the composition.** A source clip in Arena keeps the
resolution it was made at (its resize is *Original*). Also Arena's.

**Your clip barely makes contours.** It's dark or low in contrast, so it spans only a few intervals.
Turn Lines up, or lift the clip's levels before the Over.

---

## Known limits

- **Windows on software rendering only.** Resolume on Windows has run it on a GPU-less machine,
  so nothing says how fast it is on a real graphics card there.
- **Only ever run on one GPU**, an Apple M4 Max, plus Apple's software renderer, although the macOS
  build has an Intel slice.
- **The maps' own resolution.** Earth is about 10 km a pixel and the Moon about 5; a place is 512
  pixels across. The smooth curve through the heights keeps a contour smooth however far you zoom,
  so a globe zoomed far in goes soft and simple, with no steps and no detail the map doesn't have.
  Peaks and trenches are lower and shallower than their real heights, being averages.
- **The sweep covers the whole world's range.** Zoomed into a region, most of a pass happens out of
  view, and on Earth 62% of it is under the sea.
- **Half-seen worlds.** Pluto and Charon are only the side New Horizons saw, and Venus has gaps.
- **No Middle-earth**, for the reasons above, and no gas giants, for want of ground.
- **Big plugins.** Each carries every map, about 40 MB.
- **A globe's disc is opaque**, even at Background 0.
- **Survey's sea on most places.** A coastal place whose land survey carries no depths has a flat
  sea at 0 m, and Tints colours it as the deepest water on the tile: Naples' is navy, Sydney's a
  mid blue. Edinburgh, San Francisco, Rio and Hawaii have real depths, so their tints run from
  shallow to deep as they should. Shade lays a purple-grey wash over any sea, Earth's oceans
  included.
- **Wobble is subtle**: 4 pixels at most at 1080 lines.
- **No audio input.** Map Restart, Sweep Level and the rest to Resolume's audio analysis, BPM or cues.
- **No presets.** Resolume saves parameter presets itself. There is a
  [browser demo](https://isohypse-demo.stoatworks-labs.com/) to try it before installing: the
  plugin's own shaders and maps (the globes at half the resolution) with every control, but a
  browser, not Resolume, and it says what it leaves out.

---

## Links

- Project page: [stoatworks-labs.com/software/isohypse](https://stoatworks-labs.com/software/isohypse/)
- Source: [github.com/stoatworks-labs/isohypse](https://github.com/stoatworks-labs/isohypse)
- Browser demo: [isohypse-demo.stoatworks-labs.com](https://isohypse-demo.stoatworks-labs.com/)
- User guide: [stoatworks-labs.com/software/isohypse/guide](https://stoatworks-labs.com/software/isohypse/guide/)

## Reporting something

If something's wrong, shout at
[github.com/stoatworks-labs/isohypse/issues](https://github.com/stoatworks-labs/isohypse/issues).
A screenshot, the World (and Place), the Style and Sweep, any other controls you moved, whether it's
the source or the Over, and the composition's resolution and frame rate are usually enough. If the
plugin drew nothing, attach the log.

The maps are public-domain NASA, USGS and NOAA products and the Terrarium elevation tiles, some of
whose sources (the UK's Environment Agency, Copernicus, Canada, New Zealand and Australia among them)
are under CC BY and Open Government Licences and ask to be credited. Every credit is in
[ATTRIBUTIONS.md](https://github.com/stoatworks-labs/isohypse/blob/main/ATTRIBUTIONS.md) and
[SOURCES.md](https://github.com/stoatworks-labs/isohypse/blob/main/data/SOURCES.md), and both travel
with the plugin. The idea of the sweep is Sebastian Lague's, from his
[pen plotter](https://youtu.be/qyNWDzO6kKQ); the code and the look are new.
