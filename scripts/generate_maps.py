import argparse
import math
import os
import random
import sys

# Fixed by default so the bundled maps are reproducible. Regenerating used to
# produce a completely different graph every time, which is why the map CSVs
# always showed as modified and why no benchmark figure was comparable across
# runs.
DEFAULT_SEED = 20260908

# Real urban road networks have a detour index around 1.2-1.4: a road is always
# at least as long as the straight line between its endpoints, usually longer.
# Honouring that is what keeps the straight-line A* heuristic admissible.
MIN_DETOUR = 1.05
MAX_DETOUR = 1.35

# Coordinates are rounded before any distance is computed, so the numbers in
# the CSV are the numbers the weights were derived from.
COORD_DP = 6
WEIGHT_DP = 6

def distance(x1, y1, x2, y2):
    return math.sqrt((x1-x2)**2 + (y1-y2)**2)

def generate_connected_graph(nodes_count, extra_edges, out_dir, name):
    os.makedirs(out_dir, exist_ok=True)
    nodes_csv = os.path.join(out_dir, "nodes.csv")
    edges_csv = os.path.join(out_dir, "edges.csv")
    
    nodes = []
    # Generate nodes
    for i in range(nodes_count):
        nodes.append({"id": i,
                      "x": round(random.uniform(0, 100), COORD_DP),
                      "y": round(random.uniform(0, 100), COORD_DP)})
        
    edges = []
    connected = {0}
    unconnected = set(range(1, nodes_count))
    
    # Build a Spanning Tree so it's guaranteed connected
    while unconnected:
        u = random.choice(list(connected))
        # Find nearest unconnected using spatial dist (closer to realistic roads)
        best_v = -1
        best_d = float('inf')
        # Sample subset to make it slightly messy
        sample = random.sample(list(unconnected), min(10, len(unconnected)))
        for v in sample:
            d = distance(nodes[u]["x"], nodes[u]["y"], nodes[v]["x"], nodes[v]["y"])
            if d < best_d:
                best_d = d
                best_v = v
                
        detour = random.uniform(MIN_DETOUR, MAX_DETOUR)
        w = round(best_d * detour, WEIGHT_DP)
        edges.append({"u": u, "v": best_v, "w": w})
        edges.append({"u": best_v, "v": u, "w": w})  # Bidirectional
        connected.add(best_v)
        unconnected.remove(best_v)
        
    # Add extra random edges for alternative paths
    for _ in range(extra_edges):
        u = random.randint(0, nodes_count-1)
        v = random.randint(0, nodes_count-1)
        if u != v:
            d = distance(nodes[u]["x"], nodes[u]["y"], nodes[v]["x"], nodes[v]["y"])
            # Detour factor, never below 1.0: a road cannot be shorter than the
            # straight line between its endpoints. The old range started at 0.9,
            # which made ~40% of edges shorter than the crow-flies distance and
            # silently broke A* admissibility -- the straight-line heuristic
            # became an over-estimate, so A* returned suboptimal paths while
            # Dijkstra and Bellman-Ford disagreed with it.
            detour = random.uniform(MIN_DETOUR, MAX_DETOUR)
            w = round(d * detour, WEIGHT_DP)
            edges.append({"u": u, "v": v, "w": w})
            edges.append({"u": v, "v": u, "w": w})
            
    with open(nodes_csv, "w") as fn:
        fn.write("id,x,y\n")
        for n in nodes:
            fn.write(f"{n['id']},{n['x']:.6f},{n['y']:.6f}\n")
            
    with open(edges_csv, "w") as fe:
        fe.write("u,v,w\n")
        for e in edges:
            fe.write(f"{e['u']},{e['v']},{e['w']:.6f}\n")
            
    print(f"Generated {name} map with {nodes_count} nodes in {out_dir}")

def generate_grid_map(w, h, out_dir, name):
    os.makedirs(out_dir, exist_ok=True)
    nodes_csv = os.path.join(out_dir, "nodes.csv")
    edges_csv = os.path.join(out_dir, "edges.csv")
    
    with open(nodes_csv, "w") as fn:
        fn.write("id,x,y\n")
        # Visualizer canvas 0,0 is top-left
        for r in range(h):
            for c in range(w):
                id = r * w + c
                fn.write(f"{id},{c},{r}\n")
                
    with open(edges_csv, "w") as fe:
        fe.write("u,v,w\n")
        for r in range(h):
            for c in range(w):
                u = r * w + c
                if c + 1 < w:
                    v = u + 1
                    we = float(random.randint(1, 5))
                    fe.write(f"{u},{v},{we:.6f}\n")
                    fe.write(f"{v},{u},{we:.6f}\n")
                if r + 1 < h:
                    v = u + w
                    we = float(random.randint(1, 5))
                    fe.write(f"{u},{v},{we:.6f}\n")
                    fe.write(f"{v},{u},{we:.6f}\n")
    print(f"Generated {name} map ({w}x{h}) in {out_dir}")

def main() -> int:
    ap = argparse.ArgumentParser(description="Generate the bundled synthetic maps.")
    ap.add_argument("--seed", type=int, default=DEFAULT_SEED,
                    help=f"PRNG seed (default {DEFAULT_SEED}); the default reproduces "
                         "the committed maps byte for byte")
    ap.add_argument("--out", default=None, help="output root (default data/maps)")
    args = ap.parse_args()

    base = args.out or os.path.abspath(
        os.path.join(os.path.dirname(__file__), "..", "data", "maps"))

    # Re-seed before each map so adding or reordering maps cannot change the
    # ones that came before it.
    specs = [
        ("Mumbai_Pune_Expy", lambda d: generate_connected_graph(50, 40, d, "Mumbai Pune Expy")),
        ("Delhi_NCR", lambda d: generate_connected_graph(200, 300, d, "Delhi NCR")),
        ("Bengaluru_Traffic",
         lambda d: generate_connected_graph(400, 800, d, "Bengaluru Traffic")),
        ("Indian_Grid", lambda d: generate_grid_map(15, 15, d, "Indian Grid")),
        ("Small_Campus", lambda d: generate_grid_map(5, 5, d, "Small Campus")),
    ]
    for i, (name, build) in enumerate(specs):
        random.seed(args.seed + i)
        build(os.path.join(base, name))

    print(f"\nGenerated {len(specs)} maps with seed {args.seed}.")
    print("These maps are committed; regenerating with the default seed is a no-op.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

