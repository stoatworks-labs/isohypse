#!/usr/bin/env python3
"""Build Isohypse's heightmaps from public elevation data.

Writes, relative to the repo root (the folder above this script):
  data/<world>.png         16-bit greyscale heightmaps (encoding below)
  data/SOURCES.md          where every byte came from, checksums, licences
  source/WorldTable.inc    the X-macro table the plugin compiles in

Needs numpy, Pillow and rasterio (rasterio brings its own GDAL):

  python3 -m venv /tmp/isohypse-venv
  /tmp/isohypse-venv/bin/pip install numpy pillow rasterio
  /tmp/isohypse-venv/bin/python -I tools/make-worlds.py --cache <download dir> \
      [--previews <dir>] [--only earth,mars,london,...] [--offline]

Downloads are cached in --cache and reused; delete a file there to fetch it again.
Downloaded files are untrusted data: keep --cache outside the repo and run with -I.

Encoding (every PNG): linear 16-bit, v = round((h - min) / (max - min) * 65535), h in
metres, min/max the extremes of the stored data (rounded outwards to 0.1 m; those are
the values in WorldTable.inc). A world with gaps reserves v = 0 for "no data" and maps
the data to 1..65535: v = 1 + round((h - min) / (max - min) * 65534).

Globes are equirectangular, pixel-is-area: row 0 is the north edge (+90), column 0 the
west edge at longitude -180, east-positive, so pixel (i, j) has its centre at
lon = -180 + (i + 0.5) * 360 / W, lat = 90 - (j + 0.5) * 180 / H. They are downsampled
by area averaging (an exact box filter: each output pixel is the mean of the source
pixels it covers, partial pixels weighted by the fraction covered).

Places are Web Mercator, 512 x 512, each pixel one zoom-z Web Mercator pixel
(metres per pixel at the centre = 156543.03392 * cos(lat) / 2^z), built from AWS
Terrarium tiles fetched at zoom z+1 and 2 x 2 box-averaged. The crop is aligned to
the zoom-z pixel grid, so the given point lies within half a pixel of the image centre.
"""

import argparse
import concurrent.futures as cf
import hashlib
import http.client
import json
import math
import os
import re
import sys
import threading
import time
import urllib.parse
import urllib.request
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw

try:
    import rasterio
    from rasterio.windows import Window
except ImportError:  # pragma: no cover
    sys.exit("make-worlds.py needs rasterio: pip install numpy pillow rasterio (in a venv)")

ROOT = Path(__file__).resolve().parent.parent
DATA = ROOT / "data"
TABLE = ROOT / "source" / "WorldTable.inc"
SOURCES_MD = DATA / "SOURCES.md"
DOWNLOAD_DATE = "2026-10-27"
UA = "isohypse-make-worlds/1.0 (python-urllib)"
USGS = "https://planetarymaps.usgs.gov/mosaic/"
TERRARIUM = "https://s3.amazonaws.com/elevation-tiles-prod/terrarium/{z}/{x}/{y}.png"
JOERD_ATTRIBUTION = "https://raw.githubusercontent.com/tilezen/joerd/master/docs/attribution.md"

# Heights on Ceres and Vesta are stored as radii (or radius minus a sphere), which on
# these squashed bodies is mostly the flattening: contours would be latitude bands.
# Isohypse uses height above the Dawn team's biaxial reference ellipsoids instead
# (equatorial a, polar c, metres). Set to None to keep height above the sphere.
CERES_ELLIPSOID = (482000.0, 446000.0)   # DLR HAMO maps: 482 km x 446 km
VESTA_ELLIPSOID = (285000.0, 229000.0)   # Jaumann et al. 2012: 285 x 285 x 229 km

# Magellan's altimetry has gaps: thin orbit-track slivers and a few wide swaths. Slivers
# narrower than about 2r+1 output pixels are filled by smooth (Laplace) interpolation
# from their edges, so contours run across them; wider gaps stay no data. 0 = no filling.
VENUS_FILL_GAPS_PX = 4


def log(*a):
    print(*a, file=sys.stderr, flush=True)


# --------------------------------------------------------------------------- downloads

class Cache:
    """Download cache with a sha256 manifest, so checksums are computed once."""

    def __init__(self, root, offline=False):
        self.root = Path(root)
        self.root.mkdir(parents=True, exist_ok=True)
        self.offline = offline
        self.manifest_path = self.root / "manifest.json"
        try:
            self.manifest = json.loads(self.manifest_path.read_text())
        except (OSError, ValueError):
            self.manifest = {}
        self.lock = threading.Lock()

    def save(self):
        with self.lock:
            tmp = self.manifest_path.with_suffix(".tmp")
            tmp.write_text(json.dumps(self.manifest, indent=1, sort_keys=True))
            tmp.replace(self.manifest_path)

    def digest(self, path, algo="sha256"):
        path = Path(path)
        st = path.stat()
        key = str(path.relative_to(self.root))
        with self.lock:
            ent = self.manifest.get(key, {})
            if ent.get("size") == st.st_size and ent.get("mtime") == int(st.st_mtime) and algo in ent:
                return ent[algo]
        h = hashlib.new(algo)
        with open(path, "rb") as f:
            while b := f.read(1 << 22):
                h.update(b)
        with self.lock:
            ent = self.manifest.setdefault(key, {})
            if ent.get("size") != st.st_size or ent.get("mtime") != int(st.st_mtime):
                ent.clear()
            ent.update(size=st.st_size, mtime=int(st.st_mtime))
            ent[algo] = h.hexdigest()
        return h.hexdigest()

    def note(self, path, **kv):
        with self.lock:
            self.manifest.setdefault(str(Path(path).relative_to(self.root)), {}).update(kv)

    def fetch(self, url, rel, md5_url=None, quiet=False):
        """Download url to <cache>/rel unless it is already there; verify md5 if given."""
        dest = self.root / rel
        if dest.exists():
            if md5_url:
                want = self.text(md5_url, rel + ".md5").split()[0].lower()
                if self.digest(dest, "md5") != want:
                    raise SystemExit(f"md5 mismatch for cached {dest}; delete it to fetch again")
            return dest
        if self.offline:
            raise SystemExit(f"--offline and {dest} is missing")
        dest.parent.mkdir(parents=True, exist_ok=True)
        part = dest.with_name(dest.name + ".part")
        for attempt in range(6):
            have = part.stat().st_size if part.exists() else 0
            hdr = {"User-Agent": UA}
            if have:
                hdr["Range"] = f"bytes={have}-"
            try:
                req = urllib.request.Request(url, headers=hdr)
                with urllib.request.urlopen(req, timeout=120) as r:
                    mode = "ab" if (have and r.status == 206) else "wb"
                    with open(part, mode) as f:
                        while b := r.read(1 << 20):
                            f.write(b)
                break
            except urllib.error.HTTPError as e:
                if e.code == 416 and have:      # already complete
                    break
                if e.code in (403, 404):
                    raise SystemExit(f"{url}: HTTP {e.code}")
                log(f"  retry {url}: {e}")
            except Exception as e:  # noqa: BLE001 -- network errors of every flavour
                log(f"  retry {url}: {e}")
            time.sleep(3 * (attempt + 1))
        else:
            raise SystemExit(f"download failed: {url}")
        if md5_url:
            want = self.text(md5_url, rel + ".md5").split()[0].lower()
            md5 = hashlib.md5()
            with open(part, "rb") as f:
                while b := f.read(1 << 22):
                    md5.update(b)
            got = md5.hexdigest()
            if got != want:
                part.unlink()
                raise SystemExit(f"md5 mismatch for {url}: {got} != {want}")
        part.replace(dest)
        if not quiet:
            log(f"  fetched {rel} ({dest.stat().st_size:,} bytes)")
        return dest

    def text(self, url, rel):
        return self.fetch(url, rel, quiet=True).read_text(errors="replace")


