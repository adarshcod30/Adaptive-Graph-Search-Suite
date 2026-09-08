/*
 * Browser front-end for the WebAssembly engine.
 *
 * There is no backend. The C++ engine runs in this tab, which is what removes
 * the whole class of defects the Python bridge had: no `map` parameter reaches
 * a filesystem, and no two requests can race over a shared trace file, because
 * each tab owns its own engine instance and its own memory.
 */
'use strict';

// Repo layout serves the page from web/ with data one level up; the Pages
// build flattens them into siblings and rewrites just this line.
const DATA_ROOT = '../data';

const CATALOG = [
    { group: 'Nationwide (OpenStreetMap)' },
    { id: 'India_Highways', label: 'India: national highways', sub: '207,610 junctions — expressways and NH, whole country',
      dir: 'networks/India_Highways', geo: true, mb: 12, heavy: true },
    { id: 'India_Railways', label: 'India: railway network', sub: '69,944 track nodes, 8,857 named stations',
      dir: 'networks/India_Railways', geo: true, mb: 5, heavy: true,
      stationsFile: 'networks/India_Railways/stations.csv' },

    { group: 'City road networks (OpenStreetMap)' },
    { id: 'Delhi_NCR', label: 'Delhi NCR', sub: '488,431 junctions — Delhi, Gurugram, Noida, Faridabad, Ghaziabad', dir: 'cities/Delhi_NCR', geo: true, mb: 38, heavy: true },
    { id: 'Delhi',     label: 'Delhi',     sub: '253,748 junctions — full NCT including IGI Airport',               dir: 'cities/Delhi', geo: true, mb: 19, heavy: true },
    { id: 'Bengaluru', label: 'Bengaluru', sub: '240,237 junctions — city and outer ring',                          dir: 'cities/Bengaluru', geo: true, mb: 17, heavy: true },
    { id: 'Chennai',   label: 'Chennai',   sub: '141,934 junctions — city and suburbs',                             dir: 'cities/Chennai', geo: true, mb: 10 },
    { id: 'Jaipur',    label: 'Jaipur',    sub: '123,456 junctions — walled city to outer suburbs',                 dir: 'cities/Jaipur', geo: true, mb: 9 },
    { id: 'Kolkata',   label: 'Kolkata',   sub: '104,085 junctions — city and Howrah',                              dir: 'cities/Kolkata', geo: true, mb: 8 },
    { id: 'Mumbai',    label: 'Mumbai',    sub: '79,783 junctions — island city and suburbs',                       dir: 'cities/Mumbai', geo: true, mb: 6 },
    { id: 'Gorakhpur', label: 'Gorakhpur', sub: '31,830 junctions — whole city',                                    dir: 'cities/Gorakhpur', geo: true, mb: 3 },

    { group: 'Metro systems (OpenStreetMap)' },
    { id: 'transit:Delhi_Metro',          label: 'Delhi Metro',          sub: '257 stations',           transit: 'Delhi Metro' },
    { id: 'transit:Namma_Metro',          label: 'Namma Metro',          sub: '85 stations, Bengaluru', transit: 'Namma Metro' },
    { id: 'transit:Mumbai_Metro',         label: 'Mumbai Metro',         sub: '81 stations',            transit: 'Mumbai Metro' },
    { id: 'transit:Chennai_Metro',        label: 'Chennai Metro',        sub: '62 stations',            transit: 'Chennai Metro' },
    { id: 'transit:Hyderabad_Metro',      label: 'Hyderabad Metro',      sub: '59 stations',            transit: 'Hyderabad Metro' },
    { id: 'transit:Kolkata_Metro',        label: 'Kolkata Metro',        sub: '59 stations',            transit: 'Kolkata Metro' },
    { id: 'transit:Ahmedabad_Metro',      label: 'Ahmedabad Metro',      sub: '53 stations',            transit: 'Ahmedabad Metro' },
    { id: 'transit:Nagpur_Metro',         label: 'Nagpur Metro',         sub: '38 stations',            transit: 'Nagpur Metro' },
    { id: 'transit:Mumbai_Monorail',      label: 'Mumbai Monorail',      sub: '33 stations',            transit: 'Mumbai Monorail' },
    { id: 'transit:Pune_Metro',           label: 'Pune Metro',           sub: '29 stations',            transit: 'Pune Metro' },
    { id: 'transit:Kochi_Metro',          label: 'Kochi Metro',          sub: '25 stations',            transit: 'Kochi Metro' },
    { id: 'transit:Lucknow_Metro',        label: 'Lucknow Metro',        sub: '21 stations',            transit: 'Lucknow Metro' },
    { id: 'transit:Noida_Metro',          label: 'Noida Metro',          sub: '21 stations',            transit: 'Noida Metro' },
    { id: 'transit:Namo_Bharat_RRTS',     label: 'Namo Bharat (RRTS)',   sub: '15 stations, Delhi NCR', transit: 'Namo Bharat (RRTS)' },
    { id: 'transit:Kanpur_Metro',         label: 'Kanpur Metro',         sub: '14 stations',            transit: 'Kanpur Metro' },
    { id: 'transit:Meerut_Metro',         label: 'Meerut Metro',         sub: '12 stations',            transit: 'Meerut Metro' },
    { id: 'transit:Navi_Mumbai_Metro',    label: 'Navi Mumbai Metro',    sub: '12 stations',            transit: 'Navi Mumbai Metro' },
    { id: 'transit:Jaipur_Metro',         label: 'Jaipur Metro',         sub: '11 stations',            transit: 'Jaipur Metro' },
    { id: 'transit:Rapid_Metro_Gurugram', label: 'Rapid Metro Gurugram', sub: '11 stations',            transit: 'Rapid Metro Gurugram' },
    { id: 'transit:Bhopal_Metro',         label: 'Bhopal Metro',         sub: '8 stations',             transit: 'Bhopal Metro' },
    { id: 'transit:Agra_Metro',           label: 'Agra Metro',           sub: '6 stations',             transit: 'Agra Metro' },
    { id: 'transit:Indore_Metro',         label: 'Indore Metro',         sub: '5 stations',             transit: 'Indore Metro' },
    { id: 'transit:Patna_Metro',          label: 'Patna Metro',          sub: '5 stations',             transit: 'Patna Metro' },

    { group: 'Long-distance rail services' },
    { id: 'rail:', label: 'Named train routes', sub: '746 stations from 253 services', rail: '' },

    { group: 'Synthetic' },
    { id: 'Grid_Integer', label: 'Grid, integer weights', sub: '225 nodes \u2014 the only graph Dial\u2019s applies to',
      dir: 'maps/Grid_Integer', geo: false },
];

const OP_DISCOVER = 0, OP_EXPAND = 1, OP_RELAX = 2;

const $ = id => document.getElementById(id);
const cv = $('cv'), ctx = cv.getContext('2d');

let M = null;                 // the Emscripten module
/* Geometry is held in typed arrays, not objects.
   An array of {id, x, y} objects costs roughly 60 bytes each in V8; at 207k
   nodes plus 280k edges that is tens of megabytes of small objects and a
   garbage-collection pause every redraw. Flat arrays make the draw loop two
   indexed reads per node, and let the engine hand its geometry over as a
   memory copy with no parsing. */
let graph = { nodeCount: 0, edgeCount: 0, ex: null, ey: null, eu: null, ev: null };
let geographic = false;
let stations = [];            // populated for transit graphs
let nodeNames = null;         // node -> station name, for the railway graph
let loadedTransitSet = null;  // 'metro' | 'rail' — which CSV pair is in the engine

let events = [], eventIdx = 0, finalPath = [], stepSize = 1, timer = null;
let closedEdges = new Set();
let chReady = false;
let heat = null;              // per-node score overlay (centrality)
let isoBands = null;
let markedNodes = new Set();  // bridge endpoints etc.

