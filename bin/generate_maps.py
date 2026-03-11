import sys
import random
import os

def generate_city_map(nodes_count, edges_per_node, out_dir, name):
    os.makedirs(out_dir, exist_ok=True)
    
    nodes_csv = os.path.join(out_dir, "nodes.csv")
    edges_csv = os.path.join(out_dir, "edges.csv")
    
    with open(nodes_csv, "w") as fn:
        fn.write("id,x,y\n")
        for i in range(nodes_count):
            # Generate random city-like scattered coordinates
            x = random.uniform(0, 100)
            y = random.uniform(0, 100)
            fn.write(f"{i},{x:.4f},{y:.4f}\n")
            
    with open(edges_csv, "w") as fe:
        fe.write("u,v,w\n")
        for u in range(nodes_count):
            # Connect to some nearest neighbors approximately
            targets = random.sample(range(nodes_count), min(edges_per_node, nodes_count - 1))
            for v in targets:
                if u != v:
                    w = random.uniform(1.0, 15.0) # distances
                    fe.write(f"{u},{v},{w:.4f}\n")
    print(f"Generated {name} map with {nodes_count} nodes in {out_dir}")

def generate_grid_map(w, h, out_dir, name):
    os.makedirs(out_dir, exist_ok=True)
    nodes_csv = os.path.join(out_dir, "nodes.csv")
    edges_csv = os.path.join(out_dir, "edges.csv")
    
    with open(nodes_csv, "w") as fn:
        fn.write("id,x,y\n")
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
                    w_edge = random.uniform(1.0, 5.0)
                    fe.write(f"{u},{v},{w_edge:.4f}\n")
                    fe.write(f"{v},{u},{w_edge:.4f}\n")
                if r + 1 < h:
                    v = u + w
                    w_edge = random.uniform(1.0, 5.0)
                    fe.write(f"{u},{v},{w_edge:.4f}\n")
                    fe.write(f"{v},{u},{w_edge:.4f}\n")

    print(f"Generated {name} map ({w}x{h}) in {out_dir}")

if __name__ == "__main__":
    base = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "data", "maps"))
    generate_city_map(50, 4, os.path.join(base, "small_city"), "Small City")
    generate_city_map(200, 5, os.path.join(base, "large_city"), "Large City")
    generate_grid_map(10, 10, os.path.join(base, "manhattan_grid"), "Manhattan Grid")
