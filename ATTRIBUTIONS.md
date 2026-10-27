# Attributions

Isohypse is built on other people's work. This file lists what that work is, who did
it, and what it is doing here.

It is generated — the master lists live in the `stoatworks-backend` repo and are
pushed out by `scripts/sync-attributions.py`. Edit it there, not here.

## Code we derived from other people's work

Someone else solved this first, and this project would not exist in its current form without their work.

### The harness, the tooling, two plugins from one core — Stoatworks slipstream

<https://github.com/stoatworks-labs/slipstream>  
Licence: MIT  
Copyright: Stoatworks Labs

CMakeLists.txt's two bundles from one OBJECT library, the plugin class's shape (a host order per plugin with the About block last, the clock that votes on seconds against milliseconds), the harness's rig, PNG writer, cue sheets, --pipe/--film, --negative, --state and --offline, and tools/verify.sh and tools/glslc.sh. All adapted.

### The GL state and the log — Stoatworks langton, millpond and radar

<https://github.com/stoatworks-labs/millpond>  
Licence: MIT  
Copyright: Stoatworks Labs

source/Diag.{h,cpp} and source/GLState.h (radar's, which is vectrix's, from resolume-scopes), by way of slipstream. source/Target.{h,cpp} is the fleet's PassBuffer, widened.

### Fast third-order texture filtering — C. Sigg and M. Hadwiger, GPU Gems 2, chapter 20 (2005)

The cubic B-spline in four bilinear taps that the map pass reads every height through (bspline in source/Shaders.cpp).

## Third-party code this project uses

Libraries, SDKs and frameworks the project is built on or bundles.

### Resolume FFGL SDK

<https://github.com/resolume/ffgl>  
Licence: BSD-3-Clause  
Copyright: FreeFrame

Vendored as a git submodule at external/ffgl (third_party/ffgl in oxbow).

The plugin ABI itself. An FFGL effect or source is defined by this SDK's headers — there is no other way to be loadable by Resolume Arena and Avenue.

### GLEW — the OpenGL Extension Wrangler Library

<https://github.com/nigels-com/glew>  
Licence: BSD-3-Clause (with Mesa 3-D and Khronos components)  
Copyright: Milan Ikits, Marcelo E. Magallon and Lev Povalahev

Arrives inside the FFGL submodule at external/ffgl/deps/glew-2.1.0. Not fetched separately.

Resolves OpenGL entry points on Windows, where the system headers stop at OpenGL 1.1.

### libpng

<http://www.libpng.org/pub/png/libpng.html>  
Licence: PNG Reference Library License (libpng)  
Copyright: the PNG Reference Library authors

Arrives inside the FFGL submodule, under the SDK's CustomThumbnail sample.

Part of the upstream SDK tree rather than something these plugins call directly — listed because it is present in the checkout.

### stb_image

<https://github.com/nothings/stb>  
Licence: MIT or Public Domain (dual, at your option)  
Copyright: Sean Barrett

Single-header decoder vendored under external/stb/ and compiled into one translation unit.

Decodes PNG, JPEG and GIF. A sprite-sheet player has to open whatever the operator exported.

## Data this project ships

Measurements made by other people and carried inside the product. Each is credited the way its publisher asks, and where a licence requires its attribution to travel with the data, that text is reproduced here.

### Earth: ETOPO 2022 Global Relief Model — NOAA National Centers for Environmental Information

<https://doi.org/10.25921/fd45-gt74>  
Licence: Public domain (U.S. Government work)

data/earth.png: the 60 arc-second ice-surface grid, sea floor included, area-averaged to 4096 × 2048. Cited as NOAA asks: NOAA National Centers for Environmental Information. 2022: ETOPO 2022 15 Arc-Second Global Relief Model. NOAA National Centers for Environmental Information. https://doi.org/10.25921/fd45-gt74

### Moon: LRO Lunar Orbiter Laser Altimeter, LDEM_16 — NASA/GSFC/MIT, the LOLA team

<https://imbrium.mit.edu/DATA/LOLA_GDR/CYLINDRICAL/IMG/LDEM_16.IMG>  
Licence: Public domain (NASA)

data/moon.png, area-averaged to 2048 × 1024 from the LOLA team's copy of the PDS volume. Smith, D. E., et al. (2010), The Lunar Orbiter Laser Altimeter investigation on the Lunar Reconnaissance Orbiter mission, Space Sci. Rev. 150, 209-241.

### Mars: MGS Mars Orbiter Laser Altimeter, the MOLA DEM mosaic — NASA/GSFC MOLA Science Team; USGS Astrogeology Science Center

