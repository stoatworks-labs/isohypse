/**
 * Isohypse — browser demo.
 *
 * Contour lines of real worlds, swept through by a line climbing from the
 * lowest ground to the highest. The one idea, from `AGENTS.md`: a contour is
 * where the height is a whole number of intervals, and a pixel is on one when
 * its LEVEL is within half a line of a whole number, half a line measured in
 * PIXELS (`|level − k| / |∇level|`), so a contour is the same width on a cliff
 * and on a plain. The SWEEP is one more contour whose level moves; every other
 * contour is as bright as how recently the sweep crossed it. SW Isohypse (IH01)
 * draws a world on black, paper or nothing; SW Isohypse Over (IH02) draws it
 * over the clip, or reads the clip itself as the map.
 *
 * Two halves, and they are not equally faithful here:
 *
 *   **The GPU half is the plugin's own GLSL.** `shaders.js` is the nine
 *   `R"( ... )"` pieces of `source/Shaders.cpp` plus kVersion, spliced by
 *   `demo/tools/check_shaders.py --write` and never typed; the same script
 *   compares them character for character, and checks they are joined as
 *   `Assemble()` and InitGL join them, and `tools/verify.sh` runs it. They run
 *   as the plugin's passes -- generate (Generated only, into an RG16 map),
 *   intake (the Over's Clip, into an RG16 map), map (MRT into two RGBA16F
 *   targets: the picture and the light), glow (a quarter raster, down and then
 *   a Gaussian twice) and the composite into the canvas -- on the same
 *   formats where WebGL2 has them (RG16 needs EXT_texture_norm16; see
 *   `heightFormat`).
 *
 *   **The CPU half is a port.** Between the PORT markers below: Controls.cpp
 *   (every mapping, the names, groups and order), SweepClock.cpp, the parts of
 *   Worlds.cpp that are not file I/O (the dropdowns, the gap filler, the
 *   decode into RG16 texels) and the colour and orientation helpers at the top
 *   of Isohypse.cpp. Between the FRAME markers: the CPU half of
 *   ProcessOpenGL, which turns the controls and the clock into each pass's
 *   uniforms. Nothing checks these against the C++ automatically;
 *   `demo/tools/crosscheck/` renders the plugin's own harness and this page
 *   at the same settings and compares the pictures (see AGENTS.md).
 *
 * What the page cannot do as the plugin does, all said on the page: read its
 * maps from inside a bundle (they are fetched, the globes at half the
 * resolution -- `demo/tools/make_worlds.py`), decode the first one before the
 * first frame, or follow a host's tempo (Sweep Bars runs on the plugin's own
 * 120 BPM fallback, which is what it does in a host that sends none).
 */

import { Program, bindTexture, GLError } from './vendor/gl.js';
import { mountDemo } from './vendor/demo.js';
import * as S from './shaders.js';
import { GLOBES, PLACES } from './worlds/table.js';

const f32 = Math.fround;
const clampTo = (v, lo, hi) => (v < lo ? lo : v > hi ? hi : v);

// ==== BEGIN PORT ===========================================================
//
// Controls.h / Controls.cpp. Every host parameter is 0..1 (an option holds its
// element's index; Index Every and Seed are real integers), mapped here into
// the map's units. A LEVEL is one contour interval; a PASS is one sweep from
// the bottom of the world's range to the top.

const PT = {
  WORLD: 0, PLACE: 1, VIEW: 2, LONGITUDE: 3, LATITUDE: 4, ZOOM: 5, SPIN: 6, ROTATION: 7,
  LINES: 8, INDEX_EVERY: 9, LINE_WIDTH: 10, SMOOTH: 11, SEA_FILL: 12, DEPTHS: 13,
  SWEEP: 14, SWEEP_TIME: 15, SWEEP_BARS: 16, SWEEP_LEVEL: 17, SWEEP_GLOW: 18, TRAIL: 19, FLOOR: 20, FLOOD: 21,
  FLOW: 22, RESTART: 23,
  STYLE: 24, HUE: 25, HEIGHT_COLOUR: 26, GLOW: 27, SHADE: 28, TINTS: 29, WOBBLE: 30, BACKGROUND: 31,
  TERRAIN: 32, SEED: 33, FEATURE_SIZE: 34, ROUGHNESS: 35, GENERATED_SEA: 36, MORPH: 37,
  CLIP_SHOW: 38, INVERT: 39, MIX: 40,
  ABOUT_TEXT: 41,
};
/** The controls the page carries: everything before the About block. */
const kParamSlots = PT.ABOUT_TEXT;

/** HostOrder(): the ids a plugin declares, in the order the host sees them
 * (the About block, last in both, is not on this page). */
function HostOrder(effect) {
  const order = [];
  for (let id = PT.WORLD; id <= PT.MORPH; id += 1) {
    if (!(effect && id === PT.BACKGROUND)) order.push(id);
  }
  if (effect) order.push(PT.CLIP_SHOW, PT.INVERT, PT.MIX);
  return order;
}

function GroupOf(id) {
  if (id <= PT.ROTATION) return 'World';
  if (id <= PT.DEPTHS) return 'Contours';
  if (id <= PT.RESTART) return 'Sweep';
  if (id <= PT.BACKGROUND) return 'Look';
  if (id <= PT.MORPH) return 'Generated';
  if (id <= PT.MIX) return 'Clip';
  return 'About';
}

const NAMES = [
  'World', 'Place', 'View', 'Longitude', 'Latitude', 'Zoom', 'Spin', 'Rotation',
  'Lines', 'Index Every', 'Line Width', 'Smooth', 'Sea Fill', 'Depths',
  'Sweep', 'Sweep Time', 'Sweep Bars', 'Sweep Level', 'Sweep Glow', 'Trail', 'Floor', 'Flood', 'Flow', 'Restart',
  'Style', 'Hue', 'Height Colour', 'Glow', 'Shade', 'Tints', 'Wobble', 'Background',
  'Terrain', 'Seed', 'Feature Size', 'Roughness', 'Sea', 'Morph',
  'Clip Show', 'Invert', 'Mix',
];
const NameOf = (id) => NAMES[id] ?? '?';

const View = { Map: 0, Globe: 1, Count: 2 };
const VIEW_NAMES = ['Map', 'Globe'];
const Sweep = { Off: 0, Rise: 1, Fall: 2, Bounce: 3, Manual: 4, Count: 5 };
const SWEEP_NAMES = ['Off', 'Rise', 'Fall', 'Bounce', 'Manual'];
const kBarChoices = 8;
const BAR_NAMES = ['Free', '1 Bar', '2 Bars', '4 Bars', '8 Bars', '16 Bars', '32 Bars', '64 Bars'];
const BarsOf = (index) => (index <= 0 || index >= kBarChoices ? 0 : 1 << (index - 1));
const Style = { Ink: 0, Plotter: 1, Neon: 2, Survey: 3, Count: 4 };
const STYLE_NAMES = ['Ink', 'Plotter', 'Neon', 'Survey'];
const Terrain = { Continents: 0, Ridges: 1, Craters: 2, Count: 3 };
const TERRAIN_NAMES = ['Continents', 'Ridges', 'Craters'];

/** std::lround on the non-negative values a host sends: half away from zero. */
const lround = (v) => (v < 0 ? -Math.round(-v) : Math.round(v));
const OptionIndex = (value, count) => clampTo(lround(value), 0, count - 1);

const clamp01 = (v) => clampTo(v, 0, 1);
const geometric = (v, low, high) => low * Math.pow(high / low, clamp01(v));
function inverseGeometric(value, low, high) {
  const lo = Math.min(low, high);
  const hi = Math.max(low, high);
  return f32(Math.log(clampTo(value, lo, hi) / low) / Math.log(high / low));
}
function bipolarSquared(v) {
  const x = 2.0 * clamp01(v) - 1.0;
  return x * Math.abs(x);
}
function inverseBipolarSquared(y) {
  const c = clampTo(y, -1.0, 1.0);
  const x = c < 0.0 ? -Math.sqrt(-c) : Math.sqrt(c);
  return f32(0.5 * (x + 1.0));
}
const kZoomLow = 1.0, kZoomHigh = 64.0;
const kSpinMax = 60.0;
const kLinesLow = 4.0, kLinesHigh = 300.0;
const kWidthLow = 0.4, kWidthHigh = 10.0;
const kSweepLow = 0.5, kSweepHigh = 120.0;
const kFlowMax = 4.0;
const kFrequencySmall = 6.0, kFrequencyBig = 0.6;
const kMaxIndexEvery = 10;
const kMaxSeed = 999;

const LongitudeFromParam = (v) => 360.0 * clamp01(v) - 180.0;
const ParamFromLongitude = (d) => f32(clampTo((d + 180.0) / 360.0, 0.0, 1.0));
const LatitudeFromParam = (v) => 180.0 * clamp01(v) - 90.0;
const ParamFromLatitude = (d) => f32(clampTo((d + 90.0) / 180.0, 0.0, 1.0));
const ZoomFromParam = (v) => geometric(v, kZoomLow, kZoomHigh);
const ParamFromZoom = (z) => inverseGeometric(z, kZoomLow, kZoomHigh);
const SpinFromParam = (v) => kSpinMax * bipolarSquared(v);
const ParamFromSpin = (d) => inverseBipolarSquared(d / kSpinMax);
const RotationFromParam = (v) => 360.0 * clamp01(v) - 180.0;
const LinesFromParam = (v) => geometric(v, kLinesLow, kLinesHigh);
const ParamFromLines = (n) => inverseGeometric(n, kLinesLow, kLinesHigh);
const LineWidthFromParam = (v) => geometric(v, kWidthLow, kWidthHigh);
const ParamFromLineWidth = (px) => inverseGeometric(px, kWidthLow, kWidthHigh);
const SmoothFromParam = (v) => 6.0 * clamp01(v);
const SweepSecondsFromParam = (v) => geometric(v, kSweepLow, kSweepHigh);
const ParamFromSweepSeconds = (s) => inverseGeometric(s, kSweepLow, kSweepHigh);
const TrailFromParam = (v) => clamp01(v);
const FlowFromParam = (v) => kFlowMax * bipolarSquared(v);
const ParamFromFlow = (l) => inverseBipolarSquared(l / kFlowMax);
const FeatureFrequencyFromParam = (v) => geometric(v, kFrequencySmall, kFrequencyBig);
const MorphFromParam = (v) => 0.2 * clamp01(v) * clamp01(v);

//---------------------------------------------------------------------------
// SweepClock.h / SweepClock.cpp. Where the sweep is, in passes, in double.
//---------------------------------------------------------------------------
const fract = (x) => x - Math.floor(x);

