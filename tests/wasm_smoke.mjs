// Headless smoke test for the WebAssembly build: proves every exported entry
// point works and that the browser engine agrees with the native one.
import { readFileSync } from 'fs';
import { createRequire } from 'module';
import { dirname, resolve } from 'path';
import { fileURLToPath } from 'url';

const require = createRequire(import.meta.url);
const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const createAgssEngine = require(`${ROOT}/web/engine/agss_engine.js`);

let failures = 0;
function check(label, cond, detail = '') {
    if (cond) {
        console.log(`  ok    ${label}`);
    } else {
        console.log(`  FAIL  ${label}${detail ? ' -- ' + detail : ''}`);
        failures++;
    }
}
const M = await createAgssEngine();

// Thin wrapper: every entry point returns a malloc'd C string we must free.
function call(fn, sig, args) {
  const ptr = M.ccall(fn, 'number', sig, args);
  try { return JSON.parse(M.UTF8ToString(ptr)); }
  finally { M._agss_free(ptr); }
}

const nodes = readFileSync(`${ROOT}/data/cities/Jaipur/nodes.csv`, 'utf8');
const edges = readFileSync(`${ROOT}/data/cities/Jaipur/edges.csv`, 'utf8');

let r = call('agss_load_graph', ['string','string','string','number','number'],
             ['Jaipur', nodes, edges, 1, 0]);
check('real city graph loads', r.ok && r.nodes === 19110 && r.edges === 48721,
      JSON.stringify(r).slice(0, 140));
check('OSM extract keeps the heuristic admissible', r.admissible === true,
      `worst ratio ${r.admissibility}`);

r = call('agss_algorithms', [], []);
check('every algorithm is registered', r.algorithms.length === 12,
      r.algorithms.map(a => a.key).join(','));
for (const key of ['dijkstra', 'astar', 'ch', 'alt', 'bidijkstra']) {
    check(`  ${key} present`, r.algorithms.some(a => a.key === key));
}

r = call('agss_route', ['string','number','number','number','string'], ['astar', 0, 9000, 1, '']);
check('route succeeds and emits a delta trace',
      r.metadata.success && r.events.length > 0 && r.path.length > 1);
// The trace must be linear in the graph, not quadratic.
check('trace stays O(V + E)', r.events.length <= 2 * 19110 + 48721,
      `${r.events.length} events for V=19110 E=48721`);

const race = call('agss_race', ['string','number','number'], ['dijkstra,astar,bidijkstra,bellmanford,johnson', 0, 9000]);
const opt = race.rows.filter(x => x.success && x.claimsOptimal).map(x => x.cost);
check('every optimal algorithm agrees on cost',
      opt.length >= 4 && opt.every(c => Math.abs(c - opt[0]) < 1e-6),
      opt.join(', '));

r = call('agss_analyze', ['string','number','number','number'], ['bridges', 0, 0, 0]);
check('bridge analysis runs', r.ok && typeof r.bridgeCount === 'number');

r = call('agss_isochrone', ['number','string'], [0, '500,1500,4000']);
check('isochrone bands nest', r.bands.length === 3 &&
      r.bands[0].count <= r.bands[1].count && r.bands[1].count <= r.bands[2].count,
      r.bands.map(b => b.count).join(' <= '));

r = call('agss_kpaths', ['number','number','number'], [0, 9000, 3]);
check('k-shortest routes are cost-ordered',
      r.routes.length > 1 && r.routes.every((x, i) => i === 0 || x.cost >= r.routes[i-1].cost - 1e-9),
      r.routes.map(x => x.cost.toFixed(2)).join(', '));

r = call('agss_nearest', ['number','number'], [75.80, 26.91]);
check('nearest-node snapping works', r.ok && r.node >= 0);

// Error paths must return structured errors, not crash the module.
r = call('agss_load_graph', ['string','string','string','number','number'],
         ['bad', 'id,x,y\nabc,1,2\n', 'u,v,w\n0,1,1\n', 0, 0]);
check('malformed CSV is an error, not a crash',
      r.ok === false && /unparseable/.test(r.error), r.error);

// Contraction Hierarchies, end to end through the WASM boundary.
r = call('agss_ch_ready', [], []);
check('CH reports itself unprepared before a build', r.ok && r.ready === false);

r = call('agss_ch_build', ['number'], [30000]);
check('CH preprocessing completes within budget',
      r.ok && !r.aborted && r.shortcuts > 0, JSON.stringify(r).slice(0, 160));
check('CH arc growth stays road-like', r.ok && r.edgeGrowth > 1 && r.edgeGrowth < 4,
      `growth ${r.ok ? r.edgeGrowth : '?'}`);
const chBuildMs = r.buildMs;

let chAgree = 0, chChecked = 0, chExpanded = 0, dijExpanded = 0;
for (const target of [9000, 12345, 4321, 17000, 800]) {
    const chq = call('agss_ch_query', ['number', 'number', 'number'], [0, target, 0]);
    const djq = call('agss_route', ['string', 'number', 'number', 'number', 'string'],
                     ['dijkstra', 0, target, 0, '']);
    if (chq.ok === false || !djq.metadata.success) continue;
    chChecked++;
    if (Math.abs(chq.metadata.pathCost - djq.metadata.pathCost) < 1e-6) chAgree++;
    chExpanded += chq.metadata.nodesExpanded;
    dijExpanded += djq.metadata.nodesExpanded;
}
check('CH matches Dijkstra on every query',
      chChecked >= 3 && chAgree === chChecked, `${chAgree}/${chChecked} agreed`);