const state = { frontier: new Set(), explored: new Set(), parent: new Map(), head: -1 };
const camera = { x: 0, y: 0, zoom: 1 };
let bounds = { minx: 0, maxx: 1, miny: 0, maxy: 1 };

/* ------------------------------------------------------------------ engine */

/** Every entry point returns a malloc'd C string that we own and must free. */
function call(fn, sig, args) {
    const ptr = M.ccall(fn, 'number', sig, args);
    if (!ptr) throw new Error(`${fn} returned null`);
    try {
        return JSON.parse(M.UTF8ToString(ptr));
    } finally {
        M._agss_free(ptr);
    }
}

/* Large strings must go to the engine as heap pointers, not as ccall 'string'
   arguments. Emscripten marshals a string argument with stringToUTF8OnStack,
   so an 11 MB CSV blows the 8 MB stack and the module dies with "memory access
   out of bounds" before any of our code runs. Allocating on the heap and
   passing the pointer costs one extra copy and works at any size. */
function callWithBuffers(fn, strings, extraSig = [], extraArgs = []) {
    const ptrs = strings.map(str => M.stringToNewUTF8(str));
    try {
        const sig = strings.map(() => 'number').concat(extraSig);
        const args = ptrs.concat(extraArgs);
        const out = M.ccall(fn, 'number', sig, args);
        if (!out) throw new Error(`${fn} returned null`);
        try {
            return JSON.parse(M.UTF8ToString(out));
        } finally {
            M._agss_free(out);
        }
    } finally {
        for (const p of ptrs) M._free(p);
    }
}

function log(msg, cls) {
    const el = $('log');
    const line = document.createElement('div');
    line.textContent = msg;
    if (cls) line.style.color = `var(--${cls})`;
    el.prepend(line);
    while (el.childElementCount > 40) el.lastElementChild.remove();
}

/* -------------------------------------------------------------- rendering */

/* ------------------------------------------------------------ projection

   Node coordinates are projected into a shared "world" space once at load,
   and the camera works entirely in that space.

   For geographic graphs the world space is Web Mercator normalised to [0, 1],
   which is the projection every raster tile server publishes in. Using it
   here rather than the previous cos(latitude) approximation is what lets a
   real map slide underneath the graph with the two staying pixel-aligned at
   every zoom level -- an approximation that is merely close would drift
   visibly as you pan north or south.

   Planar graphs keep their own coordinates and have no basemap. */

const DEG = Math.PI / 180;

const mercX = lon => (lon + 180) / 360;
const mercY = lat => {
    const s = Math.sin(lat * DEG);
    return 0.5 - Math.log((1 + s) / (1 - s)) / (4 * Math.PI);
};
const invMercX = wx => wx * 360 - 180;
const invMercY = wy => Math.atan(Math.sinh(Math.PI * (1 - 2 * wy))) / DEG;

// Parallel Float64Arrays rather than per-node objects: the projection is
// evaluated once per load, and drawing 47k nodes then costs two array reads
// instead of a trig call each.
let world = { x: new Float64Array(0), y: new Float64Array(0) };

/// Mercator Y grows southward, matching screen Y; planar Y grows upward.
const yDir = () => (geographic ? 1 : -1);

function projectAll() {
    const n = graph.nodeCount;
    world = { x: new Float64Array(n), y: new Float64Array(n) };
    for (let i = 0; i < n; i++) {
        world.x[i] = geographic ? mercX(graph.ex[i]) : graph.ex[i];
        world.y[i] = geographic ? mercY(graph.ey[i]) : graph.ey[i];
    }
}

function computeBounds() {
    if (!graph.nodeCount) return;
    let minx = Infinity, maxx = -Infinity, miny = Infinity, maxy = -Infinity;
    for (let i = 0; i < world.x.length; i++) {
        const x = world.x[i], y = world.y[i];
        if (x < minx) minx = x;
        if (x > maxx) maxx = x;
        if (y < miny) miny = y;
        if (y > maxy) maxy = y;
    }
    const padx = (maxx - minx) * 0.05 || 1e-4, pady = (maxy - miny) * 0.05 || 1e-4;
    bounds = { minx: minx - padx, maxx: maxx + padx, miny: miny - pady, maxy: maxy + pady };
}

function fitCamera() {
    computeBounds();
    const w = bounds.maxx - bounds.minx, h = bounds.maxy - bounds.miny;
    // Mercator already carries the latitude correction, so this is a plain fit.
    camera.zoom = Math.min(cv.width / w, cv.height / h) * 0.9;
    if (geographic) camera.zoom = clampZoom(camera.zoom);
    camera.x = (bounds.minx + bounds.maxx) / 2;
    camera.y = (bounds.miny + bounds.maxy) / 2;
}

// World -> screen, and back.
const tx = wx => (wx - camera.x) * camera.zoom + cv.width / 2;
const ty = wy => cv.height / 2 + yDir() * (wy - camera.y) * camera.zoom;
const inv = (px_, py_) => [
    (px_ - cv.width / 2) / camera.zoom + camera.x,
    yDir() * (py_ - cv.height / 2) / camera.zoom + camera.y,
];

// Node index -> screen, the form the draw loop uses.
const nx = i => tx(world.x[i]);
const ny = i => ty(world.y[i]);

/* ------------------------------------------------------------- basemap

   A minimal slippy-map tile layer, drawn straight onto the same canvas under
   the graph.

   No mapping library: the camera already works in Web Mercator, so a tile is
   just an image placed at a known world rectangle, and reusing the existing
   projection keeps the map and the graph aligned by construction rather than
   by two libraries agreeing. It also keeps the page dependency-free, which is
   the whole reason it can be a single static file. */

const OSM_ATTR = '© OpenStreetMap contributors';
const ESRI_ATTR = 'Imagery © Esri, Maxar, Earthstar Geographics';

const BASEMAPS = {
    none: null,
    dark: {
        label: 'Dark',
        // No key-free provider ships a dark raster style, so a light one is
        // inverted on the canvas instead. CARTO's dark_all used to fill this
        // slot until they began stamping "API KEY REQUIRED" across every
        // unauthenticated tile -- served as HTTP 200 with a valid PNG, so
        // nothing errored and the watermark simply appeared on the map.
        url: 'https://server.arcgisonline.com/ArcGIS/rest/services/Canvas/World_Light_Gray_Base/MapServer/tile/{z}/{y}/{x}',
        maxZoom: 16,
        filter: 'invert(1) hue-rotate(180deg) brightness(0.78) contrast(1.05) saturate(0.7)',
        attribution: 'Tiles © Esri — ' + OSM_ATTR,
    },
    gray: {
        label: 'Light',
        url: 'https://server.arcgisonline.com/ArcGIS/rest/services/Canvas/World_Light_Gray_Base/MapServer/tile/{z}/{y}/{x}',
        maxZoom: 16,
        attribution: 'Tiles © Esri — ' + OSM_ATTR,
    },
    osm: {
        label: 'Street map',
        url: 'https://tile.openstreetmap.org/{z}/{x}/{y}.png',
        maxZoom: 19,
        attribution: OSM_ATTR,
    },
    satellite: {
        label: 'Satellite',
        url: 'https://server.arcgisonline.com/ArcGIS/rest/services/World_Imagery/MapServer/tile/{z}/{y}/{x}',
        maxZoom: 19,
        attribution: ESRI_ATTR,
    },
    terrain: {
        label: 'Terrain',
        url: 'https://a.tile.opentopomap.org/{z}/{x}/{y}.png',
        maxZoom: 17,
        attribution: OSM_ATTR + ', SRTM — style © OpenTopoMap (CC-BY-SA)',
    },
};

let basemap = 'dark';
const tileCache = new Map();     // "style/z/x/y" -> HTMLImageElement | 'failed'
let tilesInFlight = 0;
const MAX_IN_FLIGHT = 8;         // stay polite to the tile servers
const TILE_PX = 256;