class SweepClock {
  constructor() {
    this.passes = 0.0;
    this.ownPasses = 0.0;
    this.barOrigin = 0.0;
    this.barLocked = false;
    this.position = 0.0;
    this.direction = 1;
    this.currentMode = Sweep.Rise;
    this.manualLevel = 0.5;
  }

  Advance(dt, input) {
    this.currentMode = input.mode;
    this.manualLevel = clampTo(input.level, 0.0, 1.0);

    // The plugin's own count always runs, so a host that stops sending its
    // tempo halfway through a show hands over without a jump.
    const bars = Math.max(input.bars, 0);
    if (bars === 0) this.ownPasses += dt / Math.max(input.seconds, 1.0e-3);
    else {
      const barSeconds = (4.0 * 60.0) / Math.max(input.fallbackBpm, 1.0);
      this.ownPasses += dt / (bars * barSeconds);
    }

    if (bars > 0 && input.haveBeat) {
      if (input.restart) this.barOrigin = Math.floor(input.barCount);
      this.barLocked = true;
      this.passes = (input.barCount - this.barOrigin) / bars;
      this.ownPasses = this.passes;
    } else {
      this.barLocked = false;
      if (input.restart) this.ownPasses = 0.0;
      this.passes = this.ownPasses;
    }
    this.fold(input.mode, this.manualLevel);
  }

  fold(mode, level) {
    switch (mode) {
      case Sweep.Rise:
        this.position = fract(this.passes);
        this.direction = 1;
        break;
      case Sweep.Fall:
        this.position = 1.0 - fract(this.passes);
        this.direction = -1;
        break;
      case Sweep.Bounce: {
        const q = 2.0 * fract(0.5 * this.passes);
        this.position = q < 1.0 ? q : 2.0 - q;
        this.direction = q < 1.0 ? 1 : -1;
        break;
      }
      case Sweep.Manual:
        this.position = level;
        this.direction = 0;
        break;
      default:
        this.position = 0.0;
        this.direction = 0;
        break;
    }
  }

  Position() { return this.position; }
  Direction() { return this.direction; }
}

//---------------------------------------------------------------------------
// Worlds.h / Worlds.cpp: the dropdowns, the gap filler and the texels. The
// table is GLOBES and PLACES from worlds/table.js, which make_worlds.py writes
// from source/WorldTable.inc.
//---------------------------------------------------------------------------
const Kind = { Globe: 0, Place: 1, Generated: 2, Clip: 3 };
const Choice = { Globe: 0, Place: 1, Generated: 2, Clip: 3 };
/** WorldInfo::kCoastMetres: the coast half a metre above a flat 0 m sea. */
const kCoastMetres = 0.5;
const SeaLevel = (info) => (info.maxMetres > info.minMetres
  ? (kCoastMetres - info.minMetres) / (info.maxMetres - info.minMetres) : 0.0);

const WorldChoiceCount = (effect) => GLOBES.length + 2 + (effect ? 1 : 0);

function ChoiceOf(index, effect) {
  const globes = GLOBES.length;
  index = clampTo(index, 0, WorldChoiceCount(effect) - 1);
  if (index < globes) return { choice: Choice.Globe, globe: index };
  if (index === globes) return { choice: Choice.Place, globe: 0 };
  if (index === globes + 1) return { choice: Choice.Generated, globe: 0 };
  return { choice: Choice.Clip, globe: 0 };
}

function WorldChoiceName(index, effect) {
  const { choice, globe } = ChoiceOf(index, effect);
  switch (choice) {
    case Choice.Globe: return GLOBES[globe].name;
    case Choice.Place: return 'Place';
    case Choice.Generated: return 'Generated';
    default: return 'Clip';
  }
}

function WorldIndexOf(choice, globe, effect) {
  const globes = GLOBES.length;
  switch (choice) {
    case Choice.Globe: return clampTo(globe, 0, Math.max(globes - 1, 0));
    case Choice.Place: return globes;
    case Choice.Generated: return globes + 1;
    default: return effect ? globes + 2 : globes + 1;
  }
}

const globeNamed = (name) => Math.max(0, GLOBES.findIndex((g) => g.name === name));

/**
 * FillGaps: each unmeasured texel takes the mean of the measured ones round
 * it, by pull-push. Float32Array storage, so every sum and mean is rounded to
 * a float as the C++'s floats are.
 */
function FillGaps(width, height, values, measured) {
  const levels = [];
  {
    const n = values.length;
    const base = { w: width, h: height, sum: new Float32Array(n), weight: new Float32Array(n) };
    for (let i = 0; i < n; i += 1) {
      base.weight[i] = measured[i] ? 1.0 : 0.0;
      base.sum[i] = measured[i] ? values[i] : 0.0;
    }
    levels.push(base);
  }
  while (levels[levels.length - 1].w > 1 || levels[levels.length - 1].h > 1) {
    const fine = levels[levels.length - 1];
    const cw = Math.max(1, (fine.w + 1) >> 1);
    const ch = Math.max(1, (fine.h + 1) >> 1);
    const coarse = { w: cw, h: ch, sum: new Float32Array(cw * ch), weight: new Float32Array(cw * ch) };
    for (let y = 0; y < fine.h; y += 1) {
      for (let x = 0; x < fine.w; x += 1) {
        const from = y * fine.w + x;
        const to = (y >> 1) * cw + (x >> 1);
        coarse.sum[to] += fine.sum[from];
        coarse.weight[to] += fine.weight[from];
      }
    }
    levels.push(coarse);
  }
  // Push: each level's unmeasured cells take their parent's mean.
  for (let k = levels.length - 2; k >= 0; k -= 1) {
    const fine = levels[k];
    const coarse = levels[k + 1];
    for (let y = 0; y < fine.h; y += 1) {
      for (let x = 0; x < fine.w; x += 1) {
        const i = y * fine.w + x;
        if (fine.weight[i] > 0.0) continue;
        const parent = (y >> 1) * coarse.w + (x >> 1);
        fine.sum[i] = coarse.weight[parent] > 0.0 ? f32(coarse.sum[parent] / coarse.weight[parent]) : 0.0;
        fine.weight[i] = 1.0;
      }
    }
  }
  const base = levels[0];
  for (let i = 0; i < values.length; i += 1) {
    if (!measured[i]) values[i] = base.weight[i] > 0.0 ? f32(base.sum[i] / base.weight[i]) : 0.0;
  }
}

/**
 * A 16-bit greyscale PNG, decoded HERE rather than by the browser: an <img>
 * or createImageBitmap hands back 8 bits a channel, which would turn every
 * map into 256 terraces. The plugin uses stb_image; this reads what
 * make-worlds.py writes (colour type 0, depth 16, no interlace) and refuses
 * anything else.
 */
async function decodePng16(buffer) {
  const bytes = new Uint8Array(buffer);
  const signature = [0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a];
  if (bytes.length < 8 || signature.some((b, i) => bytes[i] !== b)) throw new Error('not a PNG');
  const view = new DataView(buffer);
  let width = 0, height = 0, depth = 0, colour = 0, interlace = 0;
  const idat = [];
  let at = 8;
  while (at + 8 <= bytes.length) {
    const length = view.getUint32(at);
    const type = String.fromCharCode(bytes[at + 4], bytes[at + 5], bytes[at + 6], bytes[at + 7]);
    const data = bytes.subarray(at + 8, at + 8 + length);
    if (type === 'IHDR') {
      width = view.getUint32(at + 8);
      height = view.getUint32(at + 12);
      depth = bytes[at + 16];
      colour = bytes[at + 17];
      interlace = bytes[at + 20];
    } else if (type === 'IDAT') idat.push(data);
    else if (type === 'IEND') break;
    at += 12 + length;
  }
  if (depth !== 16 || colour !== 0 || interlace !== 0) {
    throw new Error(`a ${depth}-bit, colour type ${colour}${interlace ? ', interlaced' : ''} PNG; the maps are 16-bit grey`);
  }
  const stream = new Blob(idat).stream().pipeThrough(new DecompressionStream('deflate'));
  const raw = new Uint8Array(await new Response(stream).arrayBuffer());
  const stride = width * 2;
  if (raw.length < height * (stride + 1)) throw new Error('truncated image data');
  const out = new Uint8Array(height * stride);
  for (let y = 0; y < height; y += 1) {
    const filter = raw[y * (stride + 1)];
    const row = y * (stride + 1) + 1;
    const o = y * stride;
    const p = o - stride;
    for (let x = 0; x < stride; x += 1) {
      const a = x >= 2 ? out[o + x - 2] : 0;
      const b = y > 0 ? out[p + x] : 0;
      const c = x >= 2 && y > 0 ? out[p + x - 2] : 0;
      let v = raw[row + x];
      switch (filter) {
        case 0: break;
        case 1: v += a; break;
        case 2: v += b; break;
        case 3: v += (a + b) >> 1; break;
        case 4: {
          const pa = Math.abs(b - c), pb = Math.abs(a - c), pc = Math.abs(a + b - 2 * c);
          v += pa <= pb && pa <= pc ? a : pb <= pc ? b : c;
          break;
        }
        default: throw new Error(`unknown PNG filter ${filter}`);
      }
      out[o + x] = v & 255;
    }
  }
  const pixels = new Uint16Array(width * height);
  for (let i = 0; i < pixels.length; i += 1) pixels[i] = (out[2 * i] << 8) | out[2 * i + 1];
  return { width, height, pixels };
}

/**
 * LoadHeightfield: RG16 texels, row 0 the north edge. R the height between
 * the table's min and max; G 65535 where measured, 0 where not, the gaps
 * carrying the height of the ground round them. The file comes from ./worlds/
 * (fetched -- there is no bundle), and is the size the table says it is HERE.
 */
