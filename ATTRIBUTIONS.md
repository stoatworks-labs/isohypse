# Attributions

Isohypse is built on other people's work, and its maps are other people's measurements.
This file lists what that work is, who did it, and what it is doing here.

**Provisional hand copy (2026-10-27).** The fleet's attributions files are generated: the
master lists live in the `stoatworks-backend` repo and are pushed out by
`scripts/sync-attributions.py`. Isohypse isn't registered there yet, so this one was
written by hand in the same shape. Once it is registered, the generated file replaces
this one and the entries below move into the backend's lists — the data credits with
them, since the Terrarium tiles' licences require their block to travel with the plugin.

## Code derived from other people's work

Someone else solved this first, and this project would not exist in its current form without their work.

### The harness, the tooling, two plugins from one core — Stoatworks slipstream

<https://github.com/stoatworks-labs/slipstream>  
Licence: MIT  
Copyright: Stoatworks Labs

`CMakeLists.txt`'s two bundles from one OBJECT library, the plugin class's shape (a host
order per plugin with the About block last, the clock that votes on seconds against
milliseconds), the harness's rig, PNG writer, cue sheets, `--pipe`/`--film`,
`--negative`, `--state`, `--offline`; `tools/verify.sh` and `tools/glslc.sh`. All adapted.

### The GL state and the log — Stoatworks langton, millpond and radar

<https://github.com/stoatworks-labs/millpond>  
Licence: MIT  
Copyright: Stoatworks Labs

`source/Diag.{h,cpp}` and `source/GLState.h` (radar's, which is vectrix's, from
`resolume-scopes`), by way of slipstream. `source/Target.{h,cpp}` is the fleet's
PassBuffer, widened.

## Third-party code this project uses

Libraries, SDKs and frameworks the project is built on or bundles.

### Resolume FFGL SDK

<https://github.com/resolume/ffgl>  
Licence: BSD-3-Clause  
Copyright: FreeFrame

Vendored as a git submodule at external/ffgl.

The plugin ABI itself. An FFGL effect or source is defined by this SDK's headers — there is no other way to be loadable by Resolume Arena and Avenue.

### stb_image

<https://github.com/nothings/stb>  
Licence: Public domain (Unlicense) or MIT, at the user's choice  
Copyright: Sean Barrett

Single header vendored at external/stb/stb_image.h, compiled static and PNG-only.

Decodes the worlds' 16-bit greyscale PNGs, on macOS and Windows alike.

### GLEW — the OpenGL Extension Wrangler Library

<https://github.com/nigels-com/glew>  
Licence: BSD-3-Clause (with Mesa 3-D and Khronos components)  
Copyright: Milan Ikits, Marcelo E. Magallon and Lev Povalahev

Taken from vcpkg on Windows (`vcpkg.json`); macOS uses the system OpenGL framework.

### libpng

<http://www.libpng.org/pub/png/libpng.html>  
Licence: PNG Reference Library License (libpng)  
Copyright: the PNG Reference Library authors

Arrives inside the FFGL submodule, under the SDK's CustomThumbnail sample. Part of the
upstream SDK tree rather than something these plugins call — listed because it is
present in the checkout.

## The maps

Every map in `data/` is a downsampled copy of a published elevation model; `data/SOURCES.md`
has each product's URL, checksum and processing.

- **Earth** — NOAA National Centers for Environmental Information. 2022: ETOPO 2022 15
  Arc-Second Global Relief Model. NOAA National Centers for Environmental Information.
  <https://doi.org/10.25921/fd45-gt74> (the 60 arc-second surface grid). Public domain.
- **Moon** — LRO Lunar Orbiter Laser Altimeter, LDEM_16 (NASA / GSFC / MIT), from the LOLA
  team's copy of the PDS volume. Public domain.
- **Mars** — MGS Mars Orbiter Laser Altimeter, the USGS MOLA DEM mosaic (NASA / GSFC /
  USGS Astrogeology). Public domain.
- **Mercury** — MESSENGER global DEM, USGS Astrogeology. Public domain.
- **Venus** — Magellan global topography, USGS Astrogeology. Public domain.
- **Ceres** and **Vesta** — Dawn Framing Camera HAMO DTMs (NASA / JPL-Caltech / UCLA /
  MPS / DLR / IDA, via USGS Astrogeology and NASA PDS). No use constraints; the authors ask
  to be cited.
- **Pluto** and **Charon** — New Horizons global DEMs (NASA / JHUAPL / SwRI / LPI, via USGS
  Astrogeology). No use constraints; the authors ask to be cited.
- **Places** — Terrain Tiles on AWS Open Data (Mapzen / Tilezen), whose sources require
  this attribution:

  * ArcticDEM terrain data DEM(s) were created from DigitalGlobe, Inc., imagery and
    funded under National Science Foundation awards 1043681, 1559691, and 1542736;
  * Australia terrain data © Commonwealth of Australia (Geoscience Australia) 2017;
  * Austria terrain data © offene Daten Österreichs – Digitales Geländemodell (DGM)
    Österreich;
  * Canada terrain data contains information licensed under the Open Government
    Licence – Canada;
  * Europe terrain data produced using Copernicus data and information funded by the
    European Union - EU-DEM layers;
  * Global ETOPO1 terrain data U.S. National Oceanic and Atmospheric Administration
  * Mexico terrain data source: INEGI, Continental relief, 2016;
  * New Zealand terrain data Copyright 2011 Crown copyright (c) Land Information New
    Zealand and the New Zealand Government (All rights reserved);
  * Norway terrain data © Kartverket;
  * United Kingdom terrain data © Environment Agency copyright and/or database right
    2015. All rights reserved;
  * United States 3DEP (formerly NED) and global GMTED2010 and SRTM terrain data
    courtesy of the U.S. Geological Survey.

## Work checked against

No code was taken from these — but they were where the idea or the method came from.

### The contour sweeps — Sebastian Lague, *I Made a Pen Plotter to Draw Maps of the Moon* (2026)

<https://youtu.be/qyNWDzO6kKQ>

The idea: a heightmap of the Moon, Mars or Earth drawn as contours, level by level. No
code, data or imagery of his is used; his contours are marching squares on the CPU for a
pen, these are a distance field on the GPU for a screen.

### Fast third-order texture filtering — C. Sigg and M. Hadwiger, *GPU Gems 2*, ch. 20 (2005)

The cubic B-spline in four bilinear taps that the map pass reads every height through
(`bspline` in `source/Shaders.cpp`).