function tileUrl(spec, z, x, y) {
    // Esri orders its path {z}/{y}/{x}; the placeholders in each spec already
    // encode that, so a plain substitution covers both conventions.
    return spec.url.replace('{z}', z).replace('{x}', x).replace('{y}', y);
}

function getTile(spec, z, x, y) {
    const key = `${basemap}/${z}/${x}/${y}`;
    const hit = tileCache.get(key);
    if (hit) return hit === 'failed' ? null : hit;
    if (tilesInFlight >= MAX_IN_FLIGHT) return null;

    const img = new Image();
    img.crossOrigin = 'anonymous';
    tilesInFlight++;
    img.onload = () => { tilesInFlight--; scheduleDraw(); };
    img.onerror = () => { tilesInFlight--; tileCache.set(key, 'failed'); };
    img.src = tileUrl(spec, z, x, y);
    tileCache.set(key, img);

    // An unbounded cache would grow without limit while panning a city.
    if (tileCache.size > 900) {
        let dropped = 0;
        for (const k of tileCache.keys()) {
            tileCache.delete(k);
            if (++dropped > 300) break;
        }
    }
    return null;
}

/// Tile zoom whose natural pixel size is closest to one screen pixel.
function tileZoomFor(spec) {
    const z = Math.round(Math.log2(camera.zoom / TILE_PX));
    return Math.max(0, Math.min(spec.maxZoom, z));
}

function drawBasemap() {
    const spec = BASEMAPS[basemap];
    if (!spec || !geographic || !graph.nodeCount) return;

    const z = tileZoomFor(spec);
    const n = 2 ** z;
    const size = camera.zoom / n;            // one tile, in screen pixels
    if (!(size > 0)) return;

    const [wx0, wy0] = inv(0, 0);
    const [wx1, wy1] = inv(cv.width, cv.height);
    const x0 = Math.floor(Math.min(wx0, wx1) * n), x1 = Math.ceil(Math.max(wx0, wx1) * n);
    const y0 = Math.floor(Math.min(wy0, wy1) * n), y1 = Math.ceil(Math.max(wy0, wy1) * n);
    if ((x1 - x0) * (y1 - y0) > 400) return;  // absurd viewport; skip rather than thrash

    ctx.save();
    ctx.imageSmoothingEnabled = true;
    if (spec.filter) ctx.filter = spec.filter;
    for (let ty_ = y0; ty_ < y1; ty_++) {
        if (ty_ < 0 || ty_ >= n) continue;
        for (let tx_ = x0; tx_ < x1; tx_++) {
            const wrapped = ((tx_ % n) + n) % n;   // wrap across the date line
            const img = getTile(spec, z, wrapped, ty_);
            const sx = tx(tx_ / n);
            const sy = ty(ty_ / n);
            if (img && img.complete && img.naturalWidth) {
                // +1 closes the hairline seams that rounding leaves between tiles.
                ctx.drawImage(img, sx, sy, size + 1, size + 1);
            } else {
                const parent = coarserTile(spec, z, wrapped, ty_);
                if (parent) {
                    // Show a stretched lower-zoom tile until the sharp one lands,
                    // so panning never flashes empty background.
                    const { img: pimg, sxf, syf, sf } = parent;
                    ctx.drawImage(pimg, sxf * TILE_PX, syf * TILE_PX, sf * TILE_PX,
                                  sf * TILE_PX, sx, sy, size + 1, size + 1);
                }
            }
        }
    }
    ctx.restore();
}

/// Nearest already-loaded ancestor tile, plus the sub-rectangle of it that
/// covers the tile we actually wanted.
function coarserTile(spec, z, x, y) {
    for (let up = 1; up <= 4 && z - up >= 0; up++) {
        const f = 2 ** up;
        const px_ = Math.floor(x / f), py_ = Math.floor(y / f);
        const cached = tileCache.get(`${basemap}/${z - up}/${px_}/${py_}`);
        if (cached && cached !== 'failed' && cached.complete && cached.naturalWidth) {
            return {
                img: cached,
                sxf: (x - px_ * f) / f,
                syf: (y - py_ * f) / f,
                sf: 1 / f,
            };
        }
    }
    return null;
}

/// Keep the camera inside a range the tile pyramid can serve. Without this,
/// scrolling far enough leaves nothing but stretched low-zoom tiles, or
/// collapses the graph to a point.
function clampZoom(z) {
    if (!geographic) return Math.max(1e-4, Math.min(1e9, z));
    const spec = BASEMAPS[basemap];
    const maxLevel = (spec ? spec.maxZoom : 22) + 1.5;
    return Math.max(TILE_PX * 2 ** 1, Math.min(TILE_PX * 2 ** maxLevel, z));
}

let drawPending = false;
function scheduleDraw() {
    if (drawPending) return;
    drawPending = true;
    // setTimeout, not rAF: tiles keep arriving while the page is hidden.
    setTimeout(() => { drawPending = false; draw(); }, 16);
}

function setBasemap(name) {
    basemap = name;
    const picker = $('baseSel');
    if (picker && picker.value !== name) picker.value = name;
    const spec = BASEMAPS[name];
    const el = $('attribution');
    if (el) {
        el.textContent = spec ? spec.attribution : '';
        el.style.display = spec ? '' : 'none';
    }
    draw();
}

/* Canvas sizing.
   fitCamera divides by the canvas dimensions, so fitting against a canvas that
   has not been laid out yet yields zoom 0 and collapses the graph into a single
   pixel. The element can legitimately measure zero -- during startup, while the
   pane is animating, or when the tab is hidden -- so sizing is driven by a
   ResizeObserver and every path re-fits once real dimensions arrive, rather
   than assuming one call at boot was enough. */
function canvasPixels() {
    const rect = cv.getBoundingClientRect();
    const dpr = Math.min(window.devicePixelRatio || 1, 2);
    return [Math.round(rect.width * dpr), Math.round(rect.height * dpr)];
}

function resize() {
    const [w, h] = canvasPixels();
    if (w <= 0 || h <= 0) return false;   // not laid out; the observer will call back
    if (w !== cv.width || h !== cv.height) {
        cv.width = w;
        cv.height = h;
        camera.zoom = 0;                  // extent-to-pixel mapping just changed
    }
    if (!(camera.zoom > 0) && graph.nodeCount) fitCamera();
    draw();
    return true;
}

new ResizeObserver(resize).observe(cv);

function currentPath() {
    if (state.head < 0) return [];
    const out = [], seen = new Set();
    let cur = state.head;
    while (cur !== undefined && cur >= 0 && !seen.has(cur)) {
        seen.add(cur);
        out.push(cur);
        cur = state.parent.get(cur);
    }
    return out.reverse();
}

/* Stroke widths and radii must be screen-space. Scaling them by camera.zoom
   works while coordinates are in the 0-100 range, but a geographic graph is
   measured in degrees, so zoom reaches ~12,000 on a city and a 0.06x factor
   paints a 700-pixel-wide "line" over half the map. Clamp to sane pixels. */
const px = (v, lo, hi) => Math.max(lo, Math.min(hi, v));