def resolve(url):
    """Follow redirects with a HEAD request; return (final URL, Content-Length)."""
    req = urllib.request.Request(url, method="HEAD", headers={"User-Agent": UA})
    with urllib.request.urlopen(req, timeout=60) as r:
        return r.geturl(), int(r.headers["Content-Length"])


_tls = threading.local()


def range_get(url, start, length):
    """One HTTP range request on a per-thread keep-alive connection."""
    u = urllib.parse.urlsplit(url)
    for attempt in range(8):
        conn = getattr(_tls, "conn", None)
        if conn is None or getattr(_tls, "host", None) != u.netloc:
            conn = _tls.conn = http.client.HTTPSConnection(u.netloc, timeout=120)
            _tls.host = u.netloc
        try:
            conn.request("GET", u.path, headers={
                "Range": f"bytes={start}-{start + length - 1}", "User-Agent": UA})
            r = conn.getresponse()
            data = r.read()
            if r.status != 206 or len(data) != length:
                raise OSError(f"HTTP {r.status}, {len(data)} of {length} bytes")
            return data
        except Exception as e:  # noqa: BLE001
            conn.close()
            _tls.conn = None
            log(f"  range retry {start}: {e}")
            time.sleep(2 * (attempt + 1))
    raise SystemExit(f"range request failed: {url} @ {start}")


# --------------------------------------------------------------------------- resampling

class Grid:
    """An equirectangular, pixel-is-area source grid, in degrees.

    lon0/lat0 are the west/north edges of pixel (0, 0); dlon/dlat are positive.
    """

    def __init__(self, lon0, dlon, ncols, lat0, dlat, nrows):
        self.lon0, self.dlon, self.ncols = lon0, dlon, ncols
        self.lat0, self.dlat, self.nrows = lat0, dlat, nrows

    def row_lat(self, rows):
        return self.lat0 - (np.asarray(rows, dtype=np.float64) + 0.5) * self.dlat

    def describe(self):
        return (f"{self.ncols}x{self.nrows}, lon {self.lon0:.4f}..{self.lon0 + self.ncols * self.dlon:.4f}, "
                f"lat {self.lat0:.4f}..{self.lat0 - self.nrows * self.dlat:.4f}, "
                f"{1 / self.dlon:.4f} px/deg")


def grid_from_raster(ds):
    t = ds.transform
    assert t.b == 0 and t.d == 0, "rotated raster"
    assert ds.tags().get("AREA_OR_POINT", "Area") == "Area", "pixel-is-point raster"
    if ds.crs.is_geographic:
        k, lon_0 = 1.0, 0.0
    else:
        p = ds.crs.to_dict()
        assert p.get("proj") == "eqc", p
        assert abs(p.get("lat_ts", 0.0)) < 1e-9 and abs(p.get("lat_0", 0.0)) < 1e-9, p
        radius = p.get("R") or p.get("a")
        k, lon_0 = 180.0 / (math.pi * radius), p.get("lon_0", 0.0)
    return Grid(lon_0 + t.c * k, t.a * k, ds.width, t.f * k, -t.e * k, ds.height)


def box_sum(a, edges):
    """Integral of the piecewise-constant a (last axis) between consecutive edges.

    edges are fractional pixel positions (0 = left edge of pixel 0, n = right edge of
    pixel n-1). Exact for a source of constant-valued pixels.
    """
    n = a.shape[-1]
    c = np.zeros(a.shape[:-1] + (n + 1,))
    np.cumsum(a, axis=-1, out=c[..., 1:])
    i = np.minimum(np.floor(edges).astype(np.int64), n - 1)
    f = edges - i
    at = c[..., i] + f * a[..., i]
    return np.diff(at, axis=-1)


def x_edges(grid, width):
    period = 360.0 / grid.dlon
    if abs(grid.ncols - period) > 2:
        raise ValueError(f"source spans {grid.ncols * grid.dlon:.3f} deg of longitude, not 360")
    start = ((-180.0 - grid.lon0) / grid.dlon) % period
    return start + np.arange(width + 1) * (period / width)


def y_edges(grid, height):
    lat = 90.0 - np.arange(height + 1) * (180.0 / height)
    return np.clip((grid.lat0 - lat) / grid.dlat, 0.0, grid.nrows)


class Reducer:
    """Accumulates x-reduced rows, then reduces in y. Handles nodata and missing rows."""

    def __init__(self, grid, width, height):
        self.g, self.w, self.h = grid, width, height
        self.sx = x_edges(grid, width)
        self.vx = np.zeros((grid.nrows, width))
        self.mx = np.zeros((grid.nrows, width))
        self.have = np.zeros(grid.nrows, dtype=bool)

    def add(self, rows, chunk):
        """chunk: (n, ncols) float64 metres, NaN for nodata, for source rows `rows`
        (a start row, or an index array for rows that are not contiguous)."""
        valid = np.isfinite(chunk)
        val = np.where(valid, chunk, 0.0)
        wrap = lambda a: np.concatenate([a, a, a[:, :8]], axis=1)   # noqa: E731
        r = rows if isinstance(rows, np.ndarray) else slice(rows, rows + chunk.shape[0])
        self.vx[r] = box_sum(wrap(val), self.sx)
        self.mx[r] = box_sum(wrap(valid.astype(np.float64)), self.sx)
        self.have[r] = True

    def finish(self, min_cover=0.5):
        ty = y_edges(self.g, self.h)
        widths = np.diff(self.sx)
        avail = self.have[:, None] * widths[None, :]
        v = box_sum(self.vx.T, ty).T
        m = box_sum(self.mx.T, ty).T
        a = box_sum(avail.T, ty).T
        out = np.full((self.h, self.w), np.nan)
        ok = (a > 0) & (m >= min_cover * a)
        out[ok] = v[ok] / m[ok]
        return out


def _shift(m, dy, dx, fill):
    """Shift a mask; longitude (x) wraps, latitude (y) does not (outside = fill)."""
    out = np.roll(m, dx, axis=1)
    if dy > 0:
        out = np.concatenate([np.full((dy, m.shape[1]), fill), out[:-dy]], axis=0)
    elif dy < 0:
        out = np.concatenate([out[-dy:], np.full((-dy, m.shape[1]), fill)], axis=0)
    return out


def fill_narrow_gaps(h, radius):
    """Fill no-data gaps narrower than about 2*radius+1 pixels by harmonic (Laplace)
    interpolation from their edges. Wider gaps stay as no data. Returns (h, filled count)."""
    gap = ~np.isfinite(h)
    disk = [(dy, dx) for dy in range(-radius, radius + 1) for dx in range(-radius, radius + 1)
            if dy * dy + dx * dx <= radius * radius]
    core = gap.copy()
    for dy, dx in disk:                     # erosion of the gap mask
        core &= _shift(gap, dy, dx, True)
    wide = np.zeros_like(gap)
    for dy, dx in disk:                     # dilation back: the opening = wide gaps
        wide |= _shift(core, dy, dx, False)
    wide &= gap
    todo = gap & ~wide
    if not todo.any():
        return h, 0
    z = np.where(np.isfinite(h), h, 0.0)
    known = ~gap
    # first guess: grow inwards with the mean of known neighbours
    cur = known.copy()
    while (todo & ~cur).any():
        acc = np.zeros_like(z)
        cnt = np.zeros_like(z)
        for dy, dx in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            k = _shift(cur, dy, dx, False)
            acc += np.where(k, _shift_val(z, dy, dx), 0.0)
            cnt += k
        new = todo & ~cur & (cnt > 0)
        if not new.any():
            break
        z[new] = acc[new] / cnt[new]
        cur |= new
    # then relax: Jacobi iterations of the Laplace equation over the filled pixels,
    # with known pixels fixed and wide gaps / outside treated as absent
    usable = known | todo
    for _ in range(400):
        acc = np.zeros_like(z)
        cnt = np.zeros_like(z)
        for dy, dx in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            k = _shift(usable, dy, dx, False)
            acc += np.where(k, _shift_val(z, dy, dx), 0.0)
            cnt += k
        upd = todo & (cnt > 0)
        z[upd] = acc[upd] / cnt[upd]
    out = h.copy()
    out[todo] = z[todo]
    return out, int(todo.sum())