async function LoadHeightfield(world) {
  try {
    const response = await fetch(`./worlds/${world.file}`);
    if (!response.ok) return { error: `could not read ${world.file} (HTTP ${response.status})` };
    const { width: w, height: h, pixels } = await decodePng16(await response.arrayBuffer());
    if (w !== world.servedWidth || h !== world.servedHeight) {
      return { error: `${world.file} is ${w}x${h}, the table says ${world.servedWidth}x${world.servedHeight}` };
    }
    const count = w * h;
    const texels = new Uint16Array(count * 2);
    if (world.hasNodata) {
      // 0 is "never measured"; the data is 1..65535 for min..max.
      const values = new Float32Array(count);
      const measured = new Uint8Array(count);
      for (let i = 0; i < count; i += 1) {
        measured[i] = pixels[i] !== 0 ? 1 : 0;
        values[i] = measured[i] ? (pixels[i] - 1.0) / 65534.0 : 0.0;
      }
      FillGaps(w, h, values, measured);
      for (let i = 0; i < count; i += 1) {
        texels[i * 2] = lround(f32(clampTo(values[i], 0.0, 1.0) * 65535.0));
        texels[i * 2 + 1] = measured[i] ? 65535 : 0;
      }
    } else {
      for (let i = 0; i < count; i += 1) {
        texels[i * 2] = pixels[i];
        texels[i * 2 + 1] = 65535;
      }
    }
    return { width: w, height: h, texels, bytes: response.headers.get('content-length') };
  } catch (error) {
    return { error: `${world.file}: ${error.message}` };
  }
}

/** TopMipLevel (Target.cpp). */
function TopMipLevel(width, height) {
  let level = 0;
  for (let size = Math.max(width, height); size > 1; size = Math.floor(size / 2)) level += 1;
  return level;
}

//---------------------------------------------------------------------------
// Isohypse.cpp's helpers: colour (styles written in sRGB, handed to the GPU as
// linear light), the styles, the globe's orientation.
//---------------------------------------------------------------------------
const linearFromSrgbF = (c) => f32(c <= f32(0.04045) ? f32(c / f32(12.92)) : Math.pow(f32((c + f32(0.055)) / f32(1.055)), f32(2.4)));
const toLinear = ([r, g, b]) => [linearFromSrgbF(f32(r)), linearFromSrgbF(f32(g)), linearFromSrgbF(f32(b))];

/** Hue, saturation, value to sRGB, hue 0..1 round the wheel from red. */
function hsv(hue, saturation, value) {
  hue -= Math.floor(hue);
  const h = hue * 6.0;
  const i = Math.floor(h) % 6;
  const f = h - Math.floor(h);
  const p = value * (1.0 - saturation), q = value * (1.0 - saturation * f), t = value * (1.0 - saturation * (1.0 - f));
  let r, g, b;
  switch (i) {
    case 0: r = value; g = t; b = p; break;
    case 1: r = q; g = value; b = p; break;
    case 2: r = p; g = value; b = t; break;
    case 3: r = p; g = q; b = value; break;
    case 4: r = t; g = p; b = value; break;
    default: r = value; g = p; b = q; break;
  }
  return [f32(r), f32(g), f32(b)];
}

function paletteOf(style, hue) {
  switch (style) {
    case Style.Plotter:
      return { ground: [0.94, 0.92, 0.87], line: [0.10, 0.11, 0.17], index: [0.05, 0.05, 0.09], accent: hsv(hue, 0.8, 0.7),
        seaLine: [0.18, 0.32, 0.58], seaFill: [0.76, 0.84, 0.90], flood: [0.50, 0.66, 0.84], disc: [0.90, 0.88, 0.82], darkGround: false, glowBoost: 0.5 };
    case Style.Neon:
      return { ground: [0.0, 0.0, 0.01], line: hsv(hue, 0.85, 1.0), index: hsv(hue, 0.6, 1.0), accent: [1.0, 0.97, 0.92],
        seaLine: hsv(hue + 0.5, 0.8, 0.9), seaFill: [0.0, 0.02, 0.06], flood: hsv(hue, 0.9, 0.35), disc: [0.01, 0.01, 0.03], darkGround: true, glowBoost: 1.8 };
    case Style.Survey:
      return { ground: [0.97, 0.95, 0.89], line: [0.66, 0.42, 0.22], index: [0.46, 0.27, 0.12], accent: hsv(hue, 0.85, 0.8),
        seaLine: [0.30, 0.50, 0.78], seaFill: [0.74, 0.86, 0.95], flood: [0.45, 0.65, 0.88], disc: [0.93, 0.91, 0.84], darkGround: false, glowBoost: 0.5 };
    default:
      return { ground: [0.0, 0.0, 0.0], line: [0.90, 0.90, 0.88], index: [1.0, 1.0, 1.0], accent: hsv(hue, 0.85, 1.0),
        seaLine: [0.50, 0.66, 0.92], seaFill: [0.04, 0.08, 0.16], flood: hsv(hue, 0.7, 0.45), disc: [0.06, 0.06, 0.07], darkGround: true, glowBoost: 1.0 };
  }
}

/** World from view, row-major: rotY( lon ) * rotX( -lat ) * rotZ( roll ). */
function orientation(lonDeg, latDeg, rollDeg) {
  const a = (lonDeg * Math.PI) / 180.0, b = (-latDeg * Math.PI) / 180.0, c = (rollDeg * Math.PI) / 180.0;
  const Ry = [Math.cos(a), 0, Math.sin(a), 0, 1, 0, -Math.sin(a), 0, Math.cos(a)];
  const Rx = [1, 0, 0, 0, Math.cos(b), -Math.sin(b), 0, Math.sin(b), Math.cos(b)];
  const Rz = [Math.cos(c), -Math.sin(c), 0, Math.sin(c), Math.cos(c), 0, 0, 0, 1];
  const mul = (x, y) => {
    const r = new Array(9);
    for (let i = 0; i < 3; i += 1) {
      for (let j = 0; j < 3; j += 1) r[i * 3 + j] = x[i * 3] * y[j] + x[i * 3 + 1] * y[3 + j] + x[i * 3 + 2] * y[6 + j];
    }
    return r;
  };
  return new Float32Array(mul(mul(Ry, Rx), Rz));
}

const wrapDegrees = (d) => d - 360.0 * Math.floor((d + 180.0) / 360.0);

const kMaxFrameDelta = 1.0;
const kPlaceKeys = 10000;
const kGeneratedWidth = 2048, kGeneratedHeight = 1024;
const kFlowWrap = 2520.0;

/** The constructor's params[]: floats, as the plugin holds them. */
function constructorDefaults(isEffect) {
  const p = new Float32Array(kParamSlots);
  p[PT.WORLD] = isEffect ? WorldIndexOf(Choice.Clip, 0, true) : WorldIndexOf(Choice.Globe, globeNamed('Moon'), false);
  p[PT.PLACE] = 0.0;
  p[PT.VIEW] = View.Globe;
  p[PT.LONGITUDE] = ParamFromLongitude(0.0);
  p[PT.LATITUDE] = ParamFromLatitude(0.0);
  p[PT.ZOOM] = ParamFromZoom(1.0);
  p[PT.SPIN] = ParamFromSpin(isEffect ? 0.0 : 4.0);
  p[PT.ROTATION] = 0.5;
  p[PT.LINES] = ParamFromLines(isEffect ? 24.0 : 40.0);
  p[PT.INDEX_EVERY] = 5.0;
  p[PT.LINE_WIDTH] = ParamFromLineWidth(1.4);
  p[PT.SMOOTH] = isEffect ? 0.45 : 0.25;
  p[PT.SEA_FILL] = 0.5;
  p[PT.DEPTHS] = 0.5;
  p[PT.SWEEP] = Sweep.Rise;
  p[PT.SWEEP_TIME] = ParamFromSweepSeconds(8.0);
  p[PT.SWEEP_BARS] = 0.0;
  p[PT.SWEEP_LEVEL] = 0.5;
  p[PT.SWEEP_GLOW] = 0.6;
  p[PT.TRAIL] = 0.35;
  p[PT.FLOOR] = 0.2;
  p[PT.FLOOD] = 0.0;
  p[PT.FLOW] = ParamFromFlow(0.0);
  p[PT.RESTART] = 0.0;
  p[PT.STYLE] = Style.Ink;
  p[PT.HUE] = 0.07;
  p[PT.HEIGHT_COLOUR] = 0.0;
  p[PT.GLOW] = 0.35;
  p[PT.SHADE] = 0.0;
  p[PT.TINTS] = 0.0;
  p[PT.WOBBLE] = 0.0;
  p[PT.BACKGROUND] = 1.0;
  p[PT.TERRAIN] = Terrain.Continents;
  p[PT.SEED] = 7.0;
  p[PT.FEATURE_SIZE] = 0.5;
  p[PT.ROUGHNESS] = 0.5;
  p[PT.GENERATED_SEA] = 0.5;
  p[PT.MORPH] = 0.0;
  p[PT.CLIP_SHOW] = 0.6;
  p[PT.INVERT] = 0.0;
  p[PT.MIX] = 1.0;
  return p;
}

// ==== END PORT =============================================================

//===========================================================================
// The GL objects the plugin's Target.cpp makes, in WebGL2.
//===========================================================================

/**
 * The heightfield's format. The plugin's is RG16 (unsigned normalised 16-bit),
 * which WebGL2 has only with EXT_texture_norm16. Without it: RG32F, which
 * holds every 16-bit value exactly but needs OES_texture_float_linear to be
 * filtered and mipmapped; failing that RG16F, half float, whose 11 bits terrace
 * a gentle slope. The line under the picture says which is in use.
 */
function heightFormat(gl) {
  const norm16 = gl.getExtension('EXT_texture_norm16');
  if (norm16) return { internal: norm16.RG16_EXT, type: gl.UNSIGNED_SHORT, name: 'RG16', exact: true };
  if (gl.getExtension('OES_texture_float_linear')) return { internal: gl.RG32F, type: gl.FLOAT, name: 'RG32F', exact: true };
  return { internal: gl.RG16F, type: gl.FLOAT, name: 'RG16F', exact: false };
}

/** applySampling (Target.cpp). */
function applySampling(gl, linear, mipmapped, wrapX) {
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, mipmapped ? gl.LINEAR_MIPMAP_LINEAR : (linear ? gl.LINEAR : gl.NEAREST));
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, linear || mipmapped ? gl.LINEAR : gl.NEAREST);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, wrapX ? gl.REPEAT : gl.CLAMP_TO_EDGE);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
}

/** Target (Target.cpp): one or two colour attachments, reallocated only on a
 * real change of size or spec. */
class Target {
  constructor(gl) {
    this.gl = gl;
    this.fbo = null;
    this.textures = [];
    this.width = 0;
    this.height = 0;
    this.topLevel = 0;
    this.key = '';
  }

