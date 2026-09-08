#!/usr/bin/env python3
"""Build a routable drivable road graph for an Indian city from OpenStreetMap.

Produces geographic nodes.csv (id,lon,lat) and edges.csv (u,v,metres), which
`agss --geo` consumes directly.

Two reductions keep the output usable:
  * only nodes shared by more than one way, plus way endpoints, become graph
    nodes -- interior geometry points are collapsed into the edge length, which
    typically removes ~85% of raw OSM nodes without changing any route;
  * one-way tags are honoured, so the directed graph reflects real driving.

Usage:
    python3 scripts/fetch_city.py --city delhi
    python3 scripts/fetch_city.py --bbox 28.55,77.15,28.70,77.30 --name My_Area
    python3 scripts/fetch_city.py --city bengaluru --cache raw.json
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

# ~11 cm at the equator: far finer than any road geometry, and small enough
# that the files stay compact.
COORD_DP = 6
WEIGHT_DP = 3
OVERPASS = "https://overpass-api.de/api/interpreter"

DRIVABLE = ("motorway|trunk|primary|secondary|tertiary|unclassified|residential|"
            "living_street|motorway_link|trunk_link|primary_link|secondary_link|tertiary_link")

# south, west, north, east. Kept deliberately modest so a fetch finishes in
# seconds; widen for a full-city extract.
CITIES = {
    "delhi":      (28.56, 77.17, 28.68, 77.28, "Delhi"),
    "mumbai":     (18.92, 72.80, 19.06, 72.89, "Mumbai"),
    "bengaluru":  (12.93, 77.56, 13.03, 77.66, "Bengaluru"),
    "jaipur":     (26.86, 75.75, 26.94, 75.85, "Jaipur"),
    "kolkata":    (22.51, 88.32, 22.61, 88.40, "Kolkata"),
    "chennai":    (13.02, 80.20, 13.12, 80.29, "Chennai"),
    "hyderabad":  (17.38, 78.42, 17.47, 78.52, "Hyderabad"),
    "pune":       (18.48, 73.82, 18.57, 73.92, "Pune"),
    "ahmedabad":  (23.00, 72.55, 23.09, 72.63, "Ahmedabad"),
    "kochi":      (9.93, 76.26, 10.02, 76.34, "Kochi"),
}


def haversine(lat1, lon1, lat2, lon2):
    p1, p2 = math.radians(lat1), math.radians(lat2)
    dp, dl = math.radians(lat2 - lat1), math.radians(lon2 - lon1)
    a = math.sin(dp / 2) ** 2 + math.cos(p1) * math.cos(p2) * math.sin(dl / 2) ** 2
    return 2 * EARTH_R * math.asin(math.sqrt(min(1.0, a)))


def fetch(bbox) -> dict:
    """Fetch via curl.

    urllib is avoided deliberately: several Python builds ship without a usable
    CA bundle, and curl uses the system trust store.
    """
    s, w, n, e = bbox
    query = f"""
