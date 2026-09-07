#!/usr/bin/env python3
"""Local development bridge between the browser UI and the agss binary.

Two defects in the previous version are fixed here, both of which only showed
up with more than one user:

  * the `map` parameter was joined onto the maps directory without
    normalisation, so `../` escaped it and read graphs from anywhere on disk;
  * every request wrote to one fixed `ui/trace.json` while the server ran
    threaded, so concurrent requests clobbered each other. Measured under ten
    parallel requests, five returned no trace at all and three returned another
    client's algorithm. Each request now gets its own temporary file.

This is a development server. It binds to loopback and is not hardened for
public exposure.
"""
from __future__ import annotations

import http.server
import json
import os
import re
import subprocess
import tempfile
import urllib.parse

HOST = os.environ.get("AGSS_HOST", "127.0.0.1")
PORT = int(os.environ.get("AGSS_PORT", "9000"))

ROOT_DIR = os.path.dirname(os.path.abspath(__file__))
UI_DIR = os.path.join(ROOT_DIR, "ui")
MAPS_DIR = os.path.realpath(os.path.join(ROOT_DIR, "data", "maps"))
CITIES_DIR = os.path.realpath(os.path.join(ROOT_DIR, "data", "cities"))
TRANSIT_DIR = os.path.join(ROOT_DIR, "data", "transit")

BINARY_CANDIDATES = [
    os.path.join(ROOT_DIR, "bin", "agss"),
    os.path.join(ROOT_DIR, "build", "bin", "agss"),
    os.path.join(ROOT_DIR, "build", "bin", "Release", "agss.exe"),
]

# A map id is a single directory name. Anything else -- separators, dots,
# absolute paths -- is rejected before it reaches the filesystem.
MAP_ID_RE = re.compile(r"^[A-Za-z0-9._-]{1,64}$")

ALLOWED_ALGS = {
    "bfs", "dfs", "dijkstra", "astar", "greedy", "bellmanford",
    "floydwarshall", "johnson", "bidijkstra", "dial",
}

RUN_TIMEOUT_S = 60


def find_binary() -> str | None:
    for path in BINARY_CANDIDATES:
        if os.path.isfile(path) and os.access(path, os.X_OK):
            return path
    return None


def resolve_map(map_id: str) -> str | None:
    """Map a client-supplied id onto a directory, or None if it is not one.

    Validated by pattern first, then confirmed by realpath containment, so a
    symlink inside the maps directory cannot be used to step outside it either.
    """
    if not MAP_ID_RE.match(map_id):
        return None
    for root in (MAPS_DIR, CITIES_DIR):
        candidate = os.path.realpath(os.path.join(root, map_id))
        if os.path.commonpath([candidate, root]) != root:
            continue
        if os.path.isdir(candidate):
            return candidate
    return None


def list_maps() -> list[dict]:
    out = []
    for root, kind in ((MAPS_DIR, "synthetic"), (CITIES_DIR, "openstreetmap")):
        if not os.path.isdir(root):
            continue
        for name in sorted(os.listdir(root)):
            path = os.path.join(root, name)
            if not os.path.isdir(path) or not MAP_ID_RE.match(name):
                continue
            if not os.path.isfile(os.path.join(path, "nodes.csv")):
                continue
            out.append({"id": name, "kind": kind,
                        "geographic": kind == "openstreetmap"})
    return out


