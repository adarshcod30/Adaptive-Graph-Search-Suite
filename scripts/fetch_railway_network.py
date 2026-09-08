#!/usr/bin/env python3
"""Build India's railway network from OpenStreetMap track topology.

The route-relation importer in fetch_transit.py covers named services -- 253
long-distance trains, which between them touch only a few hundred stations.
This builds the network itself instead: 56,000 track ways and 10,000 stations
and halts, collapsed into a routable graph the same way road junctions are.

Output is a road-style graph so the engine and the browser can use it directly:

    nodes.csv     id,lon,lat
    edges.csv     u,v,metres
    stations.csv  node,name          (the subset of nodes that are stations)

Usage:
    python3 scripts/fetch_railway_network.py
    python3 scripts/fetch_railway_network.py --country IN --out data/networks/India_Railways
"""
from __future__ import annotations

import argparse
import csv
import json
import math
import os
import subprocess
import sys

EARTH_R = 6371008.8
OVERPASS = "https://overpass-api.de/api/interpreter"

# Running lines only. Sidings, yards and spurs carry a `service` tag and would
# add thousands of stub edges that no train route ever uses.
TRACK = "rail|light_rail|narrow_gauge"

COORD_DP = 5
WEIGHT_DP = 2


def haversine(lat1, lon1, lat2, lon2):
    p1, p2 = math.radians(lat1), math.radians(lat2)
    dp, dl = math.radians(lat2 - lat1), math.radians(lon2 - lon1)
    a = math.sin(dp / 2) ** 2 + math.cos(p1) * math.cos(p2) * math.sin(dl / 2) ** 2
    return 2 * EARTH_R * math.asin(math.sqrt(min(1.0, a)))