  ensure(width, height, spec) {
    const gl = this.gl;
    if (width <= 0 || height <= 0) return false;
    const key = `${spec.internal}/${spec.attachments}/${spec.linear}/${spec.mipmapped}/${spec.wrapX}`;
    if (this.fbo && this.width === width && this.height === height && this.key === key) return true;
    this.destroy();
    this.width = width;
    this.height = height;
    this.key = key;
    this.topLevel = spec.mipmapped ? TopMipLevel(width, height) : 0;
    const count = clampTo(spec.attachments, 1, 2);
    this.fbo = gl.createFramebuffer();
    gl.bindFramebuffer(gl.FRAMEBUFFER, this.fbo);
    const buffers = [gl.COLOR_ATTACHMENT0, gl.COLOR_ATTACHMENT1].slice(0, count);
    for (let i = 0; i < count; i += 1) {
      const t = gl.createTexture();
      gl.bindTexture(gl.TEXTURE_2D, t);
      gl.texStorage2D(gl.TEXTURE_2D, spec.mipmapped ? this.topLevel + 1 : 1, spec.internal, width, height);
      applySampling(gl, spec.linear, spec.mipmapped, spec.wrapX);
      gl.framebufferTexture2D(gl.FRAMEBUFFER, buffers[i], gl.TEXTURE_2D, t, 0);
      this.textures.push(t);
    }
    gl.drawBuffers(buffers);
    const complete = gl.checkFramebufferStatus(gl.FRAMEBUFFER) === gl.FRAMEBUFFER_COMPLETE;
    gl.bindTexture(gl.TEXTURE_2D, null);
    if (!complete) {
      this.destroy();
      throw new GLError(`a ${width}x${height} render target is incomplete in this browser`);
    }
    gl.viewport(0, 0, width, height);
    gl.clearColor(0, 0, 0, 0);
    gl.clear(gl.COLOR_BUFFER_BIT);
    return true;
  }

  bind() {
    this.gl.bindFramebuffer(this.gl.FRAMEBUFFER, this.fbo);
    this.gl.viewport(0, 0, this.width, this.height);
  }

  texture(i = 0) { return this.textures[i] ?? null; }

  generateMips() {
    const gl = this.gl;
    gl.bindTexture(gl.TEXTURE_2D, this.textures[0]);
    gl.generateMipmap(gl.TEXTURE_2D);
    gl.bindTexture(gl.TEXTURE_2D, null);
  }

  destroy() {
    const gl = this.gl;
    if (this.fbo) gl.deleteFramebuffer(this.fbo);
    for (const t of this.textures) gl.deleteTexture(t);
    this.fbo = null;
    this.textures = [];
    this.width = this.height = this.topLevel = 0;
    this.key = '';
  }
}

// ==== BEGIN FRAME ==========================================================
//
// One plugin instance: IsohypsePlugin's state, its ProcessOpenGL and the parts
// of InitGL that make GL objects. `params` is params[] by PT id, floats.

class IsohypseInstance {
  constructor(gl, quad, shared, isEffect) {
    this.gl = gl;
    this.quad = quad;
    this.programs = shared.programs;
    this.blank = shared.blank;
    this.format = shared.format;
    this.isEffect = isEffect;

    this.sweep = new SweepClock();
    this.lastNow = -1.0;
    this.restartHeld = false;
    this.spinAngle = 0.0;
    this.flowPhase = 0.0;
    this.morphClock = 0.0;
    // No host tempo reaches a page: SetBeatInfo is never called.
    this.beatSeen = false;
    this.hostBpm = 120.0;
    this.barsCounted = 0.0;
    this.hostBarPhase = 0.0;

    this.worldTexture = null;
    this.worldKey = -1;
    this.failedKey = -1;
    this.worldInfo = null;
    this.worldTop = 0;
    this.worldError = '';
    this.pending = null;
    this.onLoaded = null;

    this.generated = new Target(gl);
    this.generatedKey = null;
    this.generatedValid = false;
    this.clipHeight = new Target(gl);
    this.scene = new Target(gl);
    this.glowA = new Target(gl);
    this.glowB = new Target(gl);
    this.frameState = null;
  }

  dispose() {
    const gl = this.gl;
    for (const t of [this.generated, this.clipHeight, this.scene, this.glowA, this.glowB]) t.destroy();
    if (this.worldTexture) gl.deleteTexture(this.worldTexture);
    this.worldTexture = null;
    this.pending = null;
    this.onLoaded = null;
  }

  heightSpec(mipmapped, wrapX) {
    return { internal: this.format.internal, attachments: 1, linear: true, mipmapped, wrapX };
  }

  //-------------------------------------------------------------------------
  // The world. The plugin decodes its first world before the first frame and
  // later ones off the GL thread; a page can do neither synchronously, so
  // every world is the second case: fetched and decoded while the last one
  // (or nothing, at first) stays on screen.
  //-------------------------------------------------------------------------
  ensureWorld(choice, globe, place) {
    let info = null;
    let key = -1;
    if (choice === Choice.Globe && GLOBES.length) {
      info = GLOBES[globe];
      key = globe;
    } else if (choice === Choice.Place && place >= 0) {
      info = PLACES[place];
      key = kPlaceKeys + place;
    }
    if (info === null) return;

    const finish = (field, from, fromKey) => {
      if (field.error) {
        this.worldError = field.error;
        this.failedKey = fromKey;
        // eslint-disable-next-line no-console
        console.error(`isohypse: ${field.error}`);
        return;
      }
      this.uploadWorld(from, field, fromKey);
    };

    if (this.pending && this.pending.done) {
      const { field, key: pendingKey, info: pendingInfo } = this.pending;
      this.pending = null;
      if (pendingKey === key) {
        finish(field, pendingInfo, pendingKey);
        return;
      }
      // Decoded for a world no longer wanted: dropped.
    }
    if (key === this.worldKey || key === this.failedKey || this.pending) return;
    const pending = { key, info, done: false, field: null };
    this.pending = pending;
    LoadHeightfield(info).then((field) => {
      pending.field = field;
      pending.done = true;
      this.onLoaded?.();
    });
  }

  uploadWorld(info, field, key) {
    const gl = this.gl;
    if (this.worldTexture) gl.deleteTexture(this.worldTexture);
    const t = gl.createTexture();
    gl.bindTexture(gl.TEXTURE_2D, t);
    gl.pixelStorei(gl.UNPACK_ALIGNMENT, 2);
    const top = TopMipLevel(field.width, field.height);
    gl.texStorage2D(gl.TEXTURE_2D, top + 1, this.format.internal, field.width, field.height);
    let data = field.texels;
    if (this.format.type === gl.FLOAT) {
      data = new Float32Array(field.texels.length);
      for (let i = 0; i < data.length; i += 1) data[i] = field.texels[i] / 65535;
    }
    gl.texSubImage2D(gl.TEXTURE_2D, 0, 0, 0, field.width, field.height, gl.RG, this.format.type, data);
    gl.generateMipmap(gl.TEXTURE_2D);
    gl.pixelStorei(gl.UNPACK_ALIGNMENT, 4);
    applySampling(gl, true, true, info.kind === 'globe');
    gl.bindTexture(gl.TEXTURE_2D, null);
    this.worldTexture = t;
    this.worldInfo = info;
    this.worldKey = key;
    this.worldTop = top;
    this.worldError = '';
  }

  /** Is the world this frame wants on the GPU (or failed)? For the crosscheck,
   * which starts its frames only once it is, as the harness's synchronous load
   * does. */
  async settle(params) {
    for (let i = 0; i < 4000; i += 1) {
      const { choice, globe } = ChoiceOf(OptionIndex(params[PT.WORLD], WorldChoiceCount(this.isEffect)), this.isEffect);
      if (choice !== Choice.Globe && choice !== Choice.Place) return true;
      const place = OptionIndex(params[PT.PLACE], PLACES.length);
      this.ensureWorld(choice, globe, place);
      const key = choice === Choice.Globe ? globe : kPlaceKeys + place;
      if (this.worldKey === key) return true;
      if (this.failedKey === key) return false;
      await new Promise((resolve) => setTimeout(resolve, 5));
    }
    return false;
  }

  regenerate(params) {
    const gl = this.gl;
    const key = [params[PT.TERRAIN], params[PT.SEED], params[PT.FEATURE_SIZE], params[PT.ROUGHNESS], f32(this.morphClock)];
    if (this.generatedValid && key.every((v, i) => v === this.generatedKey[i])) return;
    this.generated.ensure(kGeneratedWidth, kGeneratedHeight, this.heightSpec(true, true));
    this.generatedKey = key;
    this.generatedValid = true;

    const seed = clampTo(lround(params[PT.SEED]), 0, kMaxSeed);
    const salt = (Math.imul(seed, 2654435761) + 0x6a09e667) >>> 0;
    const m = this.morphClock;
    const offset = [
      (17.0 * ((salt >>> 8) & 255)) / 255.0 + m,
      (17.0 * ((salt >>> 16) & 255)) / 255.0 + 0.61 * m,
      (17.0 * (salt >>> 24)) / 255.0 - 0.37 * m,
    ];
    this.generated.bind();
    const p = this.programs.generate.use();
    p.set('Size', kGeneratedWidth, kGeneratedHeight);
    p.setInt('TerrainKind', OptionIndex(params[PT.TERRAIN], Terrain.Count));
    p.set('Frequency', FeatureFrequencyFromParam(params[PT.FEATURE_SIZE]));
    p.set('Gain', f32(f32(0.3) + f32(f32(0.35) * clampTo(params[PT.ROUGHNESS], 0, 1))));
    p.set('Offset', offset[0], offset[1], offset[2]);
    p.setUint('Seed', salt);
    this.quad.draw();
    this.generated.generateMips();
  }

