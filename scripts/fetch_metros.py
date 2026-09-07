#!/usr/bin/env python3
"""Build the bundled transit dataset from OpenStreetMap.

Queries Overpass for every rapid-transit route relation in India (metro,
monorail and light rail), extracts ordered station sequences per line, and
writes data/transit/{stations,links}.csv.

Travel time between adjacent stations is estimated from the great-circle
distance and a per-system average speed, because OSM does not carry timetables.
Timings are therefore indicative, not schedule-accurate -- see the README.

Usage:
    python3 scripts/fetch_metros.py                 # refresh from Overpass
    python3 scripts/fetch_metros.py --out data/transit
"""
from __future__ import annotations

import urllib.parse

import argparse
import csv
import json
import math
import sys
import time
import urllib.error
import urllib.request

OVERPASS = "https://overpass-api.de/api/interpreter"

# Rapid transit only. Suburban rail (Mumbai Local, Chennai MRTS) is excluded
# because OSM models it inconsistently across states.
QUERY = """
[out:json][timeout:600];
area["ISO3166-1"="IN"][admin_level=2]->.in;
(
  relation(area.in)["type"="route"]["route"="subway"];
  relation(area.in)["type"="route"]["route"="light_rail"];
  relation(area.in)["type"="route"]["route"="monorail"];
);
out body;
node(r)->.stops;
.stops out body;
"""

# Average including dwell time, metres/second.
SPEED_MPS = {"subway": 9.5, "light_rail": 8.0, "monorail": 8.0}
EARTH_R = 6371008.8

# Two nodes with the same name inside this radius are the same station.
MERGE_RADIUS_M = 400.0


def normalize_name(name: str) -> str:
    """Fold spelling variants so 'A.I.I.M.S.' and 'AIIMS' cluster together."""
    out = name.lower().strip()
    for ch in ".-\u2013\u2014_'":
        out = out.replace(ch, "")
    return " ".join(out.split())


def haversine(lat1, lon1, lat2, lon2):
    p1, p2 = math.radians(lat1), math.radians(lat2)
    dp = math.radians(lat2 - lat1)
    dl = math.radians(lon2 - lon1)
    a = math.sin(dp / 2) ** 2 + math.cos(p1) * math.cos(p2) * math.sin(dl / 2) ** 2
    return 2 * EARTH_R * math.asin(math.sqrt(min(1.0, a)))


def fetch(query: str, retries: int = 3) -> dict:
    data = urllib.parse.urlencode({"data": query}).encode()
    last = None
    for attempt in range(retries):
        try:
            req = urllib.request.Request(OVERPASS, data=data,
                                         headers={"User-Agent": "agss-transit-import"})
            with urllib.request.urlopen(req, timeout=600) as fh:
                return json.load(fh)
        except (urllib.error.URLError, TimeoutError, json.JSONDecodeError) as exc:
            last = exc
            wait = 10 * (attempt + 1)
            print(f"  attempt {attempt + 1} failed ({exc}); retrying in {wait}s",
                  file=sys.stderr)
            time.sleep(wait)
    raise SystemExit(f"Overpass request failed after {retries} attempts: {last}")


# OSM records some networks by operator code or legal entity name. Map those
# onto the names riders actually use.
SYSTEM_ALIASES = {
    "MMMOCL": ("Mumbai Monorail", "Mumbai"),
    "Madhya Pradesh Metro Rail Corporation Limited": ("Indore Metro", "Indore"),
    "Bhoj Metro": ("Bhopal Metro", "Bhopal"),
    "RapidX": ("Namo Bharat (RRTS)", "Delhi NCR"),
    "Namma Metro": ("Namma Metro", "Bengaluru"),
    "Rapid Metro Gurgaon": ("Rapid Metro Gurugram", "Gurugram"),
}


def system_name(tags: dict) -> str:
    for key in ("network", "operator"):
        if tags.get(key):
            raw = tags[key].strip()
            return SYSTEM_ALIASES.get(raw, (raw, None))[0]
    return "Unknown Network"