def _shift_val(z, dy, dx):
    out = np.roll(z, dx, axis=1)
    if dy > 0:
        out = np.concatenate([np.zeros((dy, z.shape[1])), out[:-dy]], axis=0)
    elif dy < 0:
        out = np.concatenate([out[-dy:], np.zeros((-dy, z.shape[1]))], axis=0)
    return out


def ellipsoid_radius(lat_deg, a, c):
    phi = np.radians(lat_deg)
    return a * c / np.sqrt((c * np.cos(phi)) ** 2 + (a * np.sin(phi)) ** 2)


# --------------------------------------------------------------------------- globe readers

def raster_rows(path, convert, rows=256):
    """Yield (row0, float64 metres with NaN) chunks from a local GeoTIFF."""
    with rasterio.open(path) as ds:
        nd = ds.nodata
        for r0 in range(0, ds.height, rows):
            n = min(rows, ds.height - r0)
            a = ds.read(1, window=Window(0, r0, ds.width, n)).astype(np.float64)
            bad = ~np.isfinite(a) | (np.abs(a) > 1e30)
            if nd is not None:
                bad |= a == nd
            a[bad] = np.nan
            yield r0, convert(a, r0)


def globe_from_raster(cache, src, w, h, convert=lambda a, r0: a):
    path = src["path"]
    with rasterio.open(path) as ds:
        grid = grid_from_raster(ds)
    log(f"  source grid {grid.describe()}")
    red = Reducer(grid, w, h)
    for r0, chunk in raster_rows(path, lambda a, r0: convert(a, r0, grid)):
        red.add(r0, chunk)
    return red.finish()


def usgs_source(cache, name, folder):
    rel = f"{folder}/{name}"
    path = cache.fetch(USGS + name, rel, md5_url=USGS + name + ".md5")
    lbl = name.rsplit(".", 1)[0] + ".lbl"
    cache.fetch(USGS + lbl, f"{folder}/{lbl}", quiet=True)
    return dict(url=USGS + name, path=path, files=[path])


def read_earth(cache, w, h):
    url = ("https://www.ngdc.noaa.gov/mgg/global/relief/ETOPO2022/data/60s/"
           "60s_surface_elev_gtif/ETOPO_2022_v1_60s_N90W180_surface.tif")
    src = dict(url=url, path=cache.fetch(url, "earth/ETOPO_2022_v1_60s_N90W180_surface.tif"))
    src["files"] = [src["path"]]
    return src, globe_from_raster(cache, src, w, h, lambda a, r0, g: a)


def read_moon(cache, w, h):
    base = "https://imbrium.mit.edu/DATA/LOLA_GDR/CYLINDRICAL/IMG/"
    lbl = cache.fetch(base + "LDEM_16.LBL", "moon/LDEM_16.LBL")
    img = cache.fetch(base + "LDEM_16.IMG", "moon/LDEM_16.IMG")
    label = lbl.read_text(errors="replace")

    def num(key):
        return float(re.search(rf"^\s*{key}\s*=\s*([-\d.]+)", label, re.M).group(1))

    lines, samples, res = int(num("LINES")), int(num("LINE_SAMPLES")), num("MAP_RESOLUTION")
    scale = num("SCALING_FACTOR")
    assert "LSB_INTEGER" in label and int(num("SAMPLE_BITS")) == 16
    # PDS simple cylindrical, pixel registered: 0-based pixel k has its centre at
    # lat = (LINE_PROJECTION_OFFSET - k) / res, lon = CENTER_LONGITUDE + (k - SAMPLE_PROJECTION_OFFSET) / res
    lat0 = (num("LINE_PROJECTION_OFFSET") + 0.5) / res
    lon0 = num("CENTER_LONGITUDE") - (num("SAMPLE_PROJECTION_OFFSET") + 0.5) / res
    assert abs(lat0 - 90) < 1e-9 and abs(lon0 - num("WESTERNMOST_LONGITUDE")) < 1e-9, (lat0, lon0)
    grid = Grid(lon0, 1 / res, samples, lat0, 1 / res, lines)
    log(f"  source grid {grid.describe()}")
    a = np.fromfile(img, dtype="<i2").reshape(lines, samples).astype(np.float64) * scale
    red = Reducer(grid, w, h)
    for r0 in range(0, lines, 512):
        red.add(r0, a[r0:r0 + 512])
    src = dict(url=base + "LDEM_16.IMG", path=img, files=[img, lbl])
    return src, red.finish()