  /**
   * ProcessOpenGL. `input` is the clip ({texture, width, height}) for the Over,
   * null for the source. Draws into the canvas's framebuffer.
   */
  process(params, input, hostWidth, hostHeight, now) {
    const gl = this.gl;
    const width = input ? input.width : hostWidth;
    const height = input ? input.height : hostHeight;
    if (width <= 0 || height <= 0) return false;
    gl.disable(gl.BLEND);

    // Time: the host's (here, the page's), frame to frame. A bigger forward
    // step than a second, or any backward one, is a jump: no time passes.
    let dt = 0.0;
    if (this.lastNow >= 0.0) {
      const step = now - this.lastNow;
      if (step >= 0.0 && step <= kMaxFrameDelta) dt = step;
    }
    this.lastNow = now;

    // What to draw, and the clocks.
    const { choice, globe } = ChoiceOf(OptionIndex(params[PT.WORLD], WorldChoiceCount(this.isEffect)), this.isEffect);
    const place = PLACES.length === 0 ? -1 : OptionIndex(params[PT.PLACE], PLACES.length);
    this.ensureWorld(choice, globe, place);

    const restartDown = params[PT.RESTART] > 0.5;
    const sweepIn = {
      mode: OptionIndex(params[PT.SWEEP], Sweep.Count),
      seconds: SweepSecondsFromParam(params[PT.SWEEP_TIME]),
      bars: BarsOf(OptionIndex(params[PT.SWEEP_BARS], kBarChoices)),
      level: clampTo(params[PT.SWEEP_LEVEL], 0.0, 1.0),
      restart: restartDown && !this.restartHeld,
      haveBeat: this.beatSeen,
      barCount: this.barsCounted + this.hostBarPhase,
      fallbackBpm: this.beatSeen ? this.hostBpm : 120.0,
    };
    this.restartHeld = restartDown;
    this.sweep.Advance(dt, sweepIn);
    this.spinAngle = wrapDegrees(this.spinAngle + dt * SpinFromParam(params[PT.SPIN]));
    this.flowPhase += dt * FlowFromParam(params[PT.FLOW]);
    this.flowPhase -= kFlowWrap * Math.floor(this.flowPhase / kFlowWrap);
    this.morphClock += dt * MorphFromParam(params[PT.MORPH]);

    // The heightfield, and how the frame is projected onto it.
    const lines = LinesFromParam(params[PT.LINES]);
    const view = OptionIndex(params[PT.VIEW], View.Count);
    let heightTexture = null;
    let heightWidth = 1, heightHeight = 1, heightTop = 0;
    let datum = -0.5 / lines;
    let hasSea = false;
    let kind = Kind.Globe;
    let aspect = 1.0;

    if (choice === Choice.Generated) {
      this.regenerate(params);
      kind = Kind.Generated;
      heightTexture = this.generated.texture();
      heightWidth = kGeneratedWidth;
      heightHeight = kGeneratedHeight;
      heightTop = this.generated.topLevel;
      if (OptionIndex(params[PT.TERRAIN], Terrain.Count) === Terrain.Continents) {
        hasSea = true;
        datum = clampTo(params[PT.GENERATED_SEA], 0.0, 1.0);
      }
    } else if (choice === Choice.Clip && input) {
      kind = Kind.Clip;
      if (this.clipHeight.ensure(width, height, this.heightSpec(true, false))) {
        this.clipHeight.bind();
        const p = this.programs.intake.use();
        bindTexture(gl, 0, input.texture);
        p.setSampler('ClipIn', 0);
        p.setInt('InvertLuma', params[PT.INVERT] > 0.5 ? 1 : 0);
        this.quad.draw();
        bindTexture(gl, 0, null);
        this.clipHeight.generateMips();
        heightTexture = this.clipHeight.texture();
        heightWidth = width;
        heightHeight = height;
        heightTop = this.clipHeight.topLevel;
      }
    } else if (this.worldKey >= 0) {
      // The world on the GPU: the one asked for, or the last one while the new
      // one decodes. Its size is the file served here (half the plugin's for a
      // globe), which is what the shader's texel arithmetic must be told; the
      // plugin's table size is the same thing there.
      const info = this.worldInfo;
      kind = info.kind === 'globe' ? Kind.Globe : Kind.Place;
      heightTexture = this.worldTexture;
      heightWidth = info.servedWidth;
      heightHeight = info.servedHeight;
      heightTop = this.worldTop;
      hasSea = info.hasSea;
      if (hasSea) datum = SeaLevel(info);
      aspect = info.servedWidth / Math.max(info.servedHeight, 1);
    }

    let projection = 0;
    let centreX = 0.0, centreY = 0.0;
    let roll = RotationFromParam(params[PT.ROTATION]);
    if (kind === Kind.Globe || kind === Kind.Generated) {
      projection = view === View.Globe ? 1 : 0;
      centreX = wrapDegrees(LongitudeFromParam(params[PT.LONGITUDE]) + this.spinAngle);
      centreY = LatitudeFromParam(params[PT.LATITUDE]);
    } else if (kind === Kind.Place) {
      projection = 2;
      centreX = clampTo(params[PT.LONGITUDE], 0.0, 1.0) - 0.5;
      centreY = clampTo(params[PT.LATITUDE], 0.0, 1.0) - 0.5;
      roll = wrapDegrees(roll + this.spinAngle);
    } else {
      projection = 3;
      roll = 0.0;
    }
    // A place at Zoom 1 covers the frame: fitted to the wider of the two.
    let zoom = ZoomFromParam(params[PT.ZOOM]);
    if (kind === Kind.Place) zoom *= Math.max(1.0, width / height / aspect);
    const rasterK = height / 1080.0;

    this.frameState = {
      kind, projection, datum, hasSea, lines, zoom, centreLon: centreX, centreLat: centreY, roll,
      sweepPosition: this.sweep.Position(), sweepDirection: this.sweep.Direction(),
      drewWorld: heightTexture !== null, heightWidth, heightHeight,
    };

    const style = OptionIndex(params[PT.STYLE], Style.Count);
    const palette = paletteOf(style, params[PT.HUE]);

    // The map.
    this.scene.ensure(width, height, { internal: gl.RGBA16F, attachments: 2, linear: true, mipmapped: false, wrapX: false });
    this.scene.bind();
    gl.clearColor(0, 0, 0, 0);
    gl.clear(gl.COLOR_BUFFER_BIT);
    if (heightTexture !== null) {
      const s = this.programs.map.use();
      bindTexture(gl, 0, heightTexture);
      s.setSampler('Height', 0);
      s.set('HeightSize', heightWidth, heightHeight);
      s.set('HeightTop', heightTop);
      s.setInt('Projection', projection);
      s.setInt('WrapX', kind === Kind.Globe || kind === Kind.Generated ? 1 : 0);
      s.set('Raster', width, height);
      s.set('ViewOrigin', 0.0, 0.0);
      s.set('Centre', centreX, centreY);
      s.set('Zoom', zoom);
      s.set('Roll', (roll * Math.PI) / 180.0);
      gl.uniformMatrix3fv(s.location('Orientation'), true, orientation(centreX, centreY, roll));
      s.set('TexAspect', aspect);
      s.set('SmoothBias', SmoothFromParam(params[PT.SMOOTH]));
      s.set('Lines', lines);
      s.set('Datum', datum);
      s.set('FlowPhase', this.flowPhase);
      s.setInt('HasSea', hasSea ? 1 : 0);
      s.setInt('IndexEvery', clampTo(lround(params[PT.INDEX_EVERY]), 0, kMaxIndexEvery));
      s.set('LineWidth', LineWidthFromParam(params[PT.LINE_WIDTH]) * rasterK);
      s.set('SeaFill', clampTo(params[PT.SEA_FILL], 0, 1));
      s.set('Depths', clampTo(params[PT.DEPTHS], 0, 1));
      const wobble = clampTo(params[PT.WOBBLE], 0, 1);
      s.set('WobbleAmount', wobble * 4.0 * rasterK);
      s.set('WobbleScale', 90.0 * rasterK);
      s.setInt('SweepMode', sweepIn.mode);
      s.setInt('SweepDir', this.sweep.Direction());
      s.set('SweepPos', this.sweep.Position());
      s.set('Trail', TrailFromParam(params[PT.TRAIL]));
      s.set('FloorLevel', clampTo(params[PT.FLOOR], 0, 1));
      s.set('SweepGlow', clampTo(params[PT.SWEEP_GLOW], 0, 1));
      s.set('Flood', clampTo(params[PT.FLOOD], 0, 1));
      s.set('LineColour', toLinear(palette.line));
      s.set('IndexColour', toLinear(palette.index));
      s.set('AccentColour', toLinear(palette.accent));
      s.set('SeaLineColour', toLinear(palette.seaLine));
      s.set('SeaFillColour', toLinear(palette.seaFill));
      s.set('FloodColour', toLinear(palette.flood));
      s.set('DiscColour', toLinear(palette.disc));
      s.set('HeightColour', clampTo(params[PT.HEIGHT_COLOUR], 0, 1));
      s.set('Tints', clampTo(params[PT.TINTS], 0, 1));
      s.set('Shade', clampTo(params[PT.SHADE], 0, 1));
      s.setInt('DarkGround', palette.darkGround ? 1 : 0);
      s.setInt('LandPalette', style === Style.Survey ? 1 : 0);
      s.setInt('TestFlags', 0);
      this.quad.draw();
      bindTexture(gl, 0, null);
    }

    // The glow: the map's light, a quarter of the raster, blurred.
    const glowAmount = f32(clampTo(params[PT.GLOW], 0, 1) * f32(palette.glowBoost));
    const glowWidth = Math.floor((width + 3) / 4);
    const glowHeight = Math.floor((height + 3) / 4);
    const glowSpec = { internal: gl.RGBA16F, attachments: 1, linear: true, mipmapped: false, wrapX: false };
    const glowing = glowAmount > 0 && heightTexture !== null
      && this.glowA.ensure(glowWidth, glowHeight, glowSpec)
      && this.glowB.ensure(glowWidth, glowHeight, glowSpec);
    if (glowing) {
      this.glowA.bind();
      {
        const p = this.programs.glowDown.use();
        bindTexture(gl, 0, this.scene.texture(1));
        p.setSampler('Source', 0);
        p.set('SourceSize', width, height);
        this.quad.draw();
      }
      const p = this.programs.glowBlur.use();
      p.setSampler('Source', 0);
      this.glowB.bind();
      bindTexture(gl, 0, this.glowA.texture());
      p.set('Step', 1.0 / glowWidth, 0.0);
      this.quad.draw();
      this.glowA.bind();
      bindTexture(gl, 0, this.glowB.texture());
      p.set('Step', 0.0, 1.0 / glowHeight);
      this.quad.draw();
      bindTexture(gl, 0, null);
    }

    // Composite, into the host's framebuffer (the canvas).
    gl.bindFramebuffer(gl.FRAMEBUFFER, null);
    gl.viewport(0, 0, hostWidth, hostHeight);
    {
      const s = this.programs.composite.use();
      bindTexture(gl, 0, this.scene.texture(0));
      bindTexture(gl, 1, glowing ? this.glowA.texture() : this.blank);
      bindTexture(gl, 2, input ? input.texture : this.blank);
      s.setSampler('Picture', 0);
      s.setSampler('Glow', 1);
      s.setSampler('ClipIn', 2);
      s.set('Ground', toLinear(palette.ground));
      s.set('GroundAlpha', this.isEffect ? 1.0 : clampTo(params[PT.BACKGROUND], 0, 1));
      s.set('GlowAmount', glowing ? glowAmount : 0.0);
      s.setInt('IsEffect', this.isEffect ? 1 : 0);
      s.set('ClipShow', clampTo(params[PT.CLIP_SHOW], 0, 1));
      s.set('MixAmount', this.isEffect ? clampTo(params[PT.MIX], 0, 1) : 1.0);
      s.set('Raster', width, height);
      s.set('ViewOrigin', 0.0, 0.0);
      this.quad.draw();
      for (let unit = 2; unit >= 0; unit -= 1) bindTexture(gl, unit, null);
      gl.activeTexture(gl.TEXTURE0);
    }
    return true;
  }
}