def city_of(tags: dict) -> str:
    for key in ("network", "operator"):
        raw = (tags.get(key) or "").strip()
        if raw in SYSTEM_ALIASES and SYSTEM_ALIASES[raw][1]:
            return SYSTEM_ALIASES[raw][1]
    for key in ("network:city", "city", "addr:city"):
        if tags.get(key):
            return tags[key].strip()
    net = system_name(tags)
    # "Delhi Metro" -> "Delhi", "Namma Metro" keeps its own name.
    for suffix in (" Metro Rail", " Metro", " Monorail", " Light Rail"):
        if net.endswith(suffix):
            return net[: -len(suffix)].strip()
    return net


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default="data/transit")
    ap.add_argument("--cache", help="read a previously saved Overpass JSON instead of querying")
    ap.add_argument("--save-raw", help="write the raw Overpass response here")
    args = ap.parse_args()

    if args.cache:
        with open(args.cache) as fh:
            payload = json.load(fh)
    else:
        print("querying Overpass for Indian rapid transit routes ...", file=sys.stderr)
        payload = fetch(QUERY)
        if args.save_raw:
            with open(args.save_raw, "w") as fh:
                json.dump(payload, fh)

    elements = payload.get("elements", [])
    nodes = {e["id"]: e for e in elements if e.get("type") == "node"}
    relations = [e for e in elements if e.get("type") == "relation"]
    print(f"  {len(relations)} route relations, {len(nodes)} member nodes", file=sys.stderr)

    stations: dict[int, dict] = {}
    links: list[dict] = []
    seen_links: set[tuple[int, int, str]] = set()

    for rel in relations:
        tags = rel.get("tags", {})
        route = tags.get("route", "subway")
        line = tags.get("name") or tags.get("ref") or "Unnamed Line"
        system = system_name(tags)
        city = city_of(tags)
        speed = SPEED_MPS.get(route, 9.0)

        ordered = []
        for m in rel.get("members", []):
            if m.get("type") != "node":
                continue
            if m.get("role", "") not in ("stop", "stop_entry_only", "stop_exit_only",
                                         "platform", "platform_entry_only",
                                         "platform_exit_only", ""):
                continue
            n = nodes.get(m["ref"])
            if not n:
                continue
            ntags = n.get("tags", {})
            name = ntags.get("name")
            if not name:
                continue
            # Collapse the stop/platform pair OSM emits for the same station.
            if ordered and ordered[-1][1] == name:
                continue
            ordered.append((n["id"], name, n["lat"], n["lon"]))

        if len(ordered) < 2:
            continue

        for nid, name, lat, lon in ordered:
            if nid not in stations:
                stations[nid] = {"id": nid, "name": name, "system": system,
                                 "city": city, "lat": lat, "lon": lon}

        for (a_id, _, a_lat, a_lon), (b_id, _, b_lat, b_lon) in zip(ordered, ordered[1:]):
            if a_id == b_id:
                continue
            key = (min(a_id, b_id), max(a_id, b_id), line)
            if key in seen_links:
                continue
            seen_links.add(key)
            metres = haversine(a_lat, a_lon, b_lat, b_lon)
            links.append({"from": a_id, "to": b_id, "line": line,
                          "seconds": round(max(45.0, metres / speed), 1)})

    if not stations:
        print("no stations extracted -- Overpass may have returned an empty set", file=sys.stderr)
        return 1

    # OSM models each platform and each direction as its own node, so a single
    # interchange such as Rajiv Chowk arrives here as four or more separate
    # stations with no edge between them. Left alone that fragments the graph
    # and makes line changes impossible, so fold nodes that share a name and
    # sit within MERGE_RADIUS_M of each other into one canonical station.
    canonical: dict[int, int] = {}
    groups: dict[tuple[str, str], list[int]] = {}
    for nid, st in stations.items():
        groups.setdefault((st["city"], normalize_name(st["name"])), []).append(nid)

    merged_away = 0
    for _key, members in groups.items():
        clusters: list[list[int]] = []
        for nid in members:
            st = stations[nid]
            placed = False
            for cl in clusters:
                head = stations[cl[0]]
                if haversine(st["lat"], st["lon"], head["lat"], head["lon"]) <= MERGE_RADIUS_M:
                    cl.append(nid)
                    placed = True
                    break
            if not placed:
                clusters.append([nid])
        for cl in clusters:
            keep = min(cl)
            # Centroid keeps the merged station between its platforms.
            stations[keep]["lat"] = sum(stations[i]["lat"] for i in cl) / len(cl)
            stations[keep]["lon"] = sum(stations[i]["lon"] for i in cl) / len(cl)
            for nid in cl:
                canonical[nid] = keep
                if nid != keep:
                    merged_away += 1

    for nid in list(stations):
        if canonical.get(nid, nid) != nid:
            del stations[nid]

    rewritten: list[dict] = []
    seen_after: set[tuple[int, int, str]] = set()
    for l in links:
        a = canonical.get(l["from"], l["from"])
        b = canonical.get(l["to"], l["to"])
        if a == b or a not in stations or b not in stations:
            continue
        key = (min(a, b), max(a, b), l["line"])
        if key in seen_after:
            continue
        seen_after.add(key)
        rewritten.append({"from": a, "to": b, "line": l["line"], "seconds": l["seconds"]})
    links = rewritten
    print(f"  merged {merged_away} duplicate platform nodes into shared stations",
          file=sys.stderr)

    # Renumber to small stable ids so the CSV stays readable and diffable.
    ordered_ids = sorted(stations, key=lambda i: (stations[i]["city"], stations[i]["system"],
                                                  stations[i]["name"], i))
    remap = {osm_id: idx for idx, osm_id in enumerate(ordered_ids, start=1)}

    import os
    os.makedirs(args.out, exist_ok=True)
    with open(f"{args.out}/stations.csv", "w", newline="") as fh:
        w = csv.writer(fh)
        w.writerow(["id", "name", "system", "city", "lat", "lon"])
        for osm_id in ordered_ids:
            s = stations[osm_id]
            w.writerow([remap[osm_id], s["name"], s["system"], s["city"],
                        f"{s['lat']:.6f}", f"{s['lon']:.6f}"])

    with open(f"{args.out}/links.csv", "w", newline="") as fh:
        w = csv.writer(fh)
        w.writerow(["from", "to", "line", "seconds"])
        for l in sorted(links, key=lambda x: (remap[x["from"]], remap[x["to"]])):
            w.writerow([remap[l["from"]], remap[l["to"]], l["line"], l["seconds"]])

    systems = sorted({s["system"] for s in stations.values()})
    cities = sorted({s["city"] for s in stations.values()})
    print(f"wrote {len(stations)} stations and {len(links)} links to {args.out}", file=sys.stderr)
    print(f"  {len(systems)} systems across {len(cities)} cities", file=sys.stderr)
    for sy in systems:
        n = sum(1 for s in stations.values() if s["system"] == sy)
        print(f"    {sy:<40} {n:>4} stations", file=sys.stderr)
    return 0


if __name__ == "__main__":
    import urllib.parse
    raise SystemExit(main())
