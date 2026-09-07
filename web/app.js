/*
 * Browser front-end for the WebAssembly engine.
 *
 * There is no backend. The C++ engine runs in this tab, which is what removes
 * the whole class of defects the Python bridge had: no `map` parameter reaches
 * a filesystem, and no two requests can race over a shared trace file, because
 * each tab owns its own engine instance and its own memory.
 */
'use strict';

const CATALOG = [
    { id: 'Small_Campus',      label: 'Small Campus (25 nodes)',            dir: '../data/maps/Small_Campus',      geo: false },
    { id: 'Mumbai_Pune_Expy',  label: 'Mumbai-Pune Expressway (50)',        dir: '../data/maps/Mumbai_Pune_Expy',  geo: false },
    { id: 'Indian_Grid',       label: 'Indian Grid (225, integer weights)', dir: '../data/maps/Indian_Grid',       geo: false },
    { id: 'Delhi_NCR',         label: 'Delhi NCR synthetic (200)',          dir: '../data/maps/Delhi_NCR',         geo: false },
    { id: 'Bengaluru_Traffic', label: 'Bengaluru Traffic synthetic (400)',  dir: '../data/maps/Bengaluru_Traffic', geo: false },
    { id: 'Delhi_Central',     label: 'Delhi roads, real OSM (47,828)',     dir: '../data/cities/Delhi_Central',   geo: true, heavy: true },
    { id: 'transit:Delhi',     label: 'Delhi Metro (rail only)',            transit: 'Delhi' },
    { id: 'transit:Bengaluru', label: 'Namma Metro, Bengaluru (rail only)', transit: 'Bengaluru' },
    { id: 'transit:Mumbai',    label: 'Mumbai Metro + Monorail (rail)',     transit: 'Mumbai' },
    { id: 'transit:Chennai',   label: 'Chennai Metro (rail only)',          transit: 'Chennai' },
    { id: 'transit:Kolkata',   label: 'Kolkata Metro (rail only)',          transit: 'Kolkata' },
    { id: 'transit:Hyderabad', label: 'Hyderabad Metro (rail only)',        transit: 'Hyderabad' },
    { id: 'transit:',          label: 'Every Indian metro (922 stations)',  transit: '' },
];

const OP_DISCOVER = 0, OP_EXPAND = 1, OP_RELAX = 2;

const $ = id => document.getElementById(id);
const cv = $('cv'), ctx = cv.getContext('2d');

let M = null;                 // the Emscripten module
let graph = { nodes: [], edges: [], coordSpace: 'planar' };
let geographic = false;
let stations = [];            // populated for transit graphs
let transitLoaded = false;

let events = [], eventIdx = 0, finalPath = [], stepSize = 1, timer = null;
let closedEdges = new Set();
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

function log(msg, cls) {
    const el = $('log');
    const line = document.createElement('div');
    line.textContent = msg;
    if (cls) line.style.color = `var(--${cls})`;
    el.prepend(line);
    while (el.childElementCount > 40) el.lastElementChild.remove();
}

/* -------------------------------------------------------------- rendering */

function computeBounds() {
    if (!graph.nodes.length) return;
    let minx = Infinity, maxx = -Infinity, miny = Infinity, maxy = -Infinity;
    for (const n of graph.nodes) {
        if (n.x < minx) minx = n.x;
        if (n.x > maxx) maxx = n.x;
        if (n.y < miny) miny = n.y;
        if (n.y > maxy) maxy = n.y;
    }
    const padx = (maxx - minx) * 0.05 || 1, pady = (maxy - miny) * 0.05 || 1;
    bounds = { minx: minx - padx, maxx: maxx + padx, miny: miny - pady, maxy: maxy + pady };
}

function fitCamera() {
    computeBounds();
    const w = bounds.maxx - bounds.minx, h = bounds.maxy - bounds.miny;
    // Latitude and longitude are not the same distance on the ground, so a
    // geographic graph gets its aspect corrected before it is drawn.
    const aspect = geographic ? Math.cos((bounds.miny + bounds.maxy) / 2 * Math.PI / 180) : 1;
    camera.zoom = Math.min(cv.width / (w / (aspect || 1)), cv.height / h) * 0.9;
    camera.x = (bounds.minx + bounds.maxx) / 2;
    camera.y = (bounds.miny + bounds.maxy) / 2;
}

function aspectScale() {
    if (!geographic) return 1;
    return Math.cos((bounds.miny + bounds.maxy) / 2 * Math.PI / 180) || 1;
}