// ==== END FRAME ============================================================

function createRenderer(gl, quad) {
  // InitGL: the quad's vertex stage is kVersion + kQuadVertex; each fragment
  // stage is Assemble( pieces ) = kVersion + kCommon + each piece in turn.
  const quadVertex = S.VERSION + S.QUAD_VERTEX;
  const assemble = (...pieces) => S.VERSION + S.COMMON + pieces.join('');
  const programs = {
    generate: new Program(gl, quadVertex, assemble(S.GENERATE_FRAGMENT), 'generate'),
    intake: new Program(gl, quadVertex, assemble(S.INTAKE_FRAGMENT), 'intake'),
    map: new Program(gl, quadVertex, assemble(S.MAP_LIBRARY, S.MAP_FRAGMENT), 'map'),
    glowDown: new Program(gl, quadVertex, assemble(S.GLOW_DOWN_FRAGMENT), 'glow down'),
    glowBlur: new Program(gl, quadVertex, assemble(S.GLOW_BLUR_FRAGMENT), 'glow blur'),
    composite: new Program(gl, quadVertex, assemble(S.COMPOSITE_FRAGMENT), 'composite'),
  };

  // A sampler bound to texture 0 is "unloadable" to Apple's GL, and an empty
  // unit is a warning in a browser: every sampler a pass does not use gets this.
  const blank = gl.createTexture();
  gl.bindTexture(gl.TEXTURE_2D, blank);
  gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA16F, 1, 1, 0, gl.RGBA, gl.FLOAT, new Float32Array(4));
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
  gl.bindTexture(gl.TEXTURE_2D, null);

  const shared = { programs, blank, format: heightFormat(gl) };
  const hostParams = new Float32Array(kParamSlots);
  let instance = null;
  let instanceVariant = null;
  let onLoaded = null;

  const fresh = (variant) => {
    instance?.dispose();
    instance = new IsohypseInstance(gl, quad, shared, variant === 'over');
    instanceVariant = variant;
    instance.onLoaded = () => onLoaded?.();
    return instance;
  };

  return {
    gl,
    get instance() { return instance; },
    get format() { return shared.format; },
    programs,
    /** Set by the crosscheck: the page's own frames stop touching the instance. */
    held: false,
    set onLoaded(fn) { onLoaded = fn; },
    fresh,
    hostParams,

    render({ input, params, width, height, time, variant }) {
      if (this.held) return;
      if (!instance || instanceVariant !== variant) fresh(variant);
      // The plugin's params[]: what the host set, as a float. A control this
      // plugin does not declare keeps its constructor value.
      const defaults = constructorDefaults(instance.isEffect);
      hostParams.set(defaults);
      for (const p of PARAMS) {
        if (p.only && p.only !== variant) continue;
        hostParams[p.slot] = params.get(p.id);
      }
      instance.process(hostParams, instance.isEffect ? input : null, width, height, time);
      // A host lets go of an event; the kit has none, so the page does.
      if (hostParams[PT.RESTART] > 0.5) params.set('restart', 0);
    },
  };
}

//===========================================================================
// The parameters: the constructor's, in HostOrder()'s order and groups, with
// the plugin's names, element lists and defaults. `only` says which plugin
// declares it.
//===========================================================================
const fix = (n, d = 2) => n.toFixed(d);
const signed = (n, d = 1) => `${n >= 0 ? '+' : '−'}${Math.abs(n).toFixed(d)}`;
const sig = (n) => (n >= 100 ? n.toFixed(0) : n >= 10 ? n.toFixed(1) : n >= 1 ? n.toFixed(2) : n.toFixed(3));
const V = (v) => f32(v);

const WORLD_NAMES = Array.from({ length: WorldChoiceCount(true) }, (_, i) => WorldChoiceName(i, true));
const PLACE_NAMES = PLACES.map((p) => p.name);
const INDEX_EVERY_NAMES = Array.from({ length: kMaxIndexEvery + 1 }, (_, i) => (i === 0 ? '0 (none)' : String(i)));
const SEED_NAMES = Array.from({ length: kMaxSeed + 1 }, (_, i) => String(i));

/** id, ParamId, FFGL type, display, hint, elements, only. Hints from the
 * README and Controls.h. */
const SPEC = [
  ['world', PT.WORLD, 'option', null, 'Which world: the nine globes from their missions’ height maps, Place (the Place control picks which), Generated (a world of its own), and on the Over, Clip (the clip’s brightness as height).', WORLD_NAMES],
  ['place', PT.PLACE, 'option', null, 'World Place: which of the twenty-two tiles of Earth.', PLACE_NAMES],
  ['view', PT.VIEW, 'option', null, 'A globe or Generated world as a ball (Globe) or flat, longitude across and latitude up (Map). A place is always flat.', VIEW_NAMES],
  ['longitude', PT.LONGITUDE, 'standard', (v) => `${signed(LongitudeFromParam(V(v)), 0)}°`, 'The longitude in the middle of the view, −180° to +180°. On a place, a pan of (value − 0.5) tile widths.'],
  ['latitude', PT.LATITUDE, 'standard', (v) => `${signed(LatitudeFromParam(V(v)), 0)}°`, 'The latitude in the middle of the view, −90° to +90°. On a place, a pan of (value − 0.5) tile heights.'],
  ['zoom', PT.ZOOM, 'standard', (v) => `×${sig(ZoomFromParam(V(v)))}`, '1 (the whole world fits the height) to 64, geometrically. A place at 1 covers the frame.'],
  ['spin', PT.SPIN, 'standard', (v) => `${signed(SpinFromParam(V(v)))}°/s`, '±60° a second, squared either side of the middle: a globe turns, a map scrolls, a place rotates.'],
  ['rotation', PT.ROTATION, 'standard', (v) => `${signed(RotationFromParam(V(v)), 0)}°`, 'The view turned, −180° to +180°.'],

  ['lines', PT.LINES, 'standard', (v) => sig(LinesFromParam(V(v))), 'How many contours from the world’s lowest point to its highest, 4 to 300, geometrically.'],
  ['indexEvery', PT.INDEX_EVERY, 'integer', null, 'A heavier contour every n-th; 0 for none. FF_TYPE_INTEGER, 0 to 10, in the plugin.', INDEX_EVERY_NAMES],
  ['lineWidth', PT.LINE_WIDTH, 'standard', (v) => `${sig(LineWidthFromParam(V(v)))} px`, '0.4 to 10 pixels at 1080 lines, scaled with the raster’s height.'],
  ['smooth', PT.SMOOTH, 'standard', (v) => `+${fix(SmoothFromParam(V(v)), 1)} mip`, 'Blurs the heights before they are contoured: 0 to 6 extra mip levels.'],
  ['seaFill', PT.SEA_FILL, 'standard', (v) => fix(V(v)), 'A world with a sea: how much the sea is filled in.'],
  ['depths', PT.DEPTHS, 'standard', (v) => fix(V(v)), 'A world with a sea: how strongly the contours below sea level are drawn.'],

  ['sweep', PT.SWEEP, 'option', null, 'Off; Rise (up through the world’s range, and again); Fall; Bounce; Manual (Sweep Level says where).', SWEEP_NAMES],
  ['sweepTime', PT.SWEEP_TIME, 'standard', (v) => `${sig(SweepSecondsFromParam(V(v)))} s`, 'One pass, 0.5 to 120 seconds, geometrically (Sweep Bars Free).'],
  ['sweepBars', PT.SWEEP_BARS, 'option', null, 'Free runs on Sweep Time; the rest lock a pass to that many bars of the host’s tempo. No host sends one here, so this runs on the plugin’s own 120 BPM, as it does in a host that sends none.', BAR_NAMES],
  ['sweepLevel', PT.SWEEP_LEVEL, 'standard', (v) => fix(V(v), 3), 'Sweep Manual: where the sweep is, 0 the lowest ground to 1 the highest.'],
  ['sweepGlow', PT.SWEEP_GLOW, 'standard', (v) => fix(V(v)), 'The sweep’s own glow, and the flash a contour gets as it is crossed.'],
  ['trail', PT.TRAIL, 'standard', (v) => `${fix(TrailFromParam(V(v)))} pass`, 'How long a line stays lit after the sweep crosses it, in passes, 0 to 1: (1 − age / Trail)².'],
  ['floor', PT.FLOOR, 'standard', (v) => fix(V(v)), 'How bright a line is when the sweep has not crossed it lately. At 0 a Rise draws the map from the bottom up.'],
  ['flood', PT.FLOOD, 'standard', (v) => fix(V(v)), 'Everything below the sweep filled in: the sea rising.'],
  ['flow', PT.FLOW, 'standard', (v) => `${signed(FlowFromParam(V(v)), 2)} lv/s`, 'Every contour drifting up or down, ±4 levels a second, squared either side of the middle.'],
  ['restart', PT.RESTART, 'event', null, 'Starts the pass again (on Sweep Bars, at the bar it is pressed in). FF_TYPE_EVENT in the plugin; a toggle the page releases after one frame here.'],

  ['style', PT.STYLE, 'option', null, 'Ink (white lines on black), Plotter (a pen on paper), Neon (coloured light) or Survey (the cartographer’s: brown contours, blue water, cream paper).', STYLE_NAMES],
  ['hue', PT.HUE, 'standard', (v) => `hue ${fix(V(v))}`, 'The sweep’s colour, and the lines’ in Neon.'],
  ['heightColour', PT.HEIGHT_COLOUR, 'standard', (v) => fix(V(v)), 'Each contour in its height’s colour.'],
  ['glow', PT.GLOW, 'standard', (v) => fix(V(v)), 'The lines’ glow: a quarter-raster Gaussian of their light.'],
  ['shade', PT.SHADE, 'standard', (v) => fix(V(v)), 'Hill-shading, lit from the upper left, in the lines’ colour.'],
  ['tints', PT.TINTS, 'standard', (v) => fix(V(v)), 'Each band between two contours in its height’s colour.'],
  ['wobble', PT.WOBBLE, 'standard', (v) => fix(V(v)), 'A hand on the pen: up to 4 pixels (at 1080 lines) of slow wander.'],
  ['background', PT.BACKGROUND, 'standard', (v) => fix(V(v)), 'The source only: the style’s ground. 0 leaves the world on transparency, to layer.', null, 'source'],

  ['terrain', PT.TERRAIN, 'option', null, 'World Generated: Continents (with a sea, at Sea), Ridges (mountain chains) or Craters (a battered moon).', TERRAIN_NAMES],
  ['seed', PT.SEED, 'integer', null, 'World Generated: a different world for each, 0 to 999. FF_TYPE_INTEGER in the plugin.', SEED_NAMES],
  ['featureSize', PT.FEATURE_SIZE, 'standard', (v) => `f ${sig(FeatureFrequencyFromParam(V(v)))}`, 'World Generated: the base frequency on the unit sphere, 6 down to 0.6, geometrically. Bigger is bigger continents, ranges and craters.'],
  ['roughness', PT.ROUGHNESS, 'standard', (v) => `gain ${fix(0.3 + 0.35 * clamp01(V(v)))}`, 'World Generated: how much each finer octave adds, a gain of 0.3 to 0.65.'],
  ['generatedSea', PT.GENERATED_SEA, 'standard', (v) => fix(V(v)), 'World Generated, Continents: the sea level, as a share of the world’s range.'],
  ['morph', PT.MORPH, 'standard', (v) => `${fix(MorphFromParam(V(v)), 3)}/s`, 'World Generated: how fast the world changes, 0 to 0.2 units a second. Redrawn every frame while it moves.'],

  ['clipShow', PT.CLIP_SHOW, 'standard', (v) => fix(V(v)), 'How much of the clip stays under the lines.', null, 'over'],
  ['invert', PT.INVERT, 'boolean', null, 'World Clip: dark is high instead of bright.', null, 'over'],
  ['mix', PT.MIX, 'standard', (v) => fix(V(v)), 'The whole effect against the clip. At 0 the clip comes back as it went in.', null, 'over'],
];