function draw() {
    ctx.clearRect(0, 0, cv.width, cv.height);
    if (!graph.nodeCount) return;

    drawBasemap();
    const onMap = BASEMAPS[basemap] !== null && geographic;
    const dpr = Math.min(window.devicePixelRatio || 1, 2);
    const big = graph.nodeCount > 12000;
    // Since the camera works in Mercator units, camera.zoom is now in the
    // millions rather than the tens, so sizes derive from the slippy zoom
    // level instead. Reusing the old factor drew every one of 47k nodes at the
    // 5px clamp and buried the map under a solid blob.
    const mapZoom = geographic ? Math.log2(camera.zoom / TILE_PX) : 0;
    const r = geographic
        ? px((mapZoom - 9) * 0.55, 0.7, 5) * dpr
        : px(camera.zoom * 0.06, big ? 1.0 : 1.6, 5) * dpr;
    const pathWidth = geographic
        ? px((mapZoom - 9) * 0.8, 2, 7) * dpr
        : px(camera.zoom * 0.06, 2, 6) * dpr;

    // Base edges. On a large graph these are drawn as one path so the whole
    // network is a single stroke call rather than 120k of them.
    ctx.strokeStyle = onMap ? 'rgba(125,180,255,0.55)' : 'rgba(120,150,210,0.16)';
    ctx.lineWidth = (onMap ? 0.6 : big ? 0.5 : 1) * dpr;
    ctx.beginPath();
    for (let i = 0; i < graph.edgeCount; i++) {
        const u = graph.eu[i], v = graph.ev[i];
        if (u > v) continue;                     // one line per undirected pair
        ctx.moveTo(nx(u), ny(u));
        ctx.lineTo(nx(v), ny(v));
    }
    ctx.stroke();

    // Isochrone hulls sit under everything else.
    if (isoBands) {
        const shades = ['rgba(78,168,255,0.22)', 'rgba(78,168,255,0.14)', 'rgba(78,168,255,0.08)'];
        isoBands.forEach((band, i) => {
            if (band.hull.length < 3) return;
            ctx.fillStyle = shades[Math.min(i, shades.length - 1)];
            ctx.beginPath();
            band.hull.forEach(([x, y], j) => {
                const sx = tx(geographic ? mercX(x) : x);
                const sy = ty(geographic ? mercY(y) : y);
                j ? ctx.lineTo(sx, sy) : ctx.moveTo(sx, sy);
            });
            ctx.closePath();
            ctx.fill();
        });
    }

    // Nodes, tinted by centrality when a heat map is loaded.
    // Node dots only once they are actually distinguishable. The old
    // threshold was in the pre-Mercator zoom units and now always passes, so
    // 47k dots covered the map they were supposed to sit on. Below this the
    // edges carry the network and the basemap carries the context.
    const showNodes = heat !== null || (geographic ? mapZoom >= 15 : !big || camera.zoom > 40);
    if (showNodes) {
        for (let i = 0; i < graph.nodeCount; i++) {
            const px_ = nx(i), py_ = ny(i);
            if (px_ < -20 || py_ < -20 || px_ > cv.width + 20 || py_ > cv.height + 20) continue;
            if (heat) {
                const t = heat[i];
                ctx.fillStyle = t > 0.01
                    ? `hsl(${(1 - t) * 210}, 90%, ${35 + t * 30}%)`
                    : 'rgba(120,150,210,0.35)';
            } else {
                ctx.fillStyle = onMap ? 'rgba(140,180,255,0.45)' : 'rgba(160,185,225,0.55)';
            }
            ctx.beginPath();
            ctx.arc(px_, py_, heat ? r * (0.8 + heat[i] * 2.2) : r, 0, 6.2832);
            ctx.fill();
        }
    }

    // Closed roads.
    if (closedEdges.size) {
        ctx.strokeStyle = 'var(--danger)';
        ctx.strokeStyle = '#f87171';
        ctx.lineWidth = pathWidth;
        ctx.beginPath();
        for (const key of closedEdges) {
            const [u, v] = key.split(':').map(Number);
            if (u >= graph.nodeCount || v >= graph.nodeCount) continue;
            ctx.moveTo(nx(u), ny(u));
            ctx.lineTo(nx(v), ny(v));
        }
        ctx.stroke();
    }

    // Search state.
    const done = eventIdx >= events.length;
    const path = done && finalPath.length ? finalPath : currentPath();

    if (state.explored.size) {
        ctx.fillStyle = onMap ? 'rgba(251,146,60,0.85)' : 'rgba(251,146,60,0.65)';
        for (const id of state.explored) {
            if (id >= graph.nodeCount) continue;
            ctx.beginPath();
            ctx.arc(nx(id), ny(id), r * 1.4, 0, 6.2832);
            ctx.fill();
        }
    }
    if (state.frontier.size) {
        ctx.fillStyle = 'rgba(217,70,239,0.9)';
        ctx.shadowColor = 'rgba(217,70,239,0.9)';
        ctx.shadowBlur = 8;
        for (const id of state.frontier) {
            if (id >= graph.nodeCount) continue;
            ctx.beginPath();
            ctx.arc(nx(id), ny(id), r * 1.9, 0, 6.2832);
            ctx.fill();
        }
        ctx.shadowBlur = 0;
    }
    if (path.length > 1) {
        ctx.strokeStyle = '#22c55e';
        ctx.shadowColor = '#22c55e';
        ctx.shadowBlur = 12;
        ctx.lineWidth = pathWidth;
        ctx.beginPath();
        path.forEach((id, i) => {
            if (id >= graph.nodeCount) return;
            i ? ctx.lineTo(nx(id), ny(id)) : ctx.moveTo(nx(id), ny(id));
        });
        ctx.stroke();
        ctx.shadowBlur = 0;
    }

    // Marked nodes (bridge endpoints).
    if (markedNodes.size) {
        ctx.strokeStyle = '#f87171';
        ctx.lineWidth = 2 * dpr;
        for (const id of markedNodes) {
            if (id >= graph.nodeCount) continue;
            ctx.beginPath();
            ctx.arc(nx(id), ny(id), r * 3, 0, 6.2832);
            ctx.stroke();
        }
    }

    // Endpoints on top.
    for (const [id, color] of [[srcNode(), '#4ea8ff'], [dstNode(), '#22c55e']]) {
        if (id < 0 || id >= graph.nodeCount) continue;
        ctx.fillStyle = color;
        ctx.strokeStyle = '#fff';
        ctx.lineWidth = 2 * dpr;
        ctx.beginPath();
        ctx.arc(nx(id), ny(id), px(r * 2.2, 5 * dpr, 11 * dpr), 0, 6.2832);
        ctx.fill();
        ctx.stroke();
    }
}

/* ------------------------------------------------------------- replay loop */

function resetState() {
    state.frontier.clear();
    state.explored.clear();
    state.parent.clear();
    state.head = -1;
    eventIdx = 0;
}

function applyEvent(ev) {
    const [op, node, parent] = ev;
    if (op === OP_DISCOVER) {
        state.frontier.add(node);
        if (parent >= 0) state.parent.set(node, parent);
    } else if (op === OP_RELAX) {
        if (parent >= 0) state.parent.set(node, parent);
    } else if (op === OP_EXPAND) {
        state.frontier.delete(node);
        state.explored.add(node);
        state.head = node;
    }
}

/* Seeking backwards replays from the start rather than storing undo records,
   which keeps stepping forward at O(1) -- the point of the delta format. */
function seekTo(n) {
    if (n < eventIdx) resetState();
    while (eventIdx < n && eventIdx < events.length) applyEvent(events[eventIdx++]);
}

function play() {
    if (timer || !events.length) return;
    if (eventIdx >= events.length) seekTo(0);
    timer = setInterval(() => {
        seekTo(Math.min(events.length, eventIdx + stepSize));
        if (eventIdx >= events.length) pause();
        requestAnimationFrame(draw);
    }, 1000 / Number($('speed').value));
}
function pause() { if (timer) { clearInterval(timer); timer = null; } }
function step() { pause(); seekTo(Math.min(events.length, eventIdx + stepSize)); draw(); }

/* ------------------------------------------------------------------ loaders */

const srcNode = () => Number($('srcIn').value) | 0;
const dstNode = () => Number($('dstIn').value) | 0;

async function fetchText(url) {
    const res = await fetch(url);
    if (!res.ok) throw new Error(`${url} -> HTTP ${res.status}`);
    return res.text();
}

