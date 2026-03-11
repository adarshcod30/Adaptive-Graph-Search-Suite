#!/usr/bin/env python3
import http.server
import urllib.parse
import json
import subprocess
import os
import shutil
import tempfile
from email.parser import BytesParser
from email.policy import default

HOST = "127.0.0.1"
PORT = 9000
ROOT_DIR = os.path.dirname(os.path.abspath(__file__))
UI_DIR = os.path.join(ROOT_DIR, "ui")
ADAPTIVE_BIN = os.path.join(ROOT_DIR, "bin", "adaptive_map")
TRACE_OUT_PATH = os.path.join(UI_DIR, "trace.json")
GRAPH_DIR = os.path.join(ROOT_DIR, "data", "graphs")
MAPS_DIR = os.path.join(ROOT_DIR, "data", "maps")
ALLOWED_ALGS = {"bfs", "dfs", "dijkstra", "astar", "greedy", "bellmanford", "floydwarshall"}

def parse_multipart_bytes(body_bytes, content_type_header):
    if not content_type_header or 'boundary=' not in content_type_header:
        return {}
    parser = BytesParser(policy=default)
    msg = parser.parsebytes(b'Content-Type: ' + content_type_header.encode() + b'\r\n\r\n' + body_bytes)
    parts = {}
    for part in msg.iter_parts():
        cd = part.get("Content-Disposition", "")
        if not cd:
            continue
        name = part.get_param("name", header="content-disposition")
        filename = part.get_param("filename", header="content-disposition")
        payload = part.get_payload(decode=True)
        parts[name] = (filename, payload)
    return parts

class Handler(http.server.SimpleHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=UI_DIR, **kwargs)

    def _json_reply(self, code, obj):
        b = json.dumps(obj).encode("utf-8")
        self.send_response(code)
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(b)))
        self.end_headers()
        self.wfile.write(b)

    def do_POST(self):
        parsed = urllib.parse.urlparse(self.path)
        if parsed.path == "/run":
            self.handle_run_post()
            return

    def do_GET(self):
        parsed = urllib.parse.urlparse(self.path)
        if parsed.path == "/maps":
            self.handle_maps_get()
            return
        if parsed.path == "/run":
            self.handle_run_get()
            return
        return super().do_GET()

    def handle_maps_get(self):
        maps = []
        if os.path.exists(MAPS_DIR):
            for m in os.listdir(MAPS_DIR):
                if os.path.isdir(os.path.join(MAPS_DIR, m)):
                    maps.append(m)
        return self._json_reply(200, {"maps": maps})

    def handle_run_post(self):
        ctype = self.headers.get("Content-Type", "")
        length = int(self.headers.get("Content-Length", 0))
        raw = self.rfile.read(length)
        data_json = None
        try:
            if ctype.startswith("application/json"):
                data_json = json.loads(raw.decode("utf-8"))
            else:
                return self._json_reply(400, {"error": "Only application/json supported now"})
        except Exception as e:
            return self._json_reply(400, {"error":"bad request JSON", "exception": str(e)})

        alg = data_json.get("alg", "bfs")
        if alg not in ALLOWED_ALGS:
            return self._json_reply(400, {"error":"unsupported alg", "allowed": sorted(list(ALLOWED_ALGS))})
            
        source = int(data_json.get("source", 0))
        target = int(data_json.get("target", 1))
        map_id = data_json.get("map", "small_city")
        
        target_dir = os.path.join(MAPS_DIR, map_id)
        if not os.path.exists(target_dir):
            return self._json_reply(404, {"error": "map not found"})

        if not os.path.isfile(ADAPTIVE_BIN) or not os.access(ADAPTIVE_BIN, os.X_OK):
            return self._json_reply(500, {"error":"adaptive binary not found or not executable", "path": ADAPTIVE_BIN})
            
        try:
            if os.path.exists(TRACE_OUT_PATH):
                os.remove(TRACE_OUT_PATH)
        except Exception:
            pass

        cmd = [ADAPTIVE_BIN, "--graph", target_dir, "--alg", alg, "--source", str(source), "--target", str(target), "--trace-out", TRACE_OUT_PATH]
        try:
            proc = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, timeout=60)
        except subprocess.TimeoutExpired as e:
            return self._json_reply(500, {"error":"execution timeout", "exception": str(e)})

        trace_data = None
        if os.path.exists(TRACE_OUT_PATH):
            try:
                with open(TRACE_OUT_PATH, "r", encoding="utf-8") as fh:
                    trace_data = json.load(fh)
            except Exception as e:
                trace_data = {"error":"failed to parse trace.json", "exception": str(e)}

        resp = {"rc": proc.returncode, "stdout": proc.stdout, "stderr": proc.stderr, "trace": trace_data}
        return self._json_reply(200, resp)

    def handle_run_get(self):
        parsed = urllib.parse.urlparse(self.path)
        params = urllib.parse.parse_qs(parsed.query)
        alg = params.get("alg", ["bfs"])[0]
        map_id = params.get("map", ["small_city"])[0]
        source = int(params.get("source", ["0"])[0])
        target = int(params.get("target", ["1"])[0])
        
        target_dir = os.path.join(MAPS_DIR, map_id)
        if not os.path.exists(target_dir):
            return self._json_reply(404, {"error": "map not found"})

        if not os.path.isfile(ADAPTIVE_BIN) or not os.access(ADAPTIVE_BIN, os.X_OK):
            return self._json_reply(500, {"error":"adaptive binary not found", "path": ADAPTIVE_BIN})

        cmd = [ADAPTIVE_BIN, "--graph", target_dir, "--alg", alg, "--source", str(source), "--target", str(target), "--trace-out", TRACE_OUT_PATH]
        proc = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, timeout=60)
        
        trace_data = None
        if os.path.exists(TRACE_OUT_PATH):
            try:
                with open(TRACE_OUT_PATH, "r", encoding="utf-8") as fh:
                    trace_data = json.load(fh)
            except Exception as e:
                trace_data = {"error":"failed to parse trace.json", "exception": str(e)}

        resp = {"rc": proc.returncode, "stdout": proc.stdout, "stderr": proc.stderr, "trace": trace_data}
        return self._json_reply(200, resp)

if __name__ == "__main__":
    os.chdir(ROOT_DIR)
    print(f"Serving UI and bridge at http://{HOST}:{PORT}/ (UI from {UI_DIR})")
    server = http.server.ThreadingHTTPServer((HOST, PORT), Handler)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("Stopping server")
        server.server_close()