const query = new URLSearchParams(window.location.search);
const initialVariant = query.get('plugin') === 'over' ? 'over' : 'source';

function buildParams(variant) {
  const effect = variant === 'over';
  const defaults = constructorDefaults(effect);
  return SPEC.map(([id, slot, ffType, display, hint, elements, only]) => {
    if (NameOf(slot) === '?' || !HostOrder(true).concat(HostOrder(false)).includes(slot)) throw new Error(`${id}: not a declared parameter`);
    const p = { id, slot, name: NameOf(slot), group: GroupOf(slot), hint, only };
    if (ffType === 'option' || ffType === 'integer') Object.assign(p, { type: 'option', elements: elements.slice() });
    else if (ffType === 'boolean' || ffType === 'event') p.type = 'boolean';
    else Object.assign(p, { type: 'standard', display });
    if (id === 'world') p.elements = WORLD_NAMES.slice(0, WorldChoiceCount(effect));
    p.default = defaults[slot];
    return p;
  });
}

const PARAMS = buildParams(initialVariant);

let renderer = null;

const mounted = mountDemo({
  name: 'Isohypse',
  pluginId: 'IH01 · IH02',
  kind: ['source', 'effect'],
  tagline:
    'Contour lines of real worlds, swept through by a line climbing from the lowest ground to the highest: the Moon, Mars, Earth and the rest of the rocky solar system from their missions’ own height maps, twenty-two places on Earth, a world it makes up, or your clip. A contour is where the height is a whole number of intervals, and its width is measured in pixels, so a line is as wide on a cliff as on a plain. SW Isohypse draws a world on black, paper or nothing; SW Isohypse Over draws it over the clip, or reads the clip itself as the map.',
  repo: 'https://github.com/stoatworks-labs/isohypse',
  page: 'https://stoatworks-labs.com/software/isohypse/',
  video: 'https://www.youtube.com/watch?v=8brna6mUpxE',

  blurb:
    'It is Isohypse’s own GLSL, ported from the repository to WebGL2 unedited, fed by a JavaScript port of the plugin’s CPU half (every control’s mapping, the sweep’s clock, the map decoder and gap filler, the styles). The maps are the plugin’s own: the places as they ship, the globes at half the resolution. SW Isohypse reads no video; SW Isohypse Over runs on generated clips or your own.',

  // The source with Background 0 leaves the world on transparency.
  showBackdrop: true,
  needFloat: true,

  variants: {
    label: 'Plugin',
    default: initialVariant,
    options: [
      { id: 'source', name: 'SW Isohypse (source)', hint: 'IH01, FF_SOURCE: a world’s contours on black, paper or nothing. Reads no clip.' },
      { id: 'over', name: 'SW Isohypse Over (effect)', hint: 'IH02, FF_EFFECT: the same over the clip, or the clip as the map.' },
    ],
  },

  // For the Over only. The scene has a full range of brightness to read as
  // height; the ramps make the contours' spacing legible; lights on black are
  // peaks on a plain; the geometry card is a map of plateaus.
  sources: ['scene', 'ramp', 'spot', 'grid', 'detail', 'bars'],

  params: PARAMS,

  differences: [
    'The maps are the plugin’s, but the GLOBES are at half its resolution: Earth 2,048 × 1,024 here against the plugin’s 4,096 × 2,048, the other eight 1,024 × 512 against 2,048 × 1,024. Each pixel here is the exact mean of a 2 × 2 block of the plugin’s, made by demo/tools/make_worlds.py (which verify.sh checks against data/), the box a mipmap takes one level down; Venus’s, Pluto’s and Charon’s gaps are kept by the rule the plugin’s maps were made with. At this page’s 960 × 540 a whole globe is 500 pixels across, and the plugin would be reading a mip level at least one down anyway; at 1920 × 1080, or zoomed in, the plugin draws detail this page does not have. The twenty-two PLACES are the plugin’s files byte for byte. No height is rescaled, so Lines, the datum and the coast are the plugin’s whatever the resolution.',
    'Loading: the plugin decodes its first world before its first frame, from inside its own bundle (or DLL). Here every world, the first included, is fetched and decoded in the page, so the first frames show the ground and nothing else until the Moon arrives (0.9 MB; Earth is 3.1 MB). As in the plugin, a world chosen mid-run leaves the last one on screen until the new one is up. The PNGs are decoded by a 16-bit decoder in plugin.js, not by the browser, which would hand back 8 bits and turn every map into 256 terraces.',
    'The CPU half is a PORT, not the plugin’s code: Controls.cpp (every mapping), SweepClock.cpp (the sweep, in passes, in double), Worlds.cpp’s dropdowns, decode and gap filler (Venus, Pluto and Charon are filled under their gaps exactly as the plugin fills them), the styles’ colours and the globe’s orientation, and the CPU half of ProcessOpenGL that turns them into uniforms. Only a reader checks the translation line by line. What is measured is the picture: demo/tools/crosscheck/ renders the plugin’s own harness (ihtest) and this page at the same settings and compares them. On 28 October 2026, served with the plugin’s full-size globes, twelve cases across both plugins, every style, globes, maps, places, Generated and the Over’s Clip came out with at least 99.979% of pixels within one level of 255 of the plugin’s, five of them 100.000% to three places; a page one frame late was 88.5% and fails. On the half-size globes it ships with, 99.68% to 99.98% at 1280 × 720. The details are in the repository’s AGENTS.md.',
    'The GPU half is not a port: the generate, intake, map, glow and composite passes are the plugin’s own GLSL, on the plugin’s formats where WebGL2 has them: the map’s two RGBA16F targets (EXT_color_buffer_float, without which the page stops and says so) and an RG16 heightfield, which needs EXT_texture_norm16. Without that the heights go into 32-bit float, which holds every 16-bit height exactly, or failing that half float, which terraces a gentle slope; the line under the picture says which. demo/tools/check_shaders.py fails the repository’s verify script if a character of the shaders, or the way they are joined, drifts.',
    'Sweep Bars: no host tempo reaches a page, so a pass locked to bars runs on the plugin’s own fallback of 120 BPM in 4/4, which is what the plugin does in a host that sends no tempo. In Resolume it follows Resolume’s bars and two layers on the same setting agree. The plugin has no audio input, so nothing else is missing for want of one.',
    'Restart is FF_TYPE_EVENT in the plugin: here it is a toggle the page releases after one frame, one rising edge, which is what the plugin counts. Index Every and Seed are FF_TYPE_INTEGER (0 to 10, 0 to 999): here they are dropdowns of every value they take. Invert is a boolean. The About block is not on the panel.',
    'Switching the Plugin dropdown is a new instance, as it would be in Resolume, with that plugin’s own defaults: the Over starts on World Clip, with no Spin, 24 Lines and a higher Smooth. The Over has no Background (its ground is the clip, mixed in by Clip Show) and three more controls (Clip Show, Invert, Mix), shown only when it is chosen; Clip is in its World list only.',
    'The clock is the page’s: seconds from frame deltas, capped at a tenth of a second a frame. The plugin’s vote on whether the host sends seconds or milliseconds is not ported, because this host’s unit is known. The transport’s Restart is a clock jump, which the plugin takes as no time passing, so the sweep carries on (the Restart control is what starts a pass again). Paused, nothing moves; Step is a sixtieth of a second.',
    'The Over: the clip is the kit’s generated one or your own file, 8-bit as Resolume hands an effect its input. With World on Clip, a still of any greyscale heightmap becomes a map, which is how the plugin reaches Middle-earth: it is not bundled, here or there, because every heightmap of it is drawn from maps that are the Tolkien Estate’s.',
    'Generated with Morph up redraws a 2,048 × 1,024 world every frame, as the plugin does; it is the expensive case (1.9 to 3.3 ms a frame at 1080p on the GPU the plugin was measured on, against 0.44 for the Moon), and a browser on a small GPU may drop frames there.',
    'Nothing here is measured. The plugin’s harness holds a ramp to exactly the number of contours it should cross, each within 0.013 px of where the arithmetic puts it; a contour’s width within 3% on slopes eight times apart; every contour’s strength behind the sweep within 0.0005 of the CPU’s model of it; and Everest, the Mariana Trench, Olympus Mons, Hellas and Maxwell Montes at their heights. Five wrong models and thirteen mutants are all caught. The plugin has been loaded in Resolume Arena 7.28.1 on macOS and 7.27.1 on Windows (software rendering). That harness and those runs, not this page, are the reason to believe the picture.',
  ],

  createRenderer: (gl, quad) => {
    renderer = createRenderer(gl, quad);
    return renderer;
  },
});

