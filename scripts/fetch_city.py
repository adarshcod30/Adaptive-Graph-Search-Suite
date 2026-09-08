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
import time

EARTH_R = 6371008.8

ALLOW_EMPTY_TILES = False

# 5 decimal places is ~1.1 m at the equator, and edge lengths are stored to the
# centimetre. Both are far finer than the positional accuracy of OSM road
# geometry, and the extra digits cost real megabytes once a city runs to half a
# million junctions -- Delhi NCR alone is 38 MB at this precision.
COORD_DP = 5
WEIGHT_DP = 2
# Rotate across public mirrors. A run that pulls several whole-city extracts
# will exhaust one instance's slot allowance and start getting XML errors back
# instead of JSON; spreading the load fixes that and survives one going down.
OVERPASS_MIRRORS = [
    # Ordered by how they actually behaved while building this dataset. The
    # main instance (overpass-api.de) starts refusing connections outright
    # after a run of whole-city extracts, so it sits last rather than first.
    "https://overpass.private.coffee/api/interpreter",
    "https://overpass.kumi.systems/api/interpreter",
    "https://overpass.osm.ch/api/interpreter",
    "https://overpass-api.de/api/interpreter",
]
OVERPASS = OVERPASS_MIRRORS[0]

DRIVABLE = ("motorway|trunk|primary|secondary|tertiary|unclassified|residential|"
            "living_street|motorway_link|trunk_link|primary_link|secondary_link|tertiary_link")

# The inter-city skeleton: expressways and national highways only. Roughly
# 189k ways across India, which collapses to a graph small enough to ship and
# large enough that Contraction Hierarchies genuinely earn their keep.
MAJOR = "motorway|trunk|motorway_link|trunk_link"

# south, west, north, east. Kept deliberately modest so a fetch finishes in
# seconds; widen for a full-city extract.
# Whole-city extents, not a downtown crop. The first pass used tight central
# boxes and the result was a Delhi without its airport and a Jaipur missing
# most of its own suburbs -- fine for a demo, useless for routing.
CITIES = {
    # Delhi NCT end to end, so IGI Airport (28.556, 77.100) is inside it.
    "delhi":      (28.40, 76.83, 28.90, 77.36, "Delhi"),
    # The wider capital region: Gurugram, Noida, Faridabad, Ghaziabad.
    "delhi-ncr":  (28.20, 76.75, 28.95, 77.60, "Delhi_NCR"),
    "mumbai":     (18.89, 72.77, 19.28, 73.02, "Mumbai"),
    "bengaluru":  (12.83, 77.45, 13.15, 77.78, "Bengaluru"),
    "jaipur":     (26.72, 75.62, 27.05, 75.98, "Jaipur"),
    "kolkata":    (22.44, 88.24, 22.68, 88.46, "Kolkata"),
    "chennai":    (12.90, 80.08, 13.28, 80.35, "Chennai"),
    "gorakhpur":  (26.66, 83.26, 26.86, 83.48, "Gorakhpur"),
    "hyderabad":  (17.25, 78.30, 17.56, 78.62, "Hyderabad"),
    "pune":       (18.44, 73.76, 18.64, 73.99, "Pune"),
    "ahmedabad":  (22.95, 72.47, 23.13, 72.70, "Ahmedabad"),
    "kochi":      (9.88, 76.22, 10.08, 76.40, "Kochi"),
}


def haversine(lat1, lon1, lat2, lon2):
    p1, p2 = math.radians(lat1), math.radians(lat2)
    dp, dl = math.radians(lat2 - lat1), math.radians(lon2 - lon1)
    a = math.sin(dp / 2) ** 2 + math.cos(p1) * math.cos(p2) * math.sin(dl / 2) ** 2
    return 2 * EARTH_R * math.asin(math.sqrt(min(1.0, a)))


# Overpass refuses or times out on a single request much past this, and a
# whole-region box like Delhi NCR (279k ways) is well past it. Bigger areas are
# split into tiles and merged.
MAX_TILE_DEG2 = 0.06


def fetch_tiled(bbox, classes: str = DRIVABLE) -> dict:
    """Fetch a bbox, splitting it into tiles when it is too large to serve.

    Ways that straddle a tile edge come back in both tiles; merging by element
    id makes that harmless, and `node(w)` fetches each way's full geometry
    regardless of which tile it was requested from, so nothing is clipped.
    """
    s_, w_, n_, e_ = bbox
    area = (n_ - s_) * (e_ - w_)
    if area <= MAX_TILE_DEG2:
        return fetch(bbox, classes)

    cols = max(1, math.ceil(math.sqrt(area / MAX_TILE_DEG2)))
    rows = cols
    print(f"  area {area:.3f} deg^2 exceeds one request; splitting into "
          f"{rows}x{cols} tiles", file=sys.stderr)

    merged: dict[int, dict] = {}
    for r in range(rows):
        for c in range(cols):
            tile = (s_ + (n_ - s_) * r / rows, w_ + (e_ - w_) * c / cols,
                    s_ + (n_ - s_) * (r + 1) / rows, w_ + (e_ - w_) * (c + 1) / cols)
            index = r * cols + c + 1
            print(f"    tile {index}/{rows * cols} ...", file=sys.stderr)

            # A tile can come back as valid JSON carrying no elements at all --
            # Overpass does this when a query is cut short server-side. Merging
            # that silently produced a graph with a hole in it: the Delhi NCR
            # extract once had zero nodes over Connaught Place while every
            # surrounding tile was dense. Nothing downstream catches it, since
            # the result is still a well-formed connected-enough graph, so the
            # only place to notice is here.
            part = fetch(tile, classes)
            ways = sum(1 for el in part.get("elements", []) if el.get("type") == "way")
            if ways == 0 and not ALLOW_EMPTY_TILES:
                for retry in range(1, 4):
                    print(f"      tile {index} came back empty; refetching "
                          f"({retry}/3)", file=sys.stderr)
                    time.sleep(30 * retry)
                    part = fetch(tile, classes)
                    ways = sum(1 for el in part.get("elements", [])
                               if el.get("type") == "way")
                    if ways:
                        break
                else:
                    raise SystemExit(
                        f"tile {index}/{rows * cols} {tile} returned no roads after "
                        f"3 refetches. Writing the merge now would leave a hole in "
                        f"the middle of the extract. Re-run when Overpass recovers, "
                        f"or pass --allow-empty-tiles if this area really has none.")
            for el in part.get("elements", []):
                key = el["id"] if el.get("type") == "way" else -el["id"]
                merged.setdefault(key, el)
    print(f"  merged {len(merged)} unique elements across tiles", file=sys.stderr)
    return {"elements": list(merged.values())}