def overpass(query: str) -> dict:
    proc = subprocess.run(
        ["curl", "-s", "--max-time", "2400", "-X", "POST",
         "--data-urlencode", f"data={query}",
         "-H", "User-Agent: agss-railway-import", OVERPASS],
        capture_output=True, text=True)
    if proc.returncode != 0:
        raise SystemExit(f"curl failed: {proc.stderr[:400]}")
    try:
        return json.loads(proc.stdout)
    except json.JSONDecodeError:
        raise SystemExit(f"Overpass returned non-JSON: {proc.stdout[:400]}")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--country", default="IN")
    ap.add_argument("--out", default="data/networks/India_Railways")
    ap.add_argument("--cache", help="use a saved Overpass JSON instead of querying")
    ap.add_argument("--save-raw")
    args = ap.parse_args()

    if args.cache:
        payload = json.load(open(args.cache))
    else:
        query = f"""
[out:json][timeout:1800];
area["ISO3166-1"="{args.country}"][admin_level=2]->.in;
(
  way(area.in)["railway"~"^({TRACK})$"]["service"!~"."];
  node(area.in)["railway"~"^(station|halt)$"];
);
out body;
node(w);
out skel qt;
"""
        print(f"fetching {args.country} railway network from Overpass ...", file=sys.stderr)
        payload = overpass(query)
        if args.save_raw:
            json.dump(payload, open(args.save_raw, "w"))

    elements = payload.get("elements", [])
    coords, stations, ways = {}, {}, []
    for e in elements:
        if e.get("type") == "node":
            coords[e["id"]] = (round(e["lat"], COORD_DP), round(e["lon"], COORD_DP))
            tags = e.get("tags") or {}
            if tags.get("railway") in ("station", "halt") and tags.get("name"):
                stations[e["id"]] = tags["name"].strip()
        elif e.get("type") == "way":
            ways.append(e)
    print(f"  {len(ways)} track ways, {len(coords)} raw nodes, {len(stations)} named stations",
          file=sys.stderr)
    if not ways:
        print("no track ways returned", file=sys.stderr)
        return 1

    # A node survives if a train can make a decision there or stop there:
    # a junction between ways, the end of a way, or a station.
    use_count: dict[int, int] = {}
    for w in ways:
        for r in w.get("nodes", []):
            use_count[r] = use_count.get(r, 0) + 1
    keep = set(stations)
    for w in ways:
        refs = w.get("nodes", [])
        if not refs:
            continue
        keep.add(refs[0])
        keep.add(refs[-1])
        for r in refs[1:-1]:
            if use_count.get(r, 0) > 1:
                keep.add(r)

    # Keeping only junctions strands any station sitting mid-way along a long
    # straight run: it has no graph node anywhere near it, and 7,709 of 10,210
    # stations ended up unplaced. Pin the nearest *track* node for each station
    # into the kept set, which gives every station somewhere to attach.
    CELL = 0.01  # ~1.1 km of latitude
    track_grid: dict[tuple[int, int], list[int]] = {}
    for r in use_count:
        if r not in coords:
            continue
        lat, lon = coords[r]
        track_grid.setdefault((int(lat / CELL), int(lon / CELL)), []).append(r)

    PIN_M = 400.0
    pinned = 0
    for osm in stations:
        if osm in use_count:
            continue                      # already a track node
        if osm not in coords:
            continue
        lat, lon = coords[osm]
        gy, gx = int(lat / CELL), int(lon / CELL)
        best, best_d = None, PIN_M
        for dy in (-1, 0, 1):
            for dx in (-1, 0, 1):
                for cand in track_grid.get((gy + dy, gx + dx), ()):
                    clat, clon = coords[cand]
                    d = haversine(lat, lon, clat, clon)
                    if d < best_d:
                        best, best_d = cand, d
        if best is not None:
            keep.add(best)
            pinned += 1
    print(f"  pinned {pinned} track nodes so stations have somewhere to attach",
          file=sys.stderr)

    # Track is bidirectional; collapse the geometry between kept nodes into
    # edge length, exactly as the road importer does.
    edges: dict[tuple[int, int], float] = {}
    for w in ways:
        refs = [r for r in w.get("nodes", []) if r in coords]
        if len(refs) < 2:
            continue
        run_start, run_len = refs[0], 0.0
        for a, b in zip(refs, refs[1:]):
            run_len += haversine(*coords[a], *coords[b])
            if b in keep:
                if run_start != b and run_len > 0:
                    for key in ((run_start, b), (b, run_start)):
                        edges[key] = min(edges.get(key, run_len), run_len)
                run_start, run_len = b, 0.0

    used = sorted({n for pair in edges for n in pair})
    remap = {osm: i for i, osm in enumerate(used)}
    os.makedirs(args.out, exist_ok=True)

    with open(f"{args.out}/nodes.csv", "w", newline="") as fh:
        w = csv.writer(fh)
        w.writerow(["id", "lon", "lat"])
        for osm in used:
            lat, lon = coords[osm]
            w.writerow([remap[osm], format(lon, f".{COORD_DP}f"), format(lat, f".{COORD_DP}f")])

    with open(f"{args.out}/edges.csv", "w", newline="") as fh:
        w = csv.writer(fh)
        w.writerow(["u", "v", "metres"])
        for (a, b), d in sorted(edges.items(), key=lambda kv: (remap[kv[0][0]], remap[kv[0][1]])):
            w.writerow([remap[a], remap[b], format(d, f".{WEIGHT_DP}f")])

    # Most OSM station nodes are mapped beside the track rather than on it, so
    # only a fraction land on the graph directly -- 1,218 of 10,232 on the
    # first pass. Snap the rest to the nearest kept node.
    #
    # A grid bucketed at roughly the snap radius keeps this linear: comparing
    # every station against every node would be 10k x 60k distance
    # calculations.
    CELL = 0.01  # ~1.1 km of latitude
    grid: dict[tuple[int, int], list[int]] = {}
    for osm in used:
        lat, lon = coords[osm]
        grid.setdefault((int(lat / CELL), int(lon / CELL)), []).append(osm)

    SNAP_M = 450.0
    named: list[tuple[int, str]] = []
    taken: set[int] = set()
    direct = snapped = dropped = 0
    for osm, name in stations.items():
        if osm in remap:
            named.append((remap[osm], name))
            taken.add(remap[osm])
            direct += 1
            continue
        if osm not in coords:
            dropped += 1
            continue
        lat, lon = coords[osm]
        gy, gx = int(lat / CELL), int(lon / CELL)
        best, best_d = None, SNAP_M
        for dy in (-1, 0, 1):
            for dx in (-1, 0, 1):
                for cand in grid.get((gy + dy, gx + dx), ()):
                    clat, clon = coords[cand]
                    d = haversine(lat, lon, clat, clon)
                    if d < best_d:
                        best, best_d = cand, d
        if best is None:
            dropped += 1
            continue
        node = remap[best]
        # One name per node: a second station snapping onto an already-claimed
        # node is almost always the same place tagged twice.
        if node in taken:
            dropped += 1
            continue
        taken.add(node)
        named.append((node, name))
        snapped += 1
    print(f"  stations: {direct} already on the graph, {snapped} snapped within "
          f"{SNAP_M:.0f} m, {dropped} unplaced", file=sys.stderr)
    with open(f"{args.out}/stations.csv", "w", newline="") as fh:
        w = csv.writer(fh)
        w.writerow(["node", "name"])
        for node, name in sorted(named):
            w.writerow([node, name])

    with open(f"{args.out}/manifest.json", "w") as fh:
        json.dump({"name": os.path.basename(args.out), "country": args.country,
                   "source": "OpenStreetMap via Overpass", "licence": "ODbL 1.0",
                   "track_ways": len(ways), "nodes": len(used), "edges": len(edges),
                   "stations": len(named)}, fh, indent=2)

    print(f"wrote {len(used)} nodes, {len(edges)} directed edges and {len(named)} "
          f"named stations to {args.out}", file=sys.stderr)
    print(f"  collapsed from {len(coords)} raw OSM nodes "
          f"({100 * (1 - len(used) / max(1, len(coords))):.0f}%)", file=sys.stderr)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