/// The engine holds one transit dataset at a time, so switching between the
/// metro and rail networks reloads it rather than trying to merge two very
/// different things into one graph.
async function ensureTransit(which) {
    if (loadedTransitSet === which) return;
    const dir = which === 'rail' ? 'railways' : 'transit';
    const [st, lk] = await Promise.all([
        fetchText(`${DATA_ROOT}/${dir}/stations.csv`),
        fetchText(`${DATA_ROOT}/${dir}/links.csv`),
    ]);
    const r = callWithBuffers('agss_load_transit', [st, lk]);
    if (!r.ok) throw new Error(r.error);
    loadedTransitSet = which;
    log(`${which === 'rail' ? 'railway' : 'metro'} data: ${r.stations} stations, ` +
        `${r.systems.length} system(s)`);
}

async function loadSelected() {
    const entry = CATALOG.find(c => c.id && c.id === $('mapSel').value);
    if (!entry) return;
    clearOverlays();
    pause();

    try {
        if (entry.transit !== undefined || entry.rail !== undefined) {
            const isRail = entry.rail !== undefined;
            await ensureTransit(isRail ? 'rail' : 'metro');
            const filter = isRail ? entry.rail : entry.transit;
            const r = call('agss_build_transit', ['string', 'number'], [filter, 0]);
            if (!r.ok) throw new Error(r.error);
            stations = r.stations;
            geographic = true;
            loadGeometry();
            $('mapInfo').textContent =
                `${r.stationNodes} stations, ${r.edges} edges — journey times estimated ` +
                `from distance, not timetables`;
            log(`${entry.label}: ${r.stationNodes} stations`);
        } else {
            // Say the real size. Delhi NCR is 38 MB of CSV; calling that
            // "a few MB" makes a slow connection look like a hang.
            if (entry.mb >= 8) {
                log(`fetching ${entry.label} — ${entry.mb} MB of CSV, ` +
                    `served compressed; this takes a moment…`);
            }
            nodeNames = null;
            const [nodes, edges] = await Promise.all([
                fetchText(`${DATA_ROOT}/${entry.dir}/nodes.csv`),
                fetchText(`${DATA_ROOT}/${entry.dir}/edges.csv`),
            ]);
            const r = callWithBuffers('agss_load_graph', [entry.id, nodes, edges],
                                      ['number', 'number'], [entry.geo ? 1 : 0, 1]);
            if (!r.ok) throw new Error(r.error);
            stations = [];
            geographic = entry.geo;
            loadGeometry();
            $('mapInfo').textContent =
                `${r.nodes.toLocaleString()} nodes, ${r.edges.toLocaleString()} edges` +
                (r.admissible ? '' : ` — heuristic inadmissible (${r.admissibility.toFixed(3)}×)`);
            if (!r.admissible) {
                log('warning: an edge is shorter than the straight line between its endpoints, ' +
                    'so A* is not optimal on this graph', 'danger');
            }
            if (r.warningCount) log(`${r.warningCount} malformed row(s) skipped`, 'danger');

            // The railway graph is a track network, so its stations are named
            // nodes rather than a separate transit layer. Load the names so
            // directions read "Gorakhpur Junction" instead of "node 25336".
            if (entry.stationsFile) {
                try {
                    const csv = await fetchText(`${DATA_ROOT}/${entry.stationsFile}`);
                    nodeNames = new Map();
                    for (const line of csv.split('\n').slice(1)) {
                        const cut = line.indexOf(',');
                        if (cut < 0) continue;
                        const id = Number(line.slice(0, cut));
                        const name = line.slice(cut + 1).trim().replace(/^"|"$/g, '');
                        if (Number.isFinite(id) && name) nodeNames.set(id, name);
                    }
                    log(`${nodeNames.size.toLocaleString()} named stations on the network`);
                } catch (err) {
                    log(`station names unavailable: ${err.message}`, 'danger');
                }
            }
            log(`${entry.label}: ${r.nodes.toLocaleString()} nodes, ${r.edges.toLocaleString()} edges`);
        }

        $('baseInfo').textContent = geographic
            ? 'Real map tiles under the graph. Scroll to zoom, drag to pan.'
            : 'This network has no real-world coordinates, so no basemap applies.';
        pickDefaultEndpoints();
        $('srcIn').max = $('dstIn').max = graph.nodeCount - 1;
        // Fit on the next frame: the ResizeObserver may still be catching up
        // with the canvas, and fitting against stale dimensions is what put
        // the whole graph in a single pixel.
        // Fit synchronously. requestAnimationFrame does not fire while the
        // page is hidden -- a background tab, or a collapsed pane -- so
        // deferring the fit to it leaves zoom at 0 and the graph in one pixel
        // until the user happens to look at it.
        camera.zoom = 0;
        resize();
    } catch (err) {
        log(`load failed: ${err.message}`, 'danger');
    }
}

function loadGeometry() {
    const n = M._agss_node_count();
    const m = M._agss_edge_count();

    // Views over the WASM heap, copied out before the buffers are freed. The
    // heap can be reallocated by a later growth, so a retained view would
    // silently start reading somewhere else.
    const cp = M._agss_coords_buffer();
    const coords = new Float64Array(M.HEAPF64.buffer, cp, n * 2).slice();
    M._agss_free(cp);

    const ep = M._agss_edges_buffer();
    const edges = new Int32Array(M.HEAP32.buffer, ep, m * 2).slice();
    M._agss_free(ep);

    graph = {
        nodeCount: n,
        edgeCount: m,
        ex: new Float64Array(n),
        ey: new Float64Array(n),
        eu: new Int32Array(m),
        ev: new Int32Array(m),
    };
    for (let i = 0; i < n; i++) {
        graph.ex[i] = coords[i * 2];
        graph.ey[i] = coords[i * 2 + 1];
    }
    for (let i = 0; i < m; i++) {
        graph.eu[i] = edges[i * 2];
        graph.ev[i] = edges[i * 2 + 1];
    }
    projectAll();
}

/// Choose endpoints that are actually connected.
///
/// Defaulting to node 0 looks harmless and is not: a real OSM extract has a
/// few hundred stranded nodes at the clipping boundary, and Delhi's node 0 is
/// one of them -- so the first thing a visitor saw was "no route". Picking two
/// nodes from the largest strongly connected component means the default query
/// always works, on every network.
function pickDefaultEndpoints() {
    const n = graph.nodeCount;
    if (!n) return;
    const fallbackA = 0, fallbackB = Math.floor(n * 0.7);
    try {
        const scc = call('agss_analyze', ['string', 'number', 'number', 'number'],
            ['scc', 0, 0, 0]);
        if (!scc.ok) throw new Error(scc.error);

        const sizes = new Map();
        for (const c of scc.componentOf) sizes.set(c, (sizes.get(c) || 0) + 1);
        let biggest = -1, best = -1;
        for (const [c, size] of sizes) if (size > best) { best = size; biggest = c; }

        const members = [];
        for (let i = 0; i < scc.componentOf.length; i++) {
            if (scc.componentOf[i] === biggest) members.push(i);
        }
        if (members.length < 2) throw new Error('no usable component');

        // Spread the pair out so the default query is a real journey rather
        // than two adjacent junctions.
        $('srcIn').value = members[Math.floor(members.length * 0.15)];
        $('dstIn').value = members[Math.floor(members.length * 0.85)];
    } catch (err) {
        $('srcIn').value = fallbackA;
        $('dstIn').value = fallbackB;
        log(`could not pick connected endpoints (${err.message}); using defaults`, 'danger');
    }
}

function clearOverlays() {
    heat = null;
    isoBands = null;
    markedNodes.clear();
    closedEdges.clear();
    events = [];
    finalPath = [];
    resetState();
    $('results').innerHTML = '';
}

/* ------------------------------------------------------------------ actions */

/* Browsers clamp performance.now() to ~0.1 ms as a Spectre mitigation, and the
   engine's steady_clock rides on it. Reporting "0.0000 ms" would imply a
   precision the platform does not offer, so anything under the floor is shown
   as such; the Benchmark button amortises over the clamp for a real figure. */