//---------------------------------------------------------------------------
// Which controls belong to which plugin; the World picker; the status line.
//
// By inline style, not the `hidden` attribute: kit.css gives these elements a
// `display` of their own, which beats the attribute's user-agent rule.
//---------------------------------------------------------------------------
if (mounted) {
  const params = mounted.params;
  const embed = query.has('embed') && query.get('embed') !== '0';
  let shownVariant = mounted.state.variant;

  // A world arriving while paused still has to be drawn.
  if (renderer) renderer.onLoaded = () => mounted.redraw();

  // Copy link carries the plugin, since the two declare different controls.
  const toQuery = params.toQuery.bind(params);
  params.toQuery = () => {
    const q = toQuery();
    if (mounted.state.variant === 'over') q.set('plugin', 'over');
    return q;
  };

  const show = (node, on) => { if (node) node.style.display = on ? '' : 'none'; };
  const rowOf = (p) => document.getElementById(`p-${p.id}`)?.closest('.prow')
    ?? [...document.querySelectorAll('.prow')].find((row) => row.querySelector('.prow__name')?.textContent === p.name);

  /** The World dropdown's elements and the constructor's defaults, per plugin. */
  function applyVariant(variant) {
    const effect = variant === 'over';
    const fresh = buildParams(variant);
    for (const p of PARAMS) {
      const f = fresh.find((q) => q.id === p.id);
      p.default = f.default;
      params.defaults[p.id] = f.default;
      if (p.id === 'world') p.elements = f.elements;
    }
    const select = document.getElementById('p-world');
    if (select) {
      while (select.options.length > WorldChoiceCount(effect)) select.remove(select.options.length - 1);
      for (let i = select.options.length; i < WorldChoiceCount(effect); i += 1) select.append(new Option(WORLD_NAMES[i], String(i)));
    }
  }

  function showForVariant(variant) {
    for (const p of PARAMS) {
      if (p.only) show(rowOf(p), p.only === variant);
    }
    for (const heading of document.querySelectorAll('.pgroup__name')) {
      if (heading.textContent === 'Clip') show(heading.closest('.pgroup'), variant === 'over');
    }
    const effect = variant === 'over';
    for (const field of document.querySelectorAll('.transport__field')) {
      if (field.querySelector('.transport__label')?.textContent === 'Clip') show(field, effect);
    }
    show(document.querySelector('.transport__file'), effect);
  }

  document.addEventListener('demo:state', () => {
    const variant = mounted.state.variant;
    if (variant !== shownVariant) {
      shownVariant = variant;
      // A new instance: that plugin's constructor's defaults.
      applyVariant(variant);
      params.reset();
      syncPicker?.();
    }
    if (!embed) showForVariant(variant);
  });

  let syncPicker = null;

  if (!embed) {
    showForVariant(shownVariant);

    const stage = document.querySelector('.stage');
    if (stage) {
      //---------------------------------------------------------------------
      // The World picker: the plugin's own World control, as buttons.
      //---------------------------------------------------------------------
      const picker = document.createElement('div');
      picker.className = 'transport';
      picker.setAttribute('role', 'group');
      picker.setAttribute('aria-label', 'World');
      const label = document.createElement('span');
      label.className = 'transport__label';
      label.textContent = 'World';
      label.title = 'The plugin’s World control (it is also the first row of the panel)';
      picker.append(label);
      const buttons = WORLD_NAMES.map((name, index) => {
        const b = document.createElement('button');
        b.type = 'button';
        b.className = 'btn';
        b.textContent = name;
        b.addEventListener('click', () => params.set('world', index));
        picker.append(b);
        return b;
      });
      syncPicker = () => {
        const effect = mounted.state.variant === 'over';
        const current = Math.round(params.get('world'));
        buttons.forEach((b, i) => {
          show(b, i < WorldChoiceCount(effect));
          b.setAttribute('aria-pressed', String(i === current));
          b.style.outline = i === current ? '2px solid currentColor' : '';
        });
      };
      params.addEventListener('change', syncPicker);
      params.addEventListener('reset', syncPicker);
      syncPicker();
      stage.querySelector('.transport')?.after(picker);

      //---------------------------------------------------------------------
      // A status line: reports what the page is drawing, measures nothing.
      //---------------------------------------------------------------------
      const line = document.createElement('p');
      line.className = 'stage__status';
      line.id = 'isohypse-stats';
      line.setAttribute('aria-live', 'off');
      stage.append(line);
      const note = document.createElement('p');
      note.className = 'stage__status';
      note.id = 'isohypse-note';
      stage.append(note);
      const PROJECTION_NAMES = ['as a map', 'as a globe', 'as a tile', 'the clip, pixel for pixel'];
      setInterval(() => {
        const inst = renderer?.instance;
        const s = inst?.frameState;
        if (!s) return;
        const effect = inst.isEffect;
        const { choice, globe } = ChoiceOf(OptionIndex(params.get('world'), WorldChoiceCount(effect)), effect);
        const wanted = choice === Choice.Globe ? GLOBES[globe] : choice === Choice.Place ? PLACES[OptionIndex(params.get('place'), PLACES.length)] : null;
        let what;
        if (s.kind === Kind.Generated) what = `Generated (${TERRAIN_NAMES[OptionIndex(params.get('terrain'), 3)]}), ${kGeneratedWidth} × ${kGeneratedHeight}`;
        else if (s.kind === Kind.Clip) what = `the clip as the map, ${s.heightWidth} × ${s.heightHeight}`;
        else if (s.drewWorld && inst.worldInfo) {
          const w = inst.worldInfo;
          what = `${w.name}, ${w.servedWidth} × ${w.servedHeight}${w.servedWidth !== w.width ? ` here (the plugin’s ${w.width} × ${w.height})` : ' (the plugin’s own file)'}`;
        } else what = 'no world yet';
        const dir = s.sweepDirection > 0 ? 'rising' : s.sweepDirection < 0 ? 'falling' : 'held';
        const sweepMode = OptionIndex(params.get('sweep'), Sweep.Count);
        line.textContent = `${what} · ${PROJECTION_NAMES[s.projection]}`
          + ` · ${sig(s.lines)} lines · datum ${s.datum.toFixed(4)}`
          + (sweepMode === Sweep.Off ? ' · sweep off' : ` · sweep at ${s.sweepPosition.toFixed(3)}, ${dir}`);
        const loading = inst.pending && !inst.pending.done && wanted ? `Loading ${wanted.name}… ` : '';
        const failed = inst.worldError ? `Could not load: ${inst.worldError}. ` : '';
        const fmt = renderer.format;
        note.textContent = `${failed}${loading}Heights in ${fmt.name}`
          + (fmt.name === 'RG16' ? ', as the plugin holds them.'
            : fmt.exact ? ' (this browser has no EXT_texture_norm16; 32-bit float holds every 16-bit height exactly).'
              : ' (this browser has neither EXT_texture_norm16 nor OES_texture_float_linear: half float, which terraces gentle slopes the plugin draws smoothly).');
      }, 250);
    }
  }

  //-------------------------------------------------------------------------
  // For the crosscheck (AGENTS.md): a fresh instance, the harness's settings,
  // `frames` frames a sixtieth of a second apart starting at 0 -- as ihtest
  // renders them -- read back as RGBA8, top row first. The world is loaded
  // before frame 0, as the harness's synchronous load has it. Read-only for
  // the page otherwise.
  //-------------------------------------------------------------------------
  async function renderCase({ variant = 'source', settings = {}, frames = 60, width = 1280, height = 720, clip = null, fps = 60 }) {
    mounted.state.playing = false;
    renderer.held = true;
    const gl = renderer.gl;
    const canvas = gl.canvas;
    canvas.width = width;
    canvas.height = height;
    const inst = renderer.fresh(variant);
    inst.onLoaded = null;
    const declared = buildParams(variant);
    const host = constructorDefaults(inst.isEffect);
    for (const [name, value] of Object.entries(settings)) {
      const slot = HostOrder(inst.isEffect).find((id) => NameOf(id) === name);
      if (slot === undefined) throw new Error(`${name}: not a parameter of this plugin`);
      const spec = declared.find((p) => p.slot === slot);
      const index = spec.type === 'option' ? spec.elements.indexOf(String(value)) : -1;
      host[slot] = index >= 0 ? index : Number(value);
    }
    let input = null;
    if (inst.isEffect) {
      // `clip`: RGBA8, top row first, base64 -- what ihtest --pipe reads. GL's
      // first row is the bottom one.
      const top = Uint8Array.from(atob(clip), (c) => c.charCodeAt(0));
      const bytes = new Uint8Array(top.length);
      const stride = width * 4;
      for (let y = 0; y < height; y += 1) bytes.set(top.subarray((height - 1 - y) * stride, (height - y) * stride), y * stride);
      const t = gl.createTexture();
      gl.bindTexture(gl.TEXTURE_2D, t);
      gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA8, width, height, 0, gl.RGBA, gl.UNSIGNED_BYTE, bytes);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
      gl.bindTexture(gl.TEXTURE_2D, null);
      input = { texture: t, width, height };
    }
    if (!(await inst.settle(host))) throw new Error(`the world did not load: ${inst.worldError}`);
    // Sized after the wait: nothing of the page's may run between here and
    // the read-back.
    canvas.width = width;
    canvas.height = height;
    for (let f = 0; f < frames; f += 1) {
      gl.bindFramebuffer(gl.FRAMEBUFFER, null);
      gl.viewport(0, 0, width, height);
      gl.clearColor(0, 0, 0, 0);
      gl.clear(gl.COLOR_BUFFER_BIT);
      inst.process(host, input, width, height, f / fps);
    }
    const pixels = new Uint8Array(width * height * 4);
    gl.readPixels(0, 0, width, height, gl.RGBA, gl.UNSIGNED_BYTE, pixels);
    const flipped = new Uint8Array(pixels.length);
    const row = width * 4;
    for (let y = 0; y < height; y += 1) flipped.set(pixels.subarray((height - 1 - y) * row, (height - y) * row), y * row);
    let binary = '';
    for (let i = 0; i < flipped.length; i += 0x8000) binary += String.fromCharCode(...flipped.subarray(i, i + 0x8000));
    const s = inst.frameState;
    return { width, height, rgba: btoa(binary), sweep: s.sweepPosition, lines: s.lines, datum: s.datum, format: renderer.format.name };
  }

  window.isohypseDemo = {
    mounted, PARAMS, PT, renderCase,
    get renderer() { return renderer; },
    port: { ChoiceOf, WorldIndexOf, constructorDefaults, decodePng16, FillGaps, SweepClock, LinesFromParam, orientation, paletteOf },
  };
}