<https://planetarymaps.usgs.gov/mosaic/Mars_MGS_MOLA_DEM_mosaic_global_463m.tif>  
Licence: CC0 / public domain

data/mars.png, area-averaged to 2048 × 1024. Smith, D. E., et al. (2001), Mars Orbiter Laser Altimeter: Experiment summary after the first year of global mapping of Mars, J. Geophys. Res. 106(E10), 23689-23722.

### Mercury: MESSENGER Global DEM 665 m v2 — NASA/JHUAPL/Carnegie Institution of Washington/USGS

<https://planetarymaps.usgs.gov/mosaic/Mercury_Messenger_USGS_DEM_Global_665m_v2.tif>  
Licence: Public domain (USGS/NASA)

data/mercury.png, area-averaged to 2048 × 1024. Becker, K. J., et al. (2016), First global digital elevation model of Mercury, 47th LPSC, abstract 2959.

### Venus: Magellan Global Topography 4641 m — NASA/JPL Magellan Team; P. Ford and G. Pettengill (MIT); USGS Astrogeology

<https://planetarymaps.usgs.gov/mosaic/Venus_Magellan_Topography_Global_4641m_v02.tif>  
Licence: Public domain (USGS asks that the authors be cited)

data/venus.png, area-averaged to 2048 × 1024, thin altimetry gaps filled from their edges. Ford, P. G., & Pettengill, G. H. (1992), Venus topography and kilometer-scale slopes, J. Geophys. Res. 97(E8), 13103-13114.

### Ceres and Vesta: Dawn Framing Camera HAMO global DTMs — NASA/JPL-Caltech/UCLA/MPS/DLR/IDA, via USGS Astrogeology

<https://planetarymaps.usgs.gov/mosaic/Ceres_Dawn_FC_HAMO_DTM_DLR_Global_60ppd_Oct2016.tif>  
Licence: No access or use constraints (NASA PDS); the authors ask to be cited

data/ceres.png and data/vesta.png, heights above each body's reference ellipsoid, area-averaged to 2048 × 1024. Vesta's DTM is https://planetarymaps.usgs.gov/mosaic/Vesta_Dawn_HAMO_DTM_DLR_Global_48ppd.tif. Preusker, F., et al. (2016), Dawn at Ceres -- shape model and rotational state, 47th LPSC, abstract 1954; Preusker, F., et al. (2014), Global shape of (4) Vesta from Dawn FC stereo images, Vesta in the Light of Dawn, LPI Contrib. 1773.

### Pluto and Charon: New Horizons LORRI-MVIC global DEMs, 300 m — NASA/JHUAPL/SwRI/LPI, the New Horizons team, via USGS Astrogeology

<https://planetarymaps.usgs.gov/mosaic/Pluto_NewHorizons_Global_DEM_300m_Jul2017_16bit.tif>  
Licence: No access constraints (NASA); the authors ask to be cited

data/pluto.png and data/charon.png, the hemisphere New Horizons saw, area-averaged to 2048 × 1024; the rest is left as no data. Charon's DEM is https://planetarymaps.usgs.gov/mosaic/Charon_NewHorizons_Global_DEM_300m_Jul2017_16bit.tif. Schenk, P. M., et al. (2018), Basins, fractures and volcanoes: Global cartography and topography of Pluto from New Horizons, Icarus 314, 400-433; Schenk, P. M., et al. (2018), Breaking up is hard to do: Global cartography and topography of Pluto's mid-sized icy moon Charon from New Horizons, Icarus 315, 124-145.

### Places: Terrain Tiles on AWS Open Data — Mapzen / Tilezen (joerd), and the sources below

<https://github.com/tilezen/joerd/blob/master/docs/attribution.md>  
Licence: Mixed, by source: public domain, CC BY, Open Government Licence and others, each requiring the attribution below

The twenty-two places' tiles in data/ (london.png to wellington.png), from the Terrarium-encoded tiles at s3.amazonaws.com/elevation-tiles-prod, cleaned of resampling artefacts and box-averaged to 512 × 512. The sources' licences require this attribution, copied verbatim from joerd's docs/attribution.md:

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

## Inspirations

What this set out to be. No code, assets or binaries from any of these were used or examined — the debt is to the idea.

### I Made a Pen Plotter to Draw Maps of the Moon — Sebastian Lague (2026)

<https://youtu.be/qyNWDzO6kKQ>

The idea: a heightmap of the Moon, Mars or Earth drawn as contours, level by level, from the lowest ground up. No code, data or imagery of his is used; his contours are marching squares on the CPU for a pen, these are a distance field on the GPU for a screen.

## Getting this wrong

If your work is here and the description is inaccurate, the licence is wrong, or you would rather not be listed — open an issue and it will be fixed.