const tx = x => (x - camera.x) * camera.zoom / aspectScale() + cv.width / 2;
const ty = y => cv.height / 2 - (y - camera.y) * camera.zoom;
const inv = (px, py) => [
    (px - cv.width / 2) * aspectScale() / camera.zoom + camera.x,
    (cv.height / 2 - py) / camera.zoom + camera.y,
];

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
    if (!(camera.zoom > 0) && graph.nodes.length) fitCamera();
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
    if (!graph.nodes.length) return;

    const dpr = Math.min(window.devicePixelRatio || 1, 2);
    const big = graph.nodes.length > 12000;
    const r = px(camera.zoom * (geographic ? 0.0004 : 0.06), big ? 1.0 : 1.6, 5) * dpr;
    const pathWidth = px(camera.zoom * (geographic ? 0.0006 : 0.06), 2, 6) * dpr;

    // Base edges. On a large graph these are drawn as one path so the whole
    // network is a single stroke call rather than 120k of them.
    ctx.strokeStyle = 'rgba(120,150,210,0.16)';
    ctx.lineWidth = (big ? 0.5 : 1) * dpr;
    ctx.beginPath();
    for (const e of graph.edges) {
        if (e.u > e.v) continue;                 // one line per undirected pair
        const a = graph.nodes[e.u], b = graph.nodes[e.v];
        if (!a || !b) continue;
        ctx.moveTo(tx(a.x), ty(a.y));
        ctx.lineTo(tx(b.x), ty(b.y));
    }
    ctx.stroke();

    // Isochrone hulls sit under everything else.
    if (isoBands) {
        const shades = ['rgba(78,168,255,0.22)', 'rgba(78,168,255,0.14)', 'rgba(78,168,255,0.08)'];
        isoBands.forEach((band, i) => {
            if (band.hull.length < 3) return;
            ctx.fillStyle = shades[Math.min(i, shades.length - 1)];
            ctx.beginPath();
            band.hull.forEach(([x, y], j) => (j ? ctx.lineTo(tx(x), ty(y)) : ctx.moveTo(tx(x), ty(y))));
            ctx.closePath();
            ctx.fill();
        });
    }

    // Nodes, tinted by centrality when a heat map is loaded.
    if (!big || camera.zoom > 40) {
        for (let i = 0; i < graph.nodes.length; i++) {
            const n = graph.nodes[i];
            const px = tx(n.x), py = ty(n.y);
            if (px < -20 || py < -20 || px > cv.width + 20 || py > cv.height + 20) continue;
            if (heat) {
                const t = heat[i];
                ctx.fillStyle = t > 0.01
                    ? `hsl(${(1 - t) * 210}, 90%, ${35 + t * 30}%)`
                    : 'rgba(120,150,210,0.35)';
            } else {
                ctx.fillStyle = 'rgba(160,185,225,0.55)';
            }
            ctx.beginPath();
            ctx.arc(px, py, heat ? r * (0.8 + heat[i] * 2.2) : r, 0, 6.2832);
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
            const a = graph.nodes[u], b = graph.nodes[v];
            if (!a || !b) continue;
            ctx.moveTo(tx(a.x), ty(a.y));
            ctx.lineTo(tx(b.x), ty(b.y));
        }
        ctx.stroke();
    }

    // Search state.
    const done = eventIdx >= events.length;
    const path = done && finalPath.length ? finalPath : currentPath();

    if (state.explored.size) {
        ctx.fillStyle = 'rgba(251,146,60,0.65)';
        for (const id of state.explored) {
            const n = graph.nodes[id];
            if (!n) continue;
            ctx.beginPath();
            ctx.arc(tx(n.x), ty(n.y), r * 1.4, 0, 6.2832);
            ctx.fill();
        }
    }
    if (state.frontier.size) {
        ctx.fillStyle = 'rgba(217,70,239,0.9)';
        ctx.shadowColor = 'rgba(217,70,239,0.9)';
        ctx.shadowBlur = 8;
        for (const id of state.frontier) {
            const n = graph.nodes[id];
            if (!n) continue;
            ctx.beginPath();
            ctx.arc(tx(n.x), ty(n.y), r * 1.9, 0, 6.2832);
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
            const n = graph.nodes[id];
            if (!n) return;
            i ? ctx.lineTo(tx(n.x), ty(n.y)) : ctx.moveTo(tx(n.x), ty(n.y));
        });
        ctx.stroke();
        ctx.shadowBlur = 0;
    }

    // Marked nodes (bridge endpoints).
    if (markedNodes.size) {
        ctx.strokeStyle = '#f87171';
        ctx.lineWidth = 2 * dpr;
        for (const id of markedNodes) {
            const n = graph.nodes[id];
            if (!n) continue;
            ctx.beginPath();
            ctx.arc(tx(n.x), ty(n.y), r * 3, 0, 6.2832);
            ctx.stroke();
        }
    }

    // Endpoints on top.
    for (const [id, color] of [[srcNode(), '#4ea8ff'], [dstNode(), '#22c55e']]) {
        const n = graph.nodes[id];
        if (!n) continue;
        ctx.fillStyle = color;
        ctx.strokeStyle = '#fff';
        ctx.lineWidth = 2 * dpr;
        ctx.beginPath();
        ctx.arc(tx(n.x), ty(n.y), px(r * 2.2, 5 * dpr, 11 * dpr), 0, 6.2832);
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

async function ensureTransit() {
    if (transitLoaded) return;
    const [st, lk] = await Promise.all([
        fetchText('../data/transit/stations.csv'),
        fetchText('../data/transit/links.csv'),
    ]);
    const r = call('agss_load_transit', ['string', 'string'], [st, lk]);
    if (!r.ok) throw new Error(r.error);
    transitLoaded = true;
    log(`transit data: ${r.stations} stations across ${r.systems.length} systems`);
}

async function loadSelected() {
    const entry = CATALOG.find(c => c.id === $('mapSel').value);
    if (!entry) return;
    clearOverlays();
    pause();

    try {
        if (entry.transit !== undefined) {
            await ensureTransit();
            const r = call('agss_build_transit', ['string', 'number'], [entry.transit, 0]);
            if (!r.ok) throw new Error(r.error);
            stations = r.stations;
            geographic = true;
            loadGeometry();
            $('mapInfo').textContent =
                `${r.stationNodes} stations, ${r.edges} edges — travel times estimated from distance`;
            log(`${entry.label}: ${r.stationNodes} stations`);
        } else {
            if (entry.heavy) log(`fetching ${entry.label} — a few MB, one moment…`);
            const [nodes, edges] = await Promise.all([
                fetchText(`${entry.dir}/nodes.csv`),
                fetchText(`${entry.dir}/edges.csv`),
            ]);
            const r = call('agss_load_graph',
                ['string', 'string', 'string', 'number', 'number'],
                [entry.id, nodes, edges, entry.geo ? 1 : 0, 1]);
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
            log(`${entry.label}: ${r.nodes.toLocaleString()} nodes, ${r.edges.toLocaleString()} edges`);
        }

        $('srcIn').value = 0;
        $('dstIn').value = Math.min(graph.nodes.length - 1, Math.floor(graph.nodes.length * 0.7));
        $('srcIn').max = $('dstIn').max = graph.nodes.length - 1;
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
    graph = call('agss_graph_json', [], []);
    // Index by dense id so lookups during draw are array indexing, not a scan.
    const byId = new Array(graph.nodes.length);
    for (const n of graph.nodes) byId[n.id] = n;
    graph.nodes = byId;
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

function runSearch(showDirs = true) {
    if (!graph.nodes.length) return;
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
        const label = stations.length
            ? (stations.find(x => x.node === s.at)?.name ?? `node ${s.at}`)
            : `node ${s.at}`;
        return `<tr><td>${i + 1}. ${s.maneuver}</td><td class="dim">${label}</td></tr>`;
    }).join('');
    $('results').innerHTML =
        `<table><tr><th>Directions (${unit})</th><th></th></tr>${rows}</table>`;
}

function race() {
    if (!graph.nodes.length) return;
    pause();
    const r = call('agss_race', ['string', 'number', 'number'], ['', srcNode(), dstNode()]);
    if (!r.ok) { log(r.error, 'danger'); return; }

    // Time each algorithm over a batch so the browser's clock clamp does not
    // flatten every small graph to 0.0 ms.
    const reps = graph.nodes.length > 20000 ? 3 : graph.nodes.length > 2000 ? 25 : 200;
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
    const samples = graph.nodes.length > 2000 ? 256 : 0;
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
            `<td>${(100 * b.count / graph.nodes.length).toFixed(1)}%</td></tr>`).join('') + '</table>';
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
    camera.x = dragStart.cx - dx * dpr * aspectScale() / camera.zoom;
    camera.y = dragStart.cy + dy * dpr / camera.zoom;
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
    if (!graph.nodes.length) return;
    const rect = cv.getBoundingClientRect();
    const dpr = Math.min(window.devicePixelRatio || 1, 2);
    const [wx, wy] = inv((e.clientX - rect.left) * dpr, (e.clientY - rect.top) * dpr);
    let node;
    if (stations.length) {
        // Multi-modal/rail graphs have no k-d tree; scan the station list.
        let best = Infinity;
        for (const n of graph.nodes) {
            if (!n) continue;
            const d = (n.x - wx) ** 2 + (n.y - wy) ** 2;
            if (d < best) { best = d; node = n.id; }
        }
    } else {
        const r = call('agss_nearest', ['number', 'number'], [wx, wy]);
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
    camera.zoom *= Math.exp(-e.deltaY * 0.0015);
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

    $('mapSel').innerHTML = CATALOG.map(c => `<option value="${c.id}">${c.label}</option>`).join('');
    $('mapSel').value = 'Delhi_NCR';
    $('mapSel').onchange = loadSelected;

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
    $('clearBtn').onclick = () => { clearOverlays(); draw(); };
    $('speed').oninput = () => { if (timer) { pause(); play(); } };

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
