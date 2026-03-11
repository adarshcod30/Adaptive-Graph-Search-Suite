import sys
import random
import os
import math

def distance(x1, y1, x2, y2):
    return math.sqrt((x1-x2)**2 + (y1-y2)**2)

def generate_connected_graph(nodes_count, extra_edges, out_dir, name):
    os.makedirs(out_dir, exist_ok=True)
    nodes_csv = os.path.join(out_dir, "nodes.csv")
    edges_csv = os.path.join(out_dir, "edges.csv")
    
    nodes = []
    # Generate nodes
    for i in range(nodes_count):
        nodes.append({"id": i, "x": random.uniform(0, 100), "y": random.uniform(0, 100)})
        
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
                
        edges.append({"u": u, "v": best_v, "w": best_d})
        edges.append({"u": best_v, "v": u, "w": best_d}) # Bidirectional
        connected.add(best_v)
        unconnected.remove(best_v)
        
    # Add extra random edges for alternative paths
    for _ in range(extra_edges):
        u = random.randint(0, nodes_count-1)
        v = random.randint(0, nodes_count-1)
        if u != v:
            d = distance(nodes[u]["x"], nodes[u]["y"], nodes[v]["x"], nodes[v]["y"])
            edges.append({"u": u, "v": v, "w": d * random.uniform(0.9, 1.2)}) # Noise
            edges.append({"u": v, "v": u, "w": d * random.uniform(0.9, 1.2)})
            
    with open(nodes_csv, "w") as fn:
        fn.write("id,x,y\n")
        for n in nodes:
            fn.write(f"{n['id']},{n['x']:.4f},{n['y']:.4f}\n")
            
    with open(edges_csv, "w") as fe:
        fe.write("u,v,w\n")
        for e in edges:
            fe.write(f"{e['u']},{e['v']},{e['w']:.4f}\n")
            
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
                    we = random.uniform(1.0, 5.0)
                    fe.write(f"{u},{v},{we:.4f}\n")
                    fe.write(f"{v},{u},{we:.4f}\n")
                if r + 1 < h:
                    v = u + w
                    we = random.uniform(1.0, 5.0)
                    fe.write(f"{u},{v},{we:.4f}\n")
                    fe.write(f"{v},{u},{we:.4f}\n")
    print(f"Generated {name} map ({w}x{h}) in {out_dir}")

if __name__ == "__main__":
    base = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "data", "maps"))
    generate_connected_graph(50, 40, os.path.join(base, "Mumbai_Pune_Expy"), "Mumbai Pune Expy")
    generate_connected_graph(200, 300, os.path.join(base, "Delhi_NCR"), "Delhi NCR")
    generate_connected_graph(400, 800, os.path.join(base, "Bengaluru_Traffic"), "Bengaluru Traffic")
    generate_grid_map(15, 15, os.path.join(base, "Indian_Grid"), "Indian Grid")
    generate_grid_map(5, 5, os.path.join(base, "Small_Campus"), "Small Campus")