const CLOCK_FLOOR_MS = 0.1;
function formatMs(v) {
    if (!(v > 0)) return `< ${CLOCK_FLOOR_MS} ms`;
    return v < CLOCK_FLOOR_MS ? `< ${CLOCK_FLOOR_MS} ms` : `${v.toFixed(3)} ms`;
}

function setMetrics(m, eventCount) {
    $('mAlg').textContent = m.algorithm;
    $('mTime').textContent = m.timeComplexity;
    $('mSpace').textContent = m.spaceComplexity;
    $('mMs').textContent = formatMs(m.algorithmMs);
    $('mTrace').textContent = formatMs(m.traceMs ?? 0);
    $('mExp').textContent = m.nodesExpanded.toLocaleString();
    $('mRel').textContent = m.edgesRelaxed.toLocaleString();
    $('mCost').textContent = m.success ? m.pathCost.toFixed(2) : '—';
    $('mEvents').textContent = eventCount.toLocaleString();
}

/// Preprocess for Contraction Hierarchies, reporting what it cost.
///
/// Kept as an explicit step rather than hidden inside the first query: CH
/// trades a one-off build for near-free queries, and burying the build would
/// misrepresent exactly the bargain the feature exists to demonstrate.
async function buildCH() {
    if (!graph.nodeCount) return;
    const btn = $('chBtn');
    btn.disabled = true;
    btn.textContent = 'Preprocessing\u2026';
    $('chInfo').textContent = 'Contracting nodes and inserting shortcuts\u2026';
    // Yield so the button text paints before the engine blocks the thread.
    await new Promise(r => setTimeout(r, 30));

    const r = call('agss_ch_build', ['number'], [20000]);
    btn.disabled = false;
    btn.textContent = 'Rebuild hierarchy';
    if (!r.ok) { log(r.error, 'danger'); return; }

    chReady = true;
    $('chInfo').innerHTML =
        `Built in <b>${r.buildMs.toFixed(0)} ms</b> \u2014 ${r.shortcuts.toLocaleString()} shortcuts ` +
        `on ${r.originalEdges.toLocaleString()} edges (${r.edgeGrowth.toFixed(2)}\u00d7 arcs)` +
        (r.aborted ? '<br><b>Budget exhausted</b>: answers stay exact, queries just less fast.' : '');
    log(`CH: ${r.buildMs.toFixed(0)} ms, ${r.shortcuts.toLocaleString()} shortcuts, ` +
        `${r.edgeGrowth.toFixed(2)}x arc growth`);
    $('chRaceBtn').disabled = false;
}

/// Run the same query through CH and Dijkstra and report the difference.
function raceCH() {
    if (!chReady) { log('preprocess first', 'danger'); return; }
    pause();
    const s = srcNode(), t = dstNode();

    const chRes = call('agss_ch_query', ['number', 'number', 'number'], [s, t, 1]);
    if (chRes.ok === false) { log(chRes.error, 'danger'); return; }
    const dj = call('agss_route', ['string', 'number', 'number', 'number', 'string'],
        ['dijkstra', s, t, 0, '']);

    if (!chRes.metadata.success || !dj.metadata.success) {
        log('no route between those nodes', 'danger');
        return;
    }
    const reps = graph.nodeCount > 20000 ? 20 : 100;
    const djBench = call('agss_bench', ['string', 'number', 'number', 'number'],
        ['dijkstra', s, t, reps]);

    const agree = Math.abs(chRes.metadata.pathCost - dj.metadata.pathCost) < 1e-6;
    const nodeRatio = dj.metadata.nodesExpanded / Math.max(1, chRes.metadata.nodesExpanded);
    $('results').innerHTML =
        `<table><tr><th></th><th>expanded</th><th>cost</th></tr>` +
        `<tr><td>Dijkstra</td><td>${dj.metadata.nodesExpanded.toLocaleString()}</td>` +
        `<td>${dj.metadata.pathCost.toFixed(2)}</td></tr>` +
        `<tr><td>CH</td><td>${chRes.metadata.nodesExpanded.toLocaleString()}</td>` +
        `<td>${chRes.metadata.pathCost.toFixed(2)}</td></tr>` +
        `<tr><td><b>ratio</b></td><td class="good"><b>${nodeRatio.toFixed(0)}\u00d7 fewer</b></td>` +
        `<td class="${agree ? 'good' : 'bad'}">${agree ? 'identical' : 'MISMATCH'}</td></tr>` +
        `</table><div class="hint">Dijkstra averages ${djBench.amortisedMs.toFixed(4)} ms ` +
        `over ${reps} runs. CH searches the hierarchy instead of the map.</div>`;

    events = chRes.events || [];
    finalPath = chRes.path || [];
    resetState();
    stepSize = Math.max(1, Math.ceil(events.length / 200));
    setMetrics(chRes.metadata, events.length);
    log(`CH expanded ${chRes.metadata.nodesExpanded} nodes vs Dijkstra's ` +
        `${dj.metadata.nodesExpanded} \u2014 ${nodeRatio.toFixed(0)}x fewer, same cost`);
    play();
}

/// Route with traffic at the selected hour, via Customizable CH.
///
/// This is the pairing that motivates CCH: the metric changes every hour, and
/// rebuilding a plain hierarchy each time would cost seconds where
/// re-customizing costs milliseconds.
async function routeAtHour() {
    if (!graph.nodeCount) return;
    const hour = Number($('hourSlider').value);
    const btn = $('trafficBtn');
    btn.disabled = true;
    btn.textContent = 'Working\u2026';
    await new Promise(r => setTimeout(r, 20));

    const c = call('agss_cch_customize', ['number'], [hour]);
    btn.disabled = false;
    btn.textContent = 'Route at this hour';
    if (!c.ok) { log(c.error, 'danger'); return; }

    const q = call('agss_cch_query', ['number', 'number', 'number'], [srcNode(), dstNode(), 1]);
    if (q.ok === false) { log(q.error, 'danger'); return; }
    if (!q.metadata.success) { log('no route between those nodes', 'danger'); return; }

    events = q.events || [];
    finalPath = q.path || [];
    resetState();
    stepSize = Math.max(1, Math.ceil(events.length / 200));
    setMetrics(q.metadata, events.length);

    const mins = q.metadata.pathCost / 60;
    $('trafficInfo').innerHTML =
        `<b>${mins.toFixed(1)} min</b> departing at ${String(hour).padStart(2, '0')}:00.` +
        (c.builtNow ? ` Structure built once in ${c.buildMs.toFixed(0)} ms;` : '') +
        ` re-costed in <b>${c.customizeMs.toFixed(0)} ms</b>.`;
    log(`traffic ${String(hour).padStart(2, '0')}:00 \u2192 ${mins.toFixed(1)} min ` +
        `(customize ${c.customizeMs.toFixed(0)} ms)`);
    play();
}

/// Duration of the same trip departing at every hour, so rush hour is
/// something you see rather than something the page asserts.
function scanDay() {
    if (!graph.nodeCount) return;
    const r = call('agss_traffic_scan', ['number', 'number', 'number'],
        [srcNode(), dstNode(), 24]);
    if (!r.ok) { log(r.error, 'danger'); return; }
    const finite = r.durations.filter(d => d !== null && isFinite(d));
    if (!finite.length) { log('no route between those nodes', 'danger'); return; }
    const max = Math.max(...finite);

    const rows = r.durations.map((d, i) => {
        const hour = String(i).padStart(2, '0') + ':00';
        if (d === null || !isFinite(d)) return `<tr><td>${hour}</td><td>\u2014</td><td></td></tr>`;
        const mins = d / 60;
        const width = Math.round(100 * d / max);
        const peak = d === r.worstDuration;
        const quiet = d === r.bestDuration;
        return `<tr><td>${hour}</td><td class="${peak ? 'bad' : quiet ? 'good' : ''}">` +
               `${mins.toFixed(1)}</td><td><div style="height:8px;border-radius:3px;` +
               `width:${width}%;background:${peak ? '#f87171' : quiet ? '#34d399' : '#4ea8ff'}` +
               `"></div></td></tr>`;
    }).join('');
    $('results').innerHTML =
        `<table><tr><th>depart</th><th>min</th><th></th></tr>${rows}</table>` +
        `<div class="hint">Slowest is ${(r.worstDuration / r.bestDuration).toFixed(2)}\u00d7 ` +
        `the quietest departure.</div>`;
    log(`day scan: ${(r.bestDuration / 60).toFixed(1)} min at ` +
        `${String(Math.round(r.bestDeparture / 3600)).padStart(2, '0')}:00, ` +
        `${(r.worstDuration / 60).toFixed(1)} min at ` +
        `${String(Math.round(r.worstDeparture / 3600)).padStart(2, '0')}:00`);
}

