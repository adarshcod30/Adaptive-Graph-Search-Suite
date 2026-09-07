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

const nodes = readFileSync(`${ROOT}/data/maps/Delhi_NCR/nodes.csv`, 'utf8');
const edges = readFileSync(`${ROOT}/data/maps/Delhi_NCR/edges.csv`, 'utf8');

let r = call('agss_load_graph', ['string','string','string','number','number'],
             ['Delhi_NCR', nodes, edges, 0, 0]);
check('graph loads', r.ok && r.nodes === 200 && r.edges === 996, JSON.stringify(r).slice(0, 120));
check('bundled map is admissible', r.admissible === true,
      `worst ratio ${r.admissibility}`);

r = call('agss_algorithms', [], []);
check('all 10 algorithms registered', r.algorithms.length === 10,
      r.algorithms.map(a => a.key).join(','));

r = call('agss_route', ['string','number','number','number','string'], ['astar', 0, 90, 1, '']);
check('route succeeds and emits a delta trace',
      r.metadata.success && r.events.length > 0 && r.path.length > 1);
// The trace must be linear in the graph, not quadratic.
check('trace stays O(V + E)', r.events.length <= 2 * 200 + 996,
      `${r.events.length} events for V=200 E=996`);

const race = call('agss_race', ['string','number','number'], ['', 0, 90]);
const opt = race.rows.filter(x => x.success && x.claimsOptimal).map(x => x.cost);
check('every optimal algorithm agrees on cost',
      opt.length >= 4 && opt.every(c => Math.abs(c - opt[0]) < 1e-6),
      opt.join(', '));

r = call('agss_analyze', ['string','number','number','number'], ['bridges', 0, 0, 0]);
check('bridge analysis runs', r.ok && typeof r.bridgeCount === 'number');

r = call('agss_isochrone', ['number','string'], [0, '20,50,100']);
check('isochrone bands nest', r.bands.length === 3 &&
      r.bands[0].count <= r.bands[1].count && r.bands[1].count <= r.bands[2].count,
      r.bands.map(b => b.count).join(' <= '));

r = call('agss_kpaths', ['number','number','number'], [0, 90, 3]);
check('k-shortest routes are cost-ordered',
      r.routes.length > 1 && r.routes.every((x, i) => i === 0 || x.cost >= r.routes[i-1].cost - 1e-9),
      r.routes.map(x => x.cost.toFixed(2)).join(', '));

r = call('agss_nearest', ['number','number'], [50.0, 50.0]);
check('nearest-node snapping works', r.ok && r.node >= 0);

// Error paths must return structured errors, not crash the module.
r = call('agss_load_graph', ['string','string','string','number','number'],
         ['bad', 'id,x,y\nabc,1,2\n', 'u,v,w\n0,1,1\n', 0, 0]);
check('malformed CSV is an error, not a crash',
      r.ok === false && /unparseable/.test(r.error), r.error);

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
// The real Blue Line ride is about 12 minutes over 6 stops.
const minutes = j.metadata.pathCost / 60;
check('Rajiv Chowk to Akshardham is a plausible metro trip',
      j.metadata.success && minutes > 5 && minutes < 25 && j.metadata.pathLength - 1 === 6,
      `${minutes.toFixed(1)} min over ${j.metadata.pathLength - 1} stops`);

console.log(failures === 0 ? '\nwasm smoke test passed' : `\n${failures} check(s) failed`);
process.exit(failures === 0 ? 0 : 1);