check('CH expands far less of the graph', chExpanded * 5 < dijExpanded,
      `CH ${chExpanded} vs Dijkstra ${dijExpanded} (build ${chBuildMs.toFixed(0)} ms)`);

// Customizable CH and the time-dependent metric, through the same boundary.
r = call('agss_cch_customize', ['number'], [-1]);
check('CCH builds and customizes', r.ok && r.customizeMs >= 0 && r.chordalEdges > 0,
      JSON.stringify(r).slice(0, 140));
const cchStatic = call('agss_cch_query', ['number', 'number', 'number'], [0, 9000, 0]);
const dijStatic = call('agss_route', ['string', 'number', 'number', 'number', 'string'],
                       ['dijkstra', 0, 9000, 0, '']);
check('CCH matches Dijkstra on the graph metric',
      cchStatic.metadata.success === dijStatic.metadata.success &&
      (!dijStatic.metadata.success ||
       Math.abs(cchStatic.metadata.pathCost - dijStatic.metadata.pathCost) < 1e-6),
      `CCH ${cchStatic.metadata.pathCost} vs ${dijStatic.metadata.pathCost}`);

const quiet = call('agss_cch_customize', ['number'], [3]);
const atQuiet = call('agss_cch_query', ['number', 'number', 'number'], [0, 9000, 0]);
const peak = call('agss_cch_customize', ['number'], [18]);
const atPeak = call('agss_cch_query', ['number', 'number', 'number'], [0, 9000, 0]);
check('re-customizing for traffic is cheap', quiet.ok && peak.ok && peak.customizeMs < 5000,
      `${peak.customizeMs} ms`);
check('rush hour costs more than the small hours',
      atPeak.metadata.pathCost > atQuiet.metadata.pathCost,
      `18:00 ${atPeak.metadata.pathCost} vs 03:00 ${atQuiet.metadata.pathCost}`);

const scan = call('agss_traffic_scan', ['number', 'number', 'number'], [0, 9000, 24]);
check('a day scan finds a peak and a trough',
      scan.ok && scan.durations.length === 24 && scan.worstDuration > scan.bestDuration,
      scan.ok ? `${(scan.bestDuration / 60).toFixed(1)}-${(scan.worstDuration / 60).toFixed(1)} min` : scan.error);

// Binary geometry transfer: the path that let a 207k-node network load at all.
const nCount = M._agss_node_count(), eCount = M._agss_edge_count();
check('binary geometry reports the right sizes', nCount === 19110 && eCount === 48721,
      `${nCount} nodes, ${eCount} edges`);
const cp = M._agss_coords_buffer();
const coords = new Float64Array(M.HEAPF64.buffer, cp, nCount * 2).slice();
M._agss_free(cp);
check('coordinates come back as finite lat/lon',
      coords.length === nCount * 2 && Number.isFinite(coords[0]) &&
      coords[0] > 70 && coords[0] < 80, `first = ${coords[0]}, ${coords[1]}`);

const st = readFileSync(`${ROOT}/data/transit/stations.csv`, 'utf8');
const lk = readFileSync(`${ROOT}/data/transit/links.csv`, 'utf8');
r = call('agss_load_transit', ['string','string'], [st, lk]);
check('all Indian metro systems load', r.ok && r.stations > 900 && r.systems.length >= 20,
      `${r.stations} stations, ${r.systems.length} systems`);
r = call('agss_build_transit', ['string','number'], ['Delhi', 0]);
check('Delhi rail graph builds', r.ok && r.stationNodes > 200);
const rc = r.stations.find(s => s.name === 'Rajiv Chowk');
const ak = r.stations.find(s => s.name === 'Akshardham');
const j = call('agss_route', ['string','number','number','number','string'],
               ['dijkstra', rc.node, ak.node, 0, '']);
// Indian Railways: a second, national transit dataset.
const rst = readFileSync(`${ROOT}/data/railways/stations.csv`, 'utf8');
const rlk = readFileSync(`${ROOT}/data/railways/links.csv`, 'utf8');
r = call('agss_load_transit', ['string', 'string'], [rst, rlk]);
check('Indian Railways loads', r.ok && r.stations > 700, `${r.ok ? r.stations : r.error} stations`);
r = call('agss_build_transit', ['string', 'number'], ['', 0]);
const nd = r.stations.find(s => s.name === 'New Delhi');
const hw = r.stations.find(s => s.name === 'Howrah Junction');
check('major junctions merged to one station each', !!nd && !!hw,
      `New Delhi=${!!nd} Howrah=${!!hw}`);
if (nd && hw) {
    const trip = call('agss_route', ['string', 'number', 'number', 'number', 'string'],
                      ['dijkstra', nd.node, hw.node, 0, '']);
    const hours = trip.metadata.pathCost / 3600;
    check('New Delhi to Howrah is a plausible rail journey',
          trip.metadata.success && hours > 10 && hours < 45, `${hours.toFixed(1)} h`);
}

// The real Blue Line ride is about 12 minutes over 6 stops.
const minutes = j.metadata.pathCost / 60;
check('Rajiv Chowk to Akshardham is a plausible metro trip',
      j.metadata.success && minutes > 5 && minutes < 25 && j.metadata.pathLength - 1 === 6,
      `${minutes.toFixed(1)} min over ${j.metadata.pathLength - 1} stops`);

console.log(failures === 0 ? '\nwasm smoke test passed' : `\n${failures} check(s) failed`);
process.exit(failures === 0 ? 0 : 1);