function invalidateCH() {
    chReady = false;
    const info = $('chInfo');
    if (info) info.textContent = 'Not preprocessed for this network yet.';
    const race = $('chRaceBtn');
    if (race) race.disabled = true;
    const build = $('chBtn');
    if (build) build.textContent = 'Preprocess (build hierarchy)';
}

function runSearch(showDirs = true) {
    if (!graph.nodeCount) return;
    pause();
    heat = null;
    isoBands = null;
    markedNodes.clear();

    const closed = [...closedEdges].join(',');
    const r = call('agss_route', ['string', 'number', 'number', 'number', 'string'],
        [$('algSel').value, srcNode(), dstNode(), 1, closed]);
    if (r.ok === false) { log(r.error, 'danger'); return; }

    events = r.events || [];
    finalPath = r.path || [];
    resetState();
    stepSize = Math.max(1, Math.ceil(events.length / 400));
    setMetrics(r.metadata, events.length);

    if (!r.metadata.success) {
        log(`${r.metadata.algorithm}: no route from ${srcNode()} to ${dstNode()}`, 'danger');
        draw();
        return;
    }
    log(`${r.metadata.algorithm}: cost ${r.metadata.pathCost.toFixed(2)}, ` +
        `${r.metadata.pathLength - 1} hops, ${events.length.toLocaleString()} events`);
    if (showDirs) showDirections();
    play();
}

function showDirections() {
    if (!finalPath.length) return;
    const d = call('agss_directions', ['string'], [finalPath.join(',')]);
    if (!d.ok) return;
    const unit = geographic
        ? (d.totalDistance >= 1000 ? `${(d.totalDistance / 1000).toFixed(2)} km` : `${d.totalDistance.toFixed(0)} m`)
        : d.totalDistance.toFixed(1);
    const rows = d.steps.map((s, i) => {
        const label = nodeNames?.get(s.at)
            ?? (stations.length ? stations.find(x => x.node === s.at)?.name : null)
            ?? `node ${s.at}`;
        return `<tr><td>${i + 1}. ${s.maneuver}</td><td class="dim">${label}</td></tr>`;
    }).join('');
    $('results').innerHTML =
        `<table><tr><th>Directions (${unit})</th><th></th></tr>${rows}</table>`;
}

function race() {
    if (!graph.nodeCount) return;
    pause();
    const r = call('agss_race', ['string', 'number', 'number'], ['', srcNode(), dstNode()]);
    if (!r.ok) { log(r.error, 'danger'); return; }

    // Time each algorithm over a batch so the browser's clock clamp does not
    // flatten every small graph to 0.0 ms.
    const reps = graph.nodeCount > 20000 ? 3 : graph.nodeCount > 2000 ? 25 : 200;
    const timing = {};
    for (const x of r.rows) {
        if (x.declined) continue;
        const b = call('agss_bench', ['string', 'number', 'number', 'number'],
            [x.key, srcNode(), dstNode(), reps]);
        if (b.ok) timing[x.key] = b.amortisedMs;
    }

    const rows = r.rows.map(x => {
        const optimal = x.success && r.referenceCost >= 0 &&
            Math.abs(x.cost - r.referenceCost) < 1e-6;
        const verdict = x.declined ? '<td class="dim">declined</td>'
            : !x.success ? '<td class="dim">no path</td>'
            : optimal ? '<td class="good">optimal</td>'
            : '<td class="bad">suboptimal</td>';
        const ms = timing[x.key] !== undefined ? timing[x.key].toFixed(4) : '—';
        return `<tr><td>${x.key}</td><td>${ms}</td>` +
               `<td>${x.expanded.toLocaleString()}</td>` +
               `<td>${x.success ? x.cost.toFixed(2) : '—'}</td>${verdict}</tr>`;
    }).join('');
    $('results').innerHTML =
        `<table><tr><th>alg</th><th>ms (avg of ${reps})</th><th>expanded</th>` +
        `<th>cost</th><th>verdict</th></tr>${rows}</table>`;

    // Animate whichever optimal algorithm expanded the fewest nodes.
    const best = r.rows.filter(x => x.success && x.claimsOptimal)
                       .sort((a, b) => a.expanded - b.expanded)[0];
    if (best) {
        $('algSel').value = best.key;
        log(`race: ${r.rows.filter(x => x.success).length} found a route; ` +
            `${best.key} was leanest at ${best.expanded.toLocaleString()} expansions`);
        runSearch(false);   // keep the comparison table on screen
    }
}

function bridges() {
    pause();
    clearOverlays();
    const r = call('agss_analyze', ['string', 'number', 'number', 'number'], ['bridges', 0, 0, 0]);
    if (!r.ok) { log(r.error, 'danger'); return; }
    r.bridges.slice(0, 400).forEach(b => { markedNodes.add(b.u); markedNodes.add(b.v); });
    const rows = r.bridges.slice(0, 25).map(b =>
        `<tr><td>${b.u} — ${b.v}</td><td>${b.isolates.toLocaleString()}</td></tr>`).join('');
    $('results').innerHTML =
        `<table><tr><th>critical road</th><th>nodes cut off</th></tr>${rows}</table>`;
    log(`${r.bridgeCount.toLocaleString()} bridges, ${r.articulationCount.toLocaleString()} ` +
        `articulation points, ${r.components} components (${r.ms.toFixed(1)} ms)`);
    draw();
}

function centrality() {
    pause();
    clearOverlays();
    const samples = graph.nodeCount > 2000 ? 256 : 0;
    const r = call('agss_analyze', ['string', 'number', 'number', 'number'],
        ['centrality', 0, 0, samples]);
    if (!r.ok) { log(r.error, 'danger'); return; }
    const max = Math.max(...r.scores, 1);
    heat = r.scores.map(v => v / max);
    const top = r.scores.map((v, i) => [i, v]).sort((a, b) => b[1] - a[1]).slice(0, 15);
    $('results').innerHTML = `<table><tr><th>busiest node</th><th>score</th></tr>` +
        top.map(([i, v]) => `<tr><td>${i}</td><td>${v.toFixed(1)}</td></tr>`).join('') + '</table>';
    log(`betweenness centrality ${r.exact ? '(exact)' : `(sampled from ${r.sampled} sources)`} ` +
        `in ${r.ms.toFixed(1)} ms`);
    draw();
}