[out:json][timeout:600];
way["highway"~"^({DRIVABLE})$"]({s},{w},{n},{e});
out body;
node(w);
out skel qt;
"""
    proc = subprocess.run(
        ["curl", "-s", "--max-time", "700", "-X", "POST",
         "--data-urlencode", f"data={query}",
         "-H", "User-Agent: agss-city-import", OVERPASS],
        capture_output=True, text=True)
    if proc.returncode != 0:
        raise SystemExit(f"curl failed: {proc.stderr[:400]}")
    try:
        return json.loads(proc.stdout)
    except json.JSONDecodeError:
        raise SystemExit(f"Overpass returned non-JSON: {proc.stdout[:400]}")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--city", choices=sorted(CITIES))
    ap.add_argument("--bbox", help="south,west,north,east")
    ap.add_argument("--name", help="output directory name")
    ap.add_argument("--out-root", default="data/cities")
    ap.add_argument("--cache", help="use a saved Overpass JSON instead of querying")
    ap.add_argument("--save-raw")
    args = ap.parse_args()

    if args.city:
        s, w, n, e, default_name = CITIES[args.city]
        bbox = (s, w, n, e)
        name = args.name or default_name
    elif args.bbox:
        bbox = tuple(float(x) for x in args.bbox.split(","))
        name = args.name or "Custom_Area"
    else:
        ap.error("pass --city or --bbox")

    if args.cache:
        payload = json.load(open(args.cache))
    else:
        print(f"fetching {name} {bbox} from Overpass ...", file=sys.stderr)
        payload = fetch(bbox)
        if args.save_raw:
            json.dump(payload, open(args.save_raw, "w"))

    elements = payload.get("elements", [])
    # Round coordinates *before* deriving any distance, so the file is
    # self-consistent: every weight is computed from exactly the numbers the
    # CSV stores. Writing full-precision weights next to rounded coordinates
    # left ~18k Jaipur edges up to 13 cm shorter than the straight line between
    # their stored endpoints, which is enough to make the A* heuristic an
    # over-estimate and cost it the optimality guarantee.
    coords = {e["id"]: (round(e["lat"], COORD_DP), round(e["lon"], COORD_DP))
              for e in elements if e.get("type") == "node"}
    ways = [e for e in elements if e.get("type") == "way"]
    print(f"  {len(ways)} ways, {len(coords)} raw nodes", file=sys.stderr)
    if not ways:
        print("no drivable ways in that box", file=sys.stderr)
        return 1

    # A node is a junction if two or more ways touch it, or it terminates one.
    use_count: dict[int, int] = {}
    for way in ways:
        refs = way.get("nodes", [])
        for r in refs:
            use_count[r] = use_count.get(r, 0) + 1
    junction = set()
    for way in ways:
        refs = way.get("nodes", [])
        if not refs:
            continue
        junction.add(refs[0])
        junction.add(refs[-1])
        for r in refs[1:-1]:
            if use_count.get(r, 0) > 1:
                junction.add(r)

    edges: dict[tuple[int, int], float] = {}
    for way in ways:
        refs = [r for r in way.get("nodes", []) if r in coords]
        if len(refs) < 2:
            continue
        tags = way.get("tags", {})
        oneway = tags.get("oneway", "no")
        forward_only = oneway in ("yes", "true", "1") or tags.get("junction") == "roundabout"
        reverse_only = oneway == "-1"

        run_start = refs[0]
        run_len = 0.0
        for a, b in zip(refs, refs[1:]):
            run_len += haversine(*coords[a], *coords[b])
            if b in junction:
                if run_start != b and run_len > 0:
                    key = (run_start, b)
                    if not reverse_only:
                        edges[key] = min(edges.get(key, run_len), run_len)
                    rkey = (b, run_start)
                    if not forward_only:
                        edges[rkey] = min(edges.get(rkey, run_len), run_len)
                run_start = b
                run_len = 0.0

    kept = sorted({n for pair in edges for n in pair})
    remap = {osm: i for i, osm in enumerate(kept)}
    out_dir = os.path.join(args.out_root, name)
    os.makedirs(out_dir, exist_ok=True)

    with open(f"{out_dir}/nodes.csv", "w", newline="") as fh:
        wr = csv.writer(fh)
        wr.writerow(["id", "lon", "lat"])   # geographic order: x=lon, y=lat
        for osm in kept:
            lat, lon = coords[osm]
            wr.writerow([remap[osm], format(lon, f".{COORD_DP}f"), format(lat, f".{COORD_DP}f")])

    with open(f"{out_dir}/edges.csv", "w", newline="") as fh:
        wr = csv.writer(fh)
        wr.writerow(["u", "v", "metres"])
        for (a, b), d in sorted(edges.items(), key=lambda kv: (remap[kv[0][0]], remap[kv[0][1]])):
            wr.writerow([remap[a], remap[b], format(d, f".{WEIGHT_DP}f")])

    with open(f"{out_dir}/manifest.json", "w") as fh:
        json.dump({"name": name, "bbox": list(bbox), "source": "OpenStreetMap via Overpass",
                   "licence": "ODbL 1.0", "nodes": len(kept), "edges": len(edges),
                   "raw_osm_nodes": len(coords), "ways": len(ways)}, fh, indent=2)

    print(f"wrote {len(kept)} junction nodes and {len(edges)} directed edges to {out_dir}",
          file=sys.stderr)
    print(f"  reduced from {len(coords)} raw OSM nodes "
          f"({100 * (1 - len(kept) / max(1, len(coords))):.0f}% collapsed)", file=sys.stderr)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