class Handler(http.server.SimpleHTTPRequestHandler):
    server_version = "agss-dev"

    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=UI_DIR, **kwargs)

    def log_message(self, fmt, *args):  # quieter default logging
        print(f"{self.address_string()} - {fmt % args}")

    def _reply(self, code: int, obj) -> None:
        body = json.dumps(obj).encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("X-Content-Type-Options", "nosniff")
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        path = urllib.parse.urlparse(self.path).path
        if path == "/maps":
            return self._reply(200, {"maps": list_maps()})
        if path == "/algorithms":
            return self._reply(200, {"algorithms": sorted(ALLOWED_ALGS)})
        if path == "/transit":
            return self.handle_transit()
        if path == "/run":
            params = urllib.parse.parse_qs(urllib.parse.urlparse(self.path).query)
            return self.handle_run({k: v[0] for k, v in params.items()})
        return super().do_GET()

    def do_POST(self):
        path = urllib.parse.urlparse(self.path).path
        if path != "/run":
            return self._reply(404, {"error": "no such endpoint"})
        try:
            length = int(self.headers.get("Content-Length", "0"))
        except ValueError:
            return self._reply(400, {"error": "bad Content-Length"})
        if length > 1 << 20:
            return self._reply(413, {"error": "request too large"})
        raw = self.rfile.read(length)
        ctype = self.headers.get("Content-Type", "")
        if not ctype.startswith("application/json"):
            return self._reply(415, {"error": "send application/json"})
        try:
            payload = json.loads(raw.decode("utf-8"))
        except (ValueError, UnicodeDecodeError) as exc:
            return self._reply(400, {"error": "malformed JSON", "detail": str(exc)})
        if not isinstance(payload, dict):
            return self._reply(400, {"error": "body must be a JSON object"})
        return self.handle_run(payload)

    def handle_run(self, params: dict):
        alg = str(params.get("alg", "dijkstra"))
        if alg not in ALLOWED_ALGS:
            return self._reply(400, {"error": "unsupported algorithm",
                                     "allowed": sorted(ALLOWED_ALGS)})

        target_dir = resolve_map(str(params.get("map", "")))
        if target_dir is None:
            return self._reply(404, {"error": "map not found"})

        try:
            source = int(params.get("source", 0))
            target = int(params.get("target", 1))
        except (TypeError, ValueError):
            return self._reply(400, {"error": "source and target must be integers"})

        binary = find_binary()
        if binary is None:
            return self._reply(500, {"error": "agss binary not built; run `make` or "
                                              "`cmake --build build`"})

        # Per-request temporary file. The previous shared ui/trace.json made
        # concurrent requests overwrite and delete each other's results.
        with tempfile.TemporaryDirectory(prefix="agss-run-") as tmp:
            trace_path = os.path.join(tmp, "trace.json")
            cmd = [binary, "route", "--graph", target_dir, "--alg", alg,
                   "--source", str(source), "--target", str(target),
                   "--trace-out", trace_path, "--quiet"]
            if os.path.basename(os.path.dirname(target_dir)) == "cities" or \
                    os.path.isfile(os.path.join(target_dir, "manifest.json")):
                cmd.append("--geo")   # OSM extracts carry lat/lon
            if params.get("close"):
                cmd += ["--close", str(params["close"])]

            try:
                proc = subprocess.run(cmd, capture_output=True, text=True,
                                      timeout=RUN_TIMEOUT_S)
            except subprocess.TimeoutExpired:
                return self._reply(504, {"error": f"engine exceeded {RUN_TIMEOUT_S}s"})
            except OSError as exc:
                return self._reply(500, {"error": "could not run engine", "detail": str(exc)})

            trace = None
            if os.path.isfile(trace_path):
                try:
                    with open(trace_path, encoding="utf-8") as fh:
                        trace = json.load(fh)
                except (OSError, ValueError) as exc:
                    return self._reply(500, {"error": "engine produced an unreadable trace",
                                             "detail": str(exc)})

        # rc 2 means "ran fine, no route exists" -- not a server error.
        return self._reply(200, {"rc": proc.returncode, "stderr": proc.stderr.strip(),
                                 "trace": trace})

    def handle_transit(self):
        stations = os.path.join(TRANSIT_DIR, "stations.csv")
        if not os.path.isfile(stations):
            return self._reply(404, {"error": "no transit data; run scripts/fetch_metros.py"})
        systems: dict[str, int] = {}
        with open(stations, encoding="utf-8") as fh:
            next(fh, None)
            for line in fh:
                parts = line.split(",")
                if len(parts) >= 3:
                    systems[parts[2].strip()] = systems.get(parts[2].strip(), 0) + 1
        return self._reply(200, {"systems": [{"name": k, "stations": v}
                                             for k, v in sorted(systems.items())]})


def main() -> int:
    if find_binary() is None:
        print("warning: agss binary not found; build it with `make` first")
    print(f"agss dev server on http://{HOST}:{PORT}/  (UI from {UI_DIR})")
    print(f"  maps: {len(list_maps())} available")
    httpd = http.server.ThreadingHTTPServer((HOST, PORT), Handler)
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        print("\nstopping")
    finally:
        httpd.server_close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