function isochrone() {
    pause();
    clearOverlays();
    // Pick bands from the graph's own scale so one control fits every network.
    const probe = call('agss_route', ['string', 'number', 'number', 'number', 'string'],
        ['dijkstra', srcNode(), dstNode(), 0, '']);
    const scale = probe.metadata?.success ? probe.metadata.pathCost : 100;
    const cuts = [scale * 0.25, scale * 0.6, scale].map(v => v.toFixed(3)).join(',');
    const r = call('agss_isochrone', ['number', 'string'], [srcNode(), cuts]);
    if (!r.ok) { log(r.error, 'danger'); return; }
    isoBands = r.bands.slice().reverse();
    const unitOf = v => geographic
        ? (stations.length ? `${(v / 60).toFixed(0)} min` : `${(v / 1000).toFixed(1)} km`)
        : v.toFixed(0);
    $('results').innerHTML = `<table><tr><th>within</th><th>reachable</th><th>share</th></tr>` +
        r.bands.map(b => `<tr><td>${unitOf(b.cutoff)}</td><td>${b.count.toLocaleString()}</td>` +
            `<td>${(100 * b.count / graph.nodeCount).toFixed(1)}%</td></tr>`).join('') + '</table>';
    log(`isochrones from node ${srcNode()} in ${r.ms.toFixed(1)} ms`);
    draw();
}

function kpaths() {
    pause();
    const r = call('agss_kpaths', ['number', 'number', 'number'], [srcNode(), dstNode(), 3]);
    if (!r.ok) { log(r.error, 'danger'); return; }
    if (!r.routes.length) { log('no route exists', 'danger'); return; }
    const best = r.routes[0].cost;
    $('results').innerHTML = `<table><tr><th>route</th><th>cost</th><th>vs best</th></tr>` +
        r.routes.map((x, i) => `<tr><td>${i + 1} (${x.path.length - 1} hops)</td>` +
            `<td>${x.cost.toFixed(2)}</td>` +
            `<td>${i ? '+' + (100 * (x.cost - best) / best).toFixed(1) + '%' : '—'}</td></tr>`).join('') +
        '</table>';
    events = [];
    finalPath = r.routes[0].path;
    resetState();
    state.head = -1;
    log(`${r.routes.length} alternative route(s) in ${r.ms.toFixed(1)} ms`);
    draw();
}

function closeWorstRoad() {
    if (finalPath.length < 2) { log('run a search first', 'danger'); return; }
    const mid = Math.floor(finalPath.length / 2);
    const u = finalPath[mid - 1], v = finalPath[mid];
    closedEdges.add(`${u}:${v}`);
    log(`closed the road ${u} — ${v}; re-routing`);
    runSearch();
}

/* -------------------------------------------------------------- interaction */

let dragging = false, dragStart = null;

cv.addEventListener('mousedown', e => {
    if (e.button !== 0) return;
    dragging = true;
    dragStart = { x: e.clientX, y: e.clientY, cx: camera.x, cy: camera.y, moved: false };
});
window.addEventListener('mousemove', e => {
    if (!dragging) return;
    const dx = e.clientX - dragStart.x, dy = e.clientY - dragStart.y;
    if (Math.abs(dx) + Math.abs(dy) > 3) dragStart.moved = true;
    const dpr = Math.min(window.devicePixelRatio || 1, 2);
    camera.x = dragStart.cx - dx * dpr / camera.zoom;
    camera.y = dragStart.cy - yDir() * dy * dpr / camera.zoom;
    requestAnimationFrame(draw);
});
window.addEventListener('mouseup', e => {
    if (dragging && !dragStart.moved) snapEndpoint(e, 'srcIn');
    dragging = false;
});
cv.addEventListener('contextmenu', e => { e.preventDefault(); snapEndpoint(e, 'dstIn'); });

/** Snap a click to the nearest node. On a 48k-node city this is the k-d tree
    earning its place: a linear scan per click would be visible. */
function snapEndpoint(e, field) {
    if (!graph.nodeCount) return;
    const rect = cv.getBoundingClientRect();
    const dpr = Math.min(window.devicePixelRatio || 1, 2);
    const [wx, wy] = inv((e.clientX - rect.left) * dpr, (e.clientY - rect.top) * dpr);
    // The engine indexes the graph's own coordinates, so undo the projection.
    const gx = geographic ? invMercX(wx) : wx;
    const gy = geographic ? invMercY(wy) : wy;
    let node;
    if (stations.length) {
        // Multi-modal/rail graphs have no k-d tree; scan in world space.
        let best = Infinity;
        for (let i = 0; i < world.x.length; i++) {
            const d = (world.x[i] - wx) ** 2 + (world.y[i] - wy) ** 2;
            if (d < best) { best = d; node = i; }
        }
    } else {
        const r = call('agss_nearest', ['number', 'number'], [gx, gy]);
        if (!r.ok) return;
        node = r.node;
    }
    $(field).value = node;
    draw();
}

cv.addEventListener('wheel', e => {
    e.preventDefault();
    const rect = cv.getBoundingClientRect();
    const dpr = Math.min(window.devicePixelRatio || 1, 2);
    const [bx, by] = inv((e.clientX - rect.left) * dpr, (e.clientY - rect.top) * dpr);
    camera.zoom = clampZoom(camera.zoom * Math.exp(-e.deltaY * 0.0015));
    const [ax, ay] = inv((e.clientX - rect.left) * dpr, (e.clientY - rect.top) * dpr);
    camera.x += bx - ax;
    camera.y += by - ay;
    requestAnimationFrame(draw);
}, { passive: false });

window.addEventListener('resize', resize);

/* -------------------------------------------------------------------- boot */

(async function boot() {
    try {
        M = await createAgssEngine();
    } catch (err) {
        $('boot').innerHTML = `<div style="max-width:520px"><b>Could not start the engine.</b>
            <p style="color:var(--muted)">${err.message}</p></div>`;
        return;
    }

    const algs = call('agss_algorithms', [], []).algorithms;
    $('algSel').innerHTML = algs
        .map(a => `<option value="${a.key}">${a.name}</option>`).join('');
    $('algSel').value = 'astar';
    const showAlgInfo = () => {
        const a = algs.find(x => x.key === $('algSel').value);
        $('algInfo').textContent = `${a.time} time, ${a.space} space — ` +
            (a.optimal ? 'guarantees the optimum' : 'not guaranteed optimal');
    };
    $('algSel').onchange = showAlgInfo;
    showAlgInfo();

    let html = '', open = false;
    for (const c of CATALOG) {
        if (c.group) {
            if (open) html += '</optgroup>';
            html += `<optgroup label="${c.group}">`;
            open = true;
        } else {
            html += `<option value="${c.id}">${c.label} — ${c.sub}</option>`;
        }
    }
    if (open) html += '</optgroup>';
    $('mapSel').innerHTML = html;
    $('mapSel').value = 'Jaipur';
    $('mapSel').onchange = loadSelected;
    $('baseSel').onchange = () => setBasemap($('baseSel').value);

    $('runBtn').onclick = runSearch;
    $('playBtn').onclick = play;
    $('pauseBtn').onclick = pause;
    $('stepBtn').onclick = step;
    $('raceBtn').onclick = race;
    $('bridgeBtn').onclick = bridges;
    $('centBtn').onclick = centrality;
    $('isoBtn').onclick = isochrone;
    $('kpathBtn').onclick = kpaths;
    $('closeBtn').onclick = closeWorstRoad;
    $('chBtn').onclick = buildCH;
    $('chRaceBtn').onclick = raceCH;
    $('trafficBtn').onclick = routeAtHour;
    $('scanBtn').onclick = scanDay;
    $('hourSlider').oninput = () => {
        $('hourInfo').innerHTML =
            `Departing at <b>${String($('hourSlider').value).padStart(2, '0')}:00</b>`;
    };
    $('clearBtn').onclick = () => { clearOverlays(); draw(); };
    $('speed').oninput = () => { if (timer) { pause(); play(); } };

    setBasemap($('baseSel').value);
    $('boot').classList.add('hidden');
    // The pane can still be laying out; keep trying briefly rather than
    // silently leaving the canvas at its 300x150 default. setTimeout rather
    // than requestAnimationFrame, which is paused while the page is hidden.
    for (let i = 0; i < 30 && !resize(); i++) {
        await new Promise(r => setTimeout(r, 50));
    }
    await loadSelected();
    log('engine ready — click to set the source, right click for the target');
})();