def fetch(bbox, classes: str = DRIVABLE, country: str | None = None) -> dict:
    """Fetch via curl.

    urllib is avoided deliberately: several Python builds ship without a usable
    CA bundle, and curl uses the system trust store.
    """
    if country:
        query = f"""
[out:json][timeout:1800];
area["ISO3166-1"="{country}"][admin_level=2]->.in;
way(area.in)["highway"~"^({classes})$"];
out body;
node(w);
out skel qt;
"""
    else:
        s, w, n, e = bbox
        query = f"""
[out:json][timeout:900];
way["highway"~"^({classes})$"]({s},{w},{n},{e});
out body;
node(w);
out skel qt;
"""
    # Overpass rejects requests when it is busy, and a single attempt turned
    # that into a silently missing city: the loop printed a header, wrote
    # nothing, and moved on. Retry, then fail loudly.
    last = ""
    for attempt in range(1, 9):
        endpoint = OVERPASS_MIRRORS[(attempt - 1) % len(OVERPASS_MIRRORS)]
        proc = subprocess.run(
            ["curl", "-s", "--max-time", "2400", "-X", "POST",
             "--data-urlencode", f"data={query}",
             "-H", "User-Agent: agss-city-import", endpoint],
            capture_output=True, text=True)
        if proc.returncode == 0:
            try:
                return json.loads(proc.stdout)
            except json.JSONDecodeError:
                last = f"non-JSON response: {proc.stdout[:200]}"
        else:
            last = f"curl exit {proc.returncode}: {proc.stderr[:200]}"
        # Pulling several whole-city extracts exhausts every public mirror's
        # slot allowance at once, and they stay closed for a few minutes.
        # Back off far enough to actually outlast that rather than burning
        # attempts against a door that is still shut.
        wait = min(300, 30 * attempt)
        host = endpoint.split("/")[2]
        print(f"    attempt {attempt} on {host} failed ({last[:80]}); "
              f"retrying in {wait}s", file=sys.stderr)
        time.sleep(wait)
    raise SystemExit(f"Overpass failed after 8 attempts: {last}")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--city", choices=sorted(CITIES))
    ap.add_argument("--highways", metavar="CC",
                    help="fetch the expressway and national-highway skeleton for a "
                         "country by ISO code, e.g. --highways IN")
    ap.add_argument("--bbox", help="south,west,north,east")
    ap.add_argument("--name", help="output directory name")
    ap.add_argument("--out-root", default="data/cities")
    ap.add_argument("--cache", help="use a saved Overpass JSON instead of querying")
    ap.add_argument("--save-raw")
    ap.add_argument("--allow-empty-tiles", action="store_true",
                    help="accept tiles that contain no roads instead of failing; "
                         "only correct for genuinely roadless areas")
    args = ap.parse_args()

    global ALLOW_EMPTY_TILES
    ALLOW_EMPTY_TILES = args.allow_empty_tiles

    classes = DRIVABLE
    country = None
    if args.highways:
        country = args.highways.upper()
        classes = MAJOR
        bbox = None
        name = args.name or f"{country}_Highways"
    elif args.city:
        s, w, n, e, default_name = CITIES[args.city]
        bbox = (s, w, n, e)
        name = args.name or default_name
    elif args.bbox:
        bbox = tuple(float(x) for x in args.bbox.split(","))
        name = args.name or "Custom_Area"
    else:
        ap.error("pass --city, --bbox or --highways")

    if args.cache:
        payload = json.load(open(args.cache))
    else:
        target = f"country {country}" if country else str(bbox)
        print(f"fetching {name} ({target}) from Overpass ...", file=sys.stderr)
        payload = (fetch(bbox, classes, country) if country
                   else fetch_tiled(bbox, classes))
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
        json.dump({"name": name, "bbox": list(bbox) if bbox else None,
                   "country": country, "classes": classes,
                   "source": "OpenStreetMap via Overpass",
                   "licence": "ODbL 1.0", "nodes": len(kept), "edges": len(edges),
                   "raw_osm_nodes": len(coords), "ways": len(ways)}, fh, indent=2)

    print(f"wrote {len(kept)} junction nodes and {len(edges)} directed edges to {out_dir}",
          file=sys.stderr)
    print(f"  reduced from {len(coords)} raw OSM nodes "
          f"({100 * (1 - len(kept) / max(1, len(coords))):.0f}% collapsed)", file=sys.stderr)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