def read_mars(cache, w, h):
    """MOLA 463 m mosaic (2.1 GB): range-read rows 4k and 4k+1 only (half the file)."""
    name = "Mars_MGS_MOLA_DEM_mosaic_global_463m.tif"
    url = USGS + name
    cache.fetch(USGS + name.replace(".tif", ".lbl"), "mars/" + name.replace(".tif", ".lbl"), quiet=True)
    md5_full = cache.text(url + ".md5", "mars/" + name + ".md5").split()[0]
    raw = cache.root / "mars" / (name.replace(".tif", "") + ".rows-4k-4k1.i16")
    meta_path = raw.with_suffix(".json")
    if raw.exists():
        meta = json.loads(meta_path.read_text())
    else:
        if cache.offline:
            raise SystemExit(f"--offline and {raw} is missing")
        final, size = resolve(url)
        with rasterio.Env(GDAL_DISABLE_READDIR_ON_OPEN="EMPTY_DIR"):
            with rasterio.open("/vsicurl/" + final) as ds:
                grid = grid_from_raster(ds)
                assert ds.dtypes[0] == "int16" and ds.block_shapes[0] == (1, ds.width)
                assert ds.profile.get("compress") is None
                rb = ds.width * 2
                off = lambda r: int(ds.get_tag_item(f"BLOCK_OFFSET_0_{r}", "TIFF", bidx=1))  # noqa: E731
                o0 = off(0)
                for r in (1, 2, 3, 777, 12345, ds.height - 1):
                    assert off(r) == o0 + r * rb, "strips are not contiguous"
                check = ds.read(1, window=Window(0, 0, ds.width, 2))
                nrows, ncols, nd = ds.height, ds.width, ds.nodata
        pairs = nrows // 4
        part = raw.with_name(raw.name + ".part")
        with open(part, "wb") as f:
            f.truncate(pairs * 2 * rb)
        fd = os.open(part, os.O_WRONLY)
        done = [0]

        def job(k):
            data = range_get(final, o0 + 4 * k * rb, 2 * rb)
            os.pwrite(fd, data, k * 2 * rb)
            done[0] += 1
            if done[0] % 500 == 0:
                log(f"  mars rows: {done[0]}/{pairs} pairs")

        log(f"  range-reading {pairs * 2} of {nrows} rows ({pairs * 2 * rb:,} of {size:,} bytes)")
        with cf.ThreadPoolExecutor(16) as ex:
            list(ex.map(job, range(pairs)))
        os.close(fd)
        first = np.fromfile(part, dtype="<i2", count=2 * ncols).reshape(2, -1)
        assert np.array_equal(first, check), "range-read rows differ from GDAL's read"
        part.replace(raw)
        meta = dict(url=final, full_size=size, nrows=nrows, ncols=rb // 2, nodata=nd,
                    grid=[grid.lon0, grid.dlon, grid.ncols, grid.lat0, grid.dlat, grid.nrows])
        meta_path.write_text(json.dumps(meta))
    grid = Grid(*meta["grid"])
    log(f"  source grid {grid.describe()} (rows 4k, 4k+1 only)")
    ncols, nrows = meta["ncols"], meta["nrows"]
    a = np.memmap(raw, dtype="<i2", mode="r").reshape(-1, ncols)
    red = Reducer(grid, w, h)
    for i0 in range(0, a.shape[0], 256):
        chunk = a[i0:i0 + 256].astype(np.float64)
        if meta["nodata"] is not None:
            chunk[chunk == meta["nodata"]] = np.nan
        i = np.arange(i0, i0 + chunk.shape[0])
        red.add(4 * (i // 2) + i % 2, chunk)            # stored row i is source row 4(i//2) + i%2
    src = dict(url=url, path=raw, files=[raw], md5_full=md5_full, full_size=meta["full_size"])
    return src, red.finish()


def read_usgs(name, folder, convert, fill_gaps=0):
    def reader(cache, w, h):
        src = usgs_source(cache, name, folder)
        hm = globe_from_raster(cache, src, w, h, convert)
        if fill_gaps:
            before = float(np.mean(~np.isfinite(hm)))
            hm, n = fill_narrow_gaps(hm, fill_gaps)
            src["filled"] = dict(radius=fill_gaps, pixels=n, before=before,
                                 after=float(np.mean(~np.isfinite(hm))))
            log(f"  filled {n} pixels of narrow gaps; no data {before:.2%} -> {src['filled']['after']:.2%}")
        return src, hm
    return reader


def ceres_convert(a, r0, g):
    r = a + 470000.0                       # DN is radius - 470 km (label Base = 470000)
    if CERES_ELLIPSOID is None:
        return a
    lat = g.row_lat(np.arange(r0, r0 + a.shape[0]))[:, None]
    return r - ellipsoid_radius(lat, *CERES_ELLIPSOID)


def vesta_convert(a, r0, g):
    if VESTA_ELLIPSOID is None:
        return a - 255000.0                # values are radii
    lat = g.row_lat(np.arange(r0, r0 + a.shape[0]))[:, None]
    return a - ellipsoid_radius(lat, *VESTA_ELLIPSOID)


GLOBES = [
    # id, display name, file, width, height, hasSea, reader
    ("Earth", "Earth", "earth.png", 4096, 2048, 1, read_earth),
    ("Moon", "Moon", "moon.png", 2048, 1024, 0, read_moon),
    ("Mars", "Mars", "mars.png", 2048, 1024, 0, read_mars),
    ("Mercury", "Mercury", "mercury.png", 2048, 1024, 0,
     read_usgs("Mercury_Messenger_USGS_DEM_Global_665m_v2.tif", "mercury", lambda a, r0, g: a * 0.5)),
    ("Venus", "Venus", "venus.png", 2048, 1024, 0,
     read_usgs("Venus_Magellan_Topography_Global_4641m_v02.tif", "venus", lambda a, r0, g: a,
               fill_gaps=VENUS_FILL_GAPS_PX)),
    ("Ceres", "Ceres", "ceres.png", 2048, 1024, 0,
     read_usgs("Ceres_Dawn_FC_HAMO_DTM_DLR_Global_60ppd_Oct2016.tif", "ceres", ceres_convert)),
    ("Vesta", "Vesta", "vesta.png", 2048, 1024, 0,
     read_usgs("Vesta_Dawn_HAMO_DTM_DLR_Global_48ppd.tif", "vesta", vesta_convert)),
    ("Pluto", "Pluto", "pluto.png", 2048, 1024, 0,
     read_usgs("Pluto_NewHorizons_Global_DEM_300m_Jul2017_16bit.tif", "pluto", lambda a, r0, g: a)),
    ("Charon", "Charon", "charon.png", 2048, 1024, 0,
     read_usgs("Charon_NewHorizons_Global_DEM_300m_Jul2017_16bit.tif", "charon", lambda a, r0, g: a)),
]

# --------------------------------------------------------------------------- places

PLACES = [
    # display name, lat, lon, z
    ("London", 51.5072, -0.1276, 11),
    ("Edinburgh", 55.9533, -3.1883, 12),
    ("Snowdon", 53.0685, -4.0763, 12),
    ("New York", 40.7128, -74.0060, 11),
    ("San Francisco", 37.7749, -122.4194, 11),
    ("Los Angeles", 34.0522, -118.2437, 10),
    ("Vancouver", 49.2827, -123.1207, 10),
    ("Mount Rainier", 46.8523, -121.7603, 11),
    ("Grand Canyon", 36.1069, -112.1129, 11),
    ("Crater Lake", 42.9446, -122.1090, 12),
    ("Rio de Janeiro", -22.9068, -43.1729, 11),
    ("Cape Town", -33.9249, 18.4241, 11),
    ("Lisbon", 38.7223, -9.1393, 12),
    ("Naples & Vesuvius", 40.8300, 14.3500, 11),
    ("Matterhorn", 45.9763, 7.6586, 12),
    ("Everest", 27.9881, 86.9250, 11),
    ("Kilimanjaro", -3.0674, 37.3556, 11),
    ("Mount Fuji", 35.3606, 138.7274, 11),
    ("Hong Kong", 22.3193, 114.1694, 11),
    ("Wellington", -41.2865, 174.7762, 11),
    ("Sydney", -33.8688, 151.2093, 10),
    ("Hawaii (Big Island)", 19.6, -155.5, 8),
]

PLACE_SIZE = 512


def place_id(name):
    return "".join(w[:1].upper() + w[1:] for w in re.findall(r"[A-Za-z0-9]+", name))


def place_file(name):
    return "-".join(re.findall(r"[a-z0-9]+", name.lower())) + ".png"


def mercator_px(lat, lon, zoom):
    n = 256 * 2 ** zoom
    x = (lon + 180.0) / 360.0 * n
    y = (1.0 - math.asinh(math.tan(math.radians(lat))) / math.pi) / 2.0 * n
    return x, y


def _grow(m, r):
    g = m.copy()
    for _ in range(r):
        n = g.copy()
        n[1:] |= g[:-1]
        n[:-1] |= g[1:]
        n[:, 1:] |= g[:, :-1]
        n[:, :-1] |= g[:, 1:]
        g = n
    return g


def _median7(h):
    """7 x 7 median and median absolute deviation (edges replicated)."""
    H, W = h.shape
    p = np.pad(h.astype(np.float32), 3, mode="edge")
    st = np.stack([p[dy:dy + H, dx:dx + W] for dy in range(7) for dx in range(7)])
    med = np.median(st, axis=0)
    mad = np.median(np.abs(st - med), axis=0)
    return med.astype(np.float64), mad.astype(np.float64)


def despike(h):
    """Remove the Terrarium tiles' resampling artefacts: small clusters of wildly wrong
    values (hundreds to thousands of metres off, ringing in both directions), mostly
    along coastlines. Pass 1 replaces pixels more than max(150 m, 10 x MAD) from their
    7 x 7 median (plus a 1-pixel halo) with that median; pass 2 mops up the ringing
    within 4 pixels of those at max(30 m, 4 x MAD). Mountain tiles come through
    untouched (checked: Everest, Matterhorn, Kilimanjaro, Grand Canyon, Rainier)."""
    h = h.copy()
    hit = np.zeros(h.shape, dtype=bool)
    flagged = 0
    for _ in range(4):
        med, mad = _median7(h)
        bad = np.abs(h - med) > np.maximum(150.0, 10.0 * mad)
        if not bad.any():
            break
        g = _grow(bad, 1)
        flagged += int(bad.sum())
        hit |= g
        h[g] = med[g]
    for _ in range(3):
        if not hit.any():
            break
        med, mad = _median7(h)
        bad = _grow(hit, 4) & (np.abs(h - med) > np.maximum(30.0, 4.0 * mad))
        if not bad.any():
            break
        g = _grow(bad, 1)
        flagged += int(bad.sum())
        hit |= g
        h[g] = med[g]
    return h, hit


def build_place(cache, name, lat, lon, z):
    zf = z + 1
    x, y = mercator_px(lat, lon, zf)
    # window origin on an even zoom-(z+1) pixel so 2x2 blocks are zoom-z pixels
    x0 = 2 * round(x / 2 - PLACE_SIZE / 2)
    y0 = 2 * round(y / 2 - PLACE_SIZE / 2)
    win = 2 * PLACE_SIZE
    margin = 8                      # so the despike filter has real neighbours at the crop edge
    tx = range((x0 - margin) // 256, (x0 + win + margin - 1) // 256 + 1)
    ty = range((y0 - margin) // 256, (y0 + win + margin - 1) // 256 + 1)
    ntiles = 2 ** zf
    jobs = [(zf, i % ntiles, j) for j in ty for i in tx]

    def get(t):
        zz, i, j = t
        return cache.fetch(TERRARIUM.format(z=zz, x=i, y=j), f"terrarium/{zz}/{i}/{j}.png", quiet=True)

    with cf.ThreadPoolExecutor(16) as ex:
        paths = list(ex.map(get, jobs))
    mosaic = np.zeros((len(ty) * 256, len(tx) * 256))
    sha = hashlib.sha256()
    for n, p in enumerate(paths):
        sha.update(p.read_bytes())
        with Image.open(p) as im:
            assert im.size == (256, 256), p
            rgb = np.asarray(im.convert("RGB"), dtype=np.float64)
        hgt = rgb[..., 0] * 256.0 + rgb[..., 1] + rgb[..., 2] / 256.0 - 32768.0
        r, c = (n // len(tx)) * 256, (n % len(tx)) * 256
        mosaic[r:r + 256, c:c + 256] = hgt
    ox, oy = x0 - tx.start * 256, y0 - ty.start * 256
    clean, hit = despike(mosaic)
    crop = clean[oy:oy + win, ox:ox + win]
    fixed = int(hit[oy:oy + win, ox:ox + win].sum())
    raw_lo, raw_hi = float(mosaic[oy:oy + win, ox:ox + win].min()), float(mosaic[oy:oy + win, ox:ox + win].max())
    out = crop.reshape(PLACE_SIZE, 2, PLACE_SIZE, 2).mean(axis=(1, 3))
    # where the point lands in the output, in output pixels
    cx, cy = x / 2 - x0 / 2, y / 2 - y0 / 2
    info = dict(tiles=f"z{zf} x {tx.start}-{tx.stop - 1}" + (f" (mod {ntiles})" if tx.stop > ntiles else "")
                + f", y {ty.start}-{ty.stop - 1}", ntiles=len(jobs), sha256=sha.hexdigest(),
                centre_px=(cx, cy), mpp=156543.03392 * math.cos(math.radians(lat)) / 2 ** z,
                despiked=fixed, raw_range=(raw_lo, raw_hi))
    return out, info


def open_sea(h):
    """Fraction of the tile at or below sea level (h < 0.5 m) and connected to the tile
    edge (4-neighbour), plus the fraction with real bathymetry (h < -1 m). Terrarium has
    sea-floor depths in some places and a flat 0 m sea in others (most coastal tiles at
    these zooms), so both count as sea; rivers below 0 (the Thames) stay under 2%."""
    wet = h < 0.5
    reach = np.zeros_like(wet)
    reach[0, :], reach[-1, :], reach[:, 0], reach[:, -1] = wet[0, :], wet[-1, :], wet[:, 0], wet[:, -1]
    while True:
        grow = reach.copy()
        grow[1:, :] |= reach[:-1, :]
        grow[:-1, :] |= reach[1:, :]
        grow[:, 1:] |= reach[:, :-1]
        grow[:, :-1] |= reach[:, 1:]
        grow &= wet
        if np.array_equal(grow, reach):
            return reach.mean(), float(np.mean(reach & (h < -1.0)))
        reach = grow


# --------------------------------------------------------------------------- encoding

def encode(h):
    valid = np.isfinite(h)
    if not valid.any():
        raise ValueError("no data")
    lo = round(math.floor(float(np.nanmin(h)) * 10) / 10, 1)
    hi = round(math.ceil(float(np.nanmax(h)) * 10) / 10, 1)
    if hi <= lo:
        hi = lo + 0.1
    has_nd = not valid.all()
    t = (np.where(valid, h, lo) - lo) / (hi - lo)
    if has_nd:
        v = 1 + np.rint(t * 65534)
        v[~valid] = 0
    else:
        v = np.rint(t * 65535)
    return np.clip(v, 0, 65535).astype(np.uint16), lo, hi, has_nd


def decode(v, lo, hi, has_nd):
    v = v.astype(np.float64)
    if has_nd:
        h = lo + (v - 1) / 65534 * (hi - lo)
        h[v == 0] = np.nan
        return h
    return lo + v / 65535 * (hi - lo)


def write_png16(path, v):
    Image.fromarray(v).save(path, compress_level=9)
    with Image.open(path) as im:
        back = np.asarray(im)
        assert im.mode in ("I;16", "I;16B", "I") and back.dtype != np.uint8, im.mode
        assert np.array_equal(back.astype(np.uint16), v), f"{path} does not round-trip"


def read_png16(path):
    with Image.open(path) as im:
        return np.asarray(im).astype(np.uint16)


# --------------------------------------------------------------------------- previews

def hillshade(h, exaggerate=1.0):
    valid = np.isfinite(h)
    z = np.where(valid, h, np.nanmean(h) if valid.any() else 0.0)
    gy, gx = np.gradient(z)
    g = np.hypot(gx, gy)
    s = np.percentile(g[valid], 98) if valid.any() else 1.0
    s = s if s > 0 else 1.0
    gx, gy = gx / s * 1.5 * exaggerate, gy / s * 1.5 * exaggerate
    norm = np.sqrt(gx * gx + gy * gy + 1.0)
    lx, ly, lz = -0.5, -0.5, math.sqrt(0.5)          # light from the north-west, 45 deg up
    shade = np.clip((-gx * lx - gy * ly + lz) / norm, 0, 1)
    lo, hi = np.nanmin(h), np.nanmax(h)
    tint = (z - lo) / ((hi - lo) or 1.0)
    img = (0.7 * shade + 0.3 * tint) * 255
    img[~valid] = 24
    return Image.fromarray(np.clip(img, 0, 255).astype(np.uint8), "L")


def contact_sheet(items, path):
    """items: list of (label, PIL L image)."""
    cell_w, pad, lab = 300, 8, 14
    rows, x, y, row_h = [], 0, 0, 0
    layout = []
    for label, im in items:
        thumb = im.copy()
        thumb.thumbnail((cell_w * (2 if im.width > im.height else 1), cell_w))
        if x + thumb.width > 1900 and x > 0:
            x, y, row_h = 0, y + row_h + pad + lab, 0
        layout.append((label, thumb, x, y))
        x += thumb.width + pad
        row_h = max(row_h, thumb.height)
    W = max(lx + t.width for _, t, lx, _ in layout)
    H = y + row_h + lab + pad
    sheet = Image.new("L", (W, H), 255)
    d = ImageDraw.Draw(sheet)
    for label, thumb, lx, ly in layout:
        sheet.paste(thumb, (lx, ly + lab))
        d.text((lx + 2, ly + 1), label, fill=0)
    sheet.save(path)


# --------------------------------------------------------------------------- checks

GLOBE_CHECKS = {
    "Earth": [("Everest", 27.988, 86.925, "among the highest"), ("Mariana Trench", 11.35, 142.2, "deeply negative"),
              ("Mid-Atlantic", 0.0, -30.0, "-4000 to -5000"), ("London", 51.5, -0.1, "small positive")],
    "Mars": [("Olympus Mons", 18.65, -133.8, "~ +21 km"), ("Hellas floor", -42.4, 70.5, "~ -7 km")],
    "Moon": [("Highest region", 5.4, -158.6, "~ +10 km"), ("South Pole-Aitken", -53.0, -169.0, "deeply negative")],
    "Venus": [("Maxwell Montes", 65.2, 3.3, "highest")],
}
PLACE_EXPECT = {
    "Everest": "max ~8000+", "Matterhorn": "max ~4400", "Mount Fuji": "max ~3700",
    "Cape Town": "Table Mountain ~1000", "London": "0-250", "Mount Rainier": "max ~4300",
    "Kilimanjaro": "max ~5800", "Snowdon": "max ~1085", "Crater Lake": "lake ~1880, rim ~2400",
    "Grand Canyon": "rim ~2100-2500, river ~750", "Hawaii (Big Island)": "max ~4200, sea floor -5000",
}


def globe_value(h, lat, lon):
    H, W = h.shape
    i = int(math.floor((lon + 180.0) / 360.0 * W)) % W
    j = min(H - 1, max(0, int(math.floor((90.0 - lat) / 180.0 * H))))
    return h[j, i]


def where_max(h):
    j, i = np.unravel_index(np.nanargmax(h), h.shape)
    H, W = h.shape
    return 90.0 - (j + 0.5) * 180.0 / H, -180.0 + (i + 0.5) * 360.0 / W


# --------------------------------------------------------------------------- SOURCES.md

def sources_md(cache, globe_info, place_info):
    def sha(p):
        return cache.digest(p)

    def files_block(src):
        lines = []
        for p in src["files"]:
            lines.append(f"  - `{Path(p).name}` sha256 `{sha(p)}` ({Path(p).stat().st_size:,} bytes)")
        return "\n".join(lines)

    g = {k: v for k, v in globe_info.items()}
    out = [
        "# Isohypse data sources",
        "",
        "Generated by `tools/make-worlds.py` -- do not edit by hand; edit the script and rerun.",
        f"Everything was downloaded on {DOWNLOAD_DATE}.",
        "",
        "All heightmaps are 16-bit greyscale PNGs, linear in metres between the `minMetres` and",
        "`maxMetres` given in `source/WorldTable.inc` (v = 0 is no data where `hasNodata` is 1).",
        "Globes are equirectangular, pixel-is-area, north up, longitude -180 at the left edge,",
        "east-positive, area-averaged down from the source with an exact box filter. Places are",
        "512 x 512 Web Mercator crops, one zoom-z pixel per pixel.",
        "",
        "## Credits to show in the plugin",
        "",
        "Every body except the places is public domain (NASA, USGS, NOAA) or released by NASA's",
        "Planetary Data System without restriction; the agencies ask for credit. The places use",
        "the Terrarium tiles, whose sources include CC BY and Open Government Licence data that",
        "*require* the attribution block in the Places section to ship with the plugin.",
        "",
    ]

    def sect(title, body):
        out.extend([f"## {title}", ""] + body + [""])

    if "Earth" in g:
        s = g["Earth"]
        sect("Earth", [
            "- Product: NOAA ETOPO 2022 Global Relief Model, version 1, 60 arc-second, ice surface",
            "  (`ETOPO_2022_v1_60s_N90W180_surface.tif`, 21600 x 10800, float32 metres, EGM2008",
            "  heights, i.e. relative to mean sea level; bathymetry included).",
            f"- URL: {s['src']['url']}",
            files_block(s["src"]),
            "- Processing: area-averaged 21600 x 10800 to 4096 x 2048 (each output pixel is the",
            "  mean of about 5.3 x 5.3 source pixels). Peaks and trenches are therefore lower and",
            "  shallower than their point values.",
            "- Licence: public domain (U.S. Government work, not subject to copyright in the US).",
            "- Credit: NOAA National Centers for Environmental Information. 2022: ETOPO 2022 15",
            "  Arc-Second Global Relief Model. NOAA National Centers for Environmental Information.",
            "  https://doi.org/10.25921/fd45-gt74",
        ])
    if "Moon" in g:
        s = g["Moon"]
        sect("Moon", [
            "- Product: LRO LOLA Gridded Data Record `LDEM_16` (V3.1, 2019; 16 px/deg, int16,",
            "  0.5 m per DN, heights relative to a 1737.4 km sphere; data set LRO-L-LOLA-4-GDR-V1.0).",
            f"- URL: {s['src']['url']} (and `.LBL`). This is the LOLA team's server at MIT, which",
            "  holds the same PDS volume as PDS Geosciences; pds-geosciences.wustl.edu refused this",
            "  machine's network (its firewall's botnet filter), so it was not used.",
            files_block(s["src"]),
            "- Processing: longitude rolled from 0..360 E to -180..180; 5760 x 2880 area-averaged",
            "  to 2048 x 1024.",
            "- Licence: public domain (NASA).",
            "- Credit: NASA/GSFC/MIT, LRO Lunar Orbiter Laser Altimeter team. Smith, D. E., et al.",
            "  (2010), The Lunar Orbiter Laser Altimeter investigation on the Lunar Reconnaissance",
            "  Orbiter mission, Space Sci. Rev. 150, 209-241.",
        ])
    if "Mars" in g:
        s = g["Mars"]
        sect("Mars", [
            "- Product: USGS Astrogeology \"Mars MGS MOLA DEM 463m\" (MOLA 128/64 px/deg merge,",
            "  int16 metres above the areoid, 46080 x 23040, -180..180 E). The 16 px/deg MEGDR at",
            "  PDS Geosciences was unreachable from this machine (see Moon).",
            f"- URL: {s['src']['url']} (full file {s['src']['full_size']:,} bytes, USGS md5",
            f"  `{s['src']['md5_full']}`).",
            "- Only rows 4k and 4k+1 (k = 0..5759) were downloaded, by HTTP range request: half the",
            "  file, to stay under the download budget. The rows as saved locally:",
            files_block(s["src"]),
            "- Processing: area-averaged to 2048 x 1024 using the rows that were read (each output",
            "  pixel averages about 22.5 columns x 11 of its 22.5 rows).",
            "- Licence: CC0 / public domain (USGS lists \"CC0 (public domain)\").",
            "- Credit: NASA/GSFC MOLA Science Team; USGS Astrogeology Science Center. Smith, D. E.,",
            "  et al. (2001), Mars Orbiter Laser Altimeter: Experiment summary after the first year",
            "  of global mapping of Mars, J. Geophys. Res. 106(E10), 23689-23722.",
        ])
    if "Mercury" in g:
        s = g["Mercury"]
        sect("Mercury", [
            "- Product: USGS \"Mercury MESSENGER Global DEM 665m (64ppd) v2\" (Oct 2016; int16,",
            "  0.5 m per DN, heights relative to a 2439.4 km sphere; 0..360 E).",
            f"- URL: {s['src']['url']} (md5 checked against the USGS `.md5`)",
            files_block(s["src"]),
            "- Processing: longitude rolled to -180..180; 23040 x 11520 area-averaged to 2048 x 1024.",
            "- Licence: public domain (USGS/NASA).",
            "- Credit: NASA/JHUAPL/Carnegie Institution of Washington/USGS. Becker, K. J., et al.",
            "  (2016), First global digital elevation model of Mercury, 47th LPSC, abstract 2959.",
        ])
    if "Venus" in g:
        s = g["Venus"]
        sect("Venus", [
            "- Product: USGS \"Venus Magellan Global Topography 4641m\" (Magellan GTDR v2, 1997;",
            "  int16 metres relative to a 6051.0 km sphere, -180..180 E). About 8% of the source",
            "  is no data (altimetry gaps); pixels with under half their area covered are left as",
            "  no data (v = 0).",
            f"- URL: {s['src']['url']} (md5 checked)",
            files_block(s["src"]),
            "- Processing: 8192 x 4096 area-averaged to 2048 x 1024, gaps excluded from the mean;",
            (f"  then gaps narrower than about {2 * s['src']['filled']['radius'] + 1} output pixels (thin orbit"
             f" slivers; {s['src']['filled']['pixels']:,} pixels) filled by Laplace interpolation from"
             f" their edges, so no data went from {s['src']['filled']['before']:.2%} to"
             f" {s['src']['filled']['after']:.2%}." if s["src"].get("filled") else "  no gap filling."),
            "- Licence: public domain (USGS lists \"public domain\"; \"Please cite authors\").",
            "- Credit: NASA/JPL Magellan Team; P. Ford and G. Pettengill (MIT); USGS Astrogeology.",
            "  Ford, P. G., & Pettengill, G. H. (1992), Venus topography and kilometer-scale slopes,",
            "  J. Geophys. Res. 97(E8), 13103-13114.",
        ])
    if "Ceres" in g:
        s = g["Ceres"]
        ell = ("height above the biaxial reference ellipsoid 482 km x 446 km used for DLR's Ceres"
               " topography maps (the file stores radius - 470 km; converted per row at the row's"
               " planetocentric latitude)" if CERES_ELLIPSOID else "radius - 470 km")
        sect("Ceres", [
            "- Product: DLR Dawn FC HAMO global DTM, 60 px/deg, via USGS (Oct 2016; int16, radius",
            "  = DN + 470000 m; IAU-approved Kait longitude system, planetocentric, 0..360 E).",
            f"- URL: {s['src']['url']} (md5 checked)",
            files_block(s["src"]),
            f"- Processing: {ell}; longitude rolled to -180..180; 21600 x 10800 area-averaged to",
            "  2048 x 1024.",
            "- Licence: no access or use constraints (NASA PDS); \"please cite authors\".",
            "- Credit: NASA/JPL-Caltech/UCLA/MPS/DLR/IDA. Preusker, F., et al. (2016), Dawn at",
            "  Ceres -- shape model and rotational state, 47th LPSC, abstract 1954.",
        ])
    if "Vesta" in g:
        s = g["Vesta"]
        ell = ("height above the 285 km x 229 km biaxial reference ellipsoid the Dawn team uses"
               " for Vesta heights (the file stores radii; converted per row)" if VESTA_ELLIPSOID
               else "radius - 255 km")
        sect("Vesta", [
            "- Product: DLR Dawn FC HAMO global DTM, 48 px/deg, via USGS (2013-12-10 edition;",
            "  float32 radii in metres, planetocentric, east-positive, -180..180).",
            "- Longitude convention: USGS states this file was shifted 150 deg east (Claudia crater",
            "  at 146 E) to match the IAU-approved **Claudia Double Prime** system. Older Dawn-team",
            "  maps in the original \"Claudia\" system put Claudia at about 356 E, 150 deg away.",
            f"- URL: {s['src']['url']} (md5 checked)",
            files_block(s["src"]),
            f"- Processing: {ell}; 17280 x 8640 area-averaged to 2048 x 1024.",
            "- Licence: no access or use constraints (NASA PDS); \"please cite authors\".",
            "- Credit: NASA/JPL-Caltech/UCLA/MPS/DLR/IDA. Preusker, F., et al. (2014), Global shape",
            "  of (4) Vesta from Dawn FC stereo images, Vesta in the Light of Dawn, LPI Contrib. 1773.",
        ])
    for body, ref, cite in (
            ("Pluto", "1188.3 km",
             "Schenk, P. M., et al. (2018), Basins, fractures and volcanoes: Global cartography and"
             " topography of Pluto from New Horizons, Icarus 314, 400-433."),
            ("Charon", "606.0 km",
             "Schenk, P. M., et al. (2018), Breaking up is hard to do: Global cartography and"
             " topography of Pluto's mid-sized icy moon Charon from New Horizons, Icarus 315, 124-145.")):
        if body in g:
            s = g[body]
            roll = "longitude rolled from 0..360 E to -180..180; " if body == "Pluto" else ""
            sect(body, [
                f"- Product: USGS \"{body} New Horizons LORRI-MVIC Global DEM 300m\" (Jul 2017; int16",
                f"  metres relative to a {ref} sphere). Only the hemisphere New Horizons saw is",
                "  mapped: the rest is no data, kept as v = 0 (an output pixel needs half its area",
                "  covered to have a value).",
                f"- URL: {s['src']['url']} (md5 checked)",
                files_block(s["src"]),
                f"- Processing: {roll}area-averaged to 2048 x 1024, gaps excluded from the mean.",
                "- Licence: no access constraints (NASA); \"please cite authors\".",
                f"- Credit: NASA/JHUAPL/SwRI/LPI, New Horizons team. {cite}",
            ])
    if place_info:
        att = cache.text(JOERD_ATTRIBUTION, "terrarium/attribution.md")
        m = re.search(r"\*\*\*Required attribution:\*\*\*\s*```\n(.*?)```", att, re.S)
        block = m.group(1).rstrip() if m else "(could not extract; see " + JOERD_ATTRIBUTION + ")"
        rows = ["| place | tiles (count) | sha256 of the tiles concatenated, row by row | despiked px |",
                "| --- | --- | --- | --- |"]
        for name, inf in place_info.items():
            rows.append(f"| {name} | {inf['tiles']} ({inf['ntiles']}) | `{inf['sha256']}` | {inf['despiked']} |")
        sect("Places (Earth tiles)", [
            "- Product: Terrain Tiles on AWS Open Data (Mapzen/Tilezen \"joerd\"), Terrarium",
            "  encoding: h = R * 256 + G + B / 256 - 32768 metres.",
            "- URL: " + TERRARIUM,
            "- Processing: tiles at zoom z+1 mosaicked; Terrarium's resampling artefacts (small",
            "  clusters hundreds to thousands of metres off, mostly on coastlines) replaced by their",
            "  7 x 7 median (pixels more than max(150 m, 10 x MAD) off, then their ringing within 4",
            "  px at max(30 m, 4 x MAD); counts below, at zoom z+1); a 1024 x 1024 window aligned to",
            "  the zoom-z pixel grid and centred on the place cropped, then 2 x 2 box-averaged to",
            "  512 x 512.",
            "- Sea: most coastal tiles here have a flat 0 m sea (the land DEMs carry no",
            "  bathymetry); Edinburgh, San Francisco, Rio and Hawaii have real depths. `hasSea` is 1",
            "  where at least 2% of the tile is at or below 0.5 m and connected to the tile edge.",
            "",
        ] + rows + [
            "",
            "- Licence: mixed, by source. Required attribution, copied verbatim from",
            f"  {JOERD_ATTRIBUTION}:",
            "",
            "```",
            block,
            "```",
            "",
            "- Which of those apply here, judging by where each source covers (joerd's",
            "  data-sources list): USGS 3DEP for the US places (New York, San Francisco, Los",
            "  Angeles, Mount Rainier, Grand Canyon, Crater Lake, Hawaii); Environment Agency LIDAR",
            "  (Open Government Licence v3) for London; EU-DEM (Copernicus) for Lisbon, Naples,",
            "  the Matterhorn and possibly Edinburgh and Snowdon; Canada CDEM (OGL-Canada) for",
            "  Vancouver; LINZ (CC BY 3.0 NZ) for Wellington; Geoscience Australia (CC BY 4.0) for",
            "  Sydney; SRTM/GMTED elsewhere; ETOPO1 for the sea floor everywhere. Simplest and",
            "  safest: ship the whole block.",
        ])
    return "\n".join(out).rstrip() + "\n"


# --------------------------------------------------------------------------- main

def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--cache", required=True, help="download cache folder (outside the repo)")
    ap.add_argument("--previews", help="folder for 8-bit hillshade previews and a contact sheet")
    ap.add_argument("--only", help="comma-separated world ids or file stems to (re)build")
    ap.add_argument("--offline", action="store_true", help="fail rather than download")
    args = ap.parse_args()

    cache = Cache(args.cache, offline=args.offline)
    DATA.mkdir(parents=True, exist_ok=True)
    TABLE.parent.mkdir(parents=True, exist_ok=True)
    only = {s.strip().lower() for s in args.only.split(",")} if args.only else None
    state_path = cache.root / "worlds-state.json"
    try:
        state = json.loads(state_path.read_text())
    except (OSError, ValueError):
        state = {"globes": {}, "places": {}}

    def wanted(wid, fname):
        return only is None or wid.lower() in only or fname[:-4] in only

    globe_info = {}
    for wid, name, fname, w, h, sea, reader in GLOBES:
        if wanted(wid, fname):
            log(f"{name}:")
            t = time.time()
            try:
                src, hm = reader(cache, w, h)
            except SystemExit as e:
                log(f"  SKIPPED: {e}")
                state["globes"].pop(wid, None)
                continue
            v, lo, hi, nd = encode(hm)
            write_png16(DATA / fname, v)
            state["globes"][wid] = dict(name=name, file=fname, w=w, h=h, lo=lo, hi=hi, nodata=int(nd), sea=sea,
                                        src={k: (str(x) if isinstance(x, Path) else
                                                 [str(y) for y in x] if k == "files" else x)
                                             for k, x in src.items()},
                                        nodata_frac=float(np.mean(v == 0)) if nd else 0.0)
            log(f"  {fname}: {lo} .. {hi} m, nodata {nd}, {time.time() - t:.0f}s")
            cache.save()
    for wid, name, fname, *_ in GLOBES:
        if wid in state["globes"]:
            s = dict(state["globes"][wid])
            s["src"] = dict(s["src"], files=[Path(p) for p in s["src"]["files"]])
            globe_info[wid] = s

    place_info = {}
    for name, lat, lon, z in PLACES:
        pid, fname = place_id(name), place_file(name)
        if wanted(pid, fname):
            log(f"{name}: z{z}")
            hm, info = build_place(cache, name, lat, lon, z)
            v, lo, hi, nd = encode(hm)
            assert not nd
            write_png16(DATA / fname, v)
            sea_frac, deep_frac = open_sea(hm)
            info.update(name=name, file=fname, lo=lo, hi=hi, lat=lat, lon=lon, z=z,
                        sea=int(sea_frac >= 0.02), sea_frac=sea_frac, deep_frac=deep_frac)
            state["places"][name] = info
            log(f"  {fname}: {lo} .. {hi} m, sea {sea_frac:.1%} (with depths {deep_frac:.1%}), "
                f"despiked {info['despiked']} px (raw {info['raw_range'][0]:.0f} .. {info['raw_range'][1]:.0f})")
    cache.save()
    for name, *_ in PLACES:
        if name in state["places"]:
            place_info[name] = state["places"][name]
    state_path.write_text(json.dumps(state, indent=1))

    # ---- the table
    lines = [
        "// Generated by tools/make-worlds.py -- do not edit.",
        "// clang-format off",
        "// ISOHYPSE_GLOBE( id, \"Display Name\", \"file.png\", width, height, minMetres, maxMetres, hasNodata, hasSea )",
        "// ISOHYPSE_PLACE( id, \"Display Name\", \"file.png\", width, height, minMetres, maxMetres, hasSea, centreLat, centreLon, metresPerPixel )",
    ]
    for wid, *_ in GLOBES:
        if wid in globe_info:
            s = globe_info[wid]
            assert len(s["name"]) <= 20
            lines.append(f"ISOHYPSE_GLOBE( {wid}, \"{s['name']}\", \"{s['file']}\", {s['w']}, {s['h']}, "
                         f"{s['lo']:.1f}, {s['hi']:.1f}, {s['nodata']}, {s['sea']} )")
    for name, *_ in PLACES:
        if name in place_info:
            p = place_info[name]
            assert len(name) <= 20
            lines.append(f"ISOHYPSE_PLACE( {place_id(name)}, \"{name}\", \"{p['file']}\", {PLACE_SIZE}, {PLACE_SIZE}, "
                         f"{p['lo']:.1f}, {p['hi']:.1f}, {p['sea']}, {p['lat']:.4f}, {p['lon']:.4f}, {p['mpp']:.2f} )")
    TABLE.write_text("\n".join(lines) + "\n")
    SOURCES_MD.write_text(sources_md(cache, globe_info, place_info))
    cache.save()

    # ---- checks, decoded back from the PNGs as the plugin will see them
    print("\nGlobe checks (decoded from the PNGs):")
    decoded = {}
    for wid, *_ in GLOBES:
        if wid not in globe_info:
            continue
        s = globe_info[wid]
        hm = decode(read_png16(DATA / s["file"]), s["lo"], s["hi"], s["nodata"])
        decoded[wid] = hm
        mlat, mlon = where_max(hm)
        print(f"  {wid:8s} range {s['lo']:9.1f} .. {s['hi']:8.1f} m, max at {mlat:+.2f}, {mlon:+.2f}"
              + (f", no data {s['nodata_frac']:.1%}" if s["nodata"] else ""))
        for label, lat, lon, expect in GLOBE_CHECKS.get(wid, []):
            val = globe_value(hm, lat, lon)
            rank = (np.nanmean(hm > val) * 100) if np.isfinite(val) else float("nan")
            print(f"    {label:18s} ({lat:+7.2f}, {lon:+8.2f}): {val:9.1f} m   top {rank:5.2f}%   expect {expect}")
    print("\nPlace checks:")
    for name, *_ in PLACES:
        if name not in place_info:
            continue
        p = place_info[name]
        hm = decode(read_png16(DATA / p["file"]), p["lo"], p["hi"], 0)
        cx, cy = p["centre_px"]
        val = hm[int(cy), int(cx)]
        print(f"  {name:20s} z{p['z']:<2d} {p['lo']:8.1f} .. {p['hi']:7.1f} m  centre {val:8.1f} m"
              f"  sea {p['sea']} ({p['sea_frac']:.0%}, depths {p['deep_frac']:.0%})  {p['mpp']:.2f} m/px"
              f"  despiked {p['despiked']}  {PLACE_EXPECT.get(name, '')}")
        decoded[name] = hm

    if args.previews:
        pv = Path(args.previews)
        pv.mkdir(parents=True, exist_ok=True)
        items = []
        for key, hm in decoded.items():
            fname = globe_info[key]["file"] if key in globe_info else place_info[key]["file"]
            im = hillshade(hm)
            im.save(pv / fname.replace(".png", "-hillshade.png"))
            items.append((key, im))
        contact_sheet(items, pv / "contact-sheet.png")

    print("\nFiles:")
    total = 0
    for f in sorted(DATA.glob("*.png")):
        total += f.stat().st_size
        print(f"  {f.name:28s} {f.stat().st_size / 1e6:7.2f} MB")
    print(f"  {'total':28s} {total / 1e6:7.2f} MB")


if __name__ == "__main__":
    main()
