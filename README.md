# Adaptive Graph Search Suite

The **Adaptive Graph Search Suite** is a high-performance, full-stack educational platform designed to visualize and analyze advanced graph traversal algorithms on realistic map topologies. At its core, the project utilizes a robust **C++17 simulation engine** capable of executing a wide array of classical algorithms, including Breadth-First Search, Depth-First Search, Dijkstra’s Algorithm, A* Heuristic Search, Greedy Best-First Search, Bellman-Ford, and the Floyd-Warshall algorithm. This low-level backend ensures highly optimized calculations, actively evaluating the time constraints, space complexities, and microsecond-level execution latency of paths mapping across thousands of nodes.

Bridging the native C++ engine is a lightweight **Python web server** that seamlessly coordinates backend-to-frontend communication. It also manages an onboard map generator that programmatically builds and serves complex JSON graph datasets themed around Indian contexts—such as the Mumbai-Pune Expressway, massive Delhi NCR road layouts, Bengaluru traffic hubs, and structured dimensional grids. 

The user experience is anchored by a stunning, premium **glassmorphism web interface** built natively with HTML, CSS, and Vanilla JavaScript. As algorithms execute on the backend, the UI’s interactive HTML5 `<canvas>` parses the generated computation traces to vividly animate the step-by-step search process. The visualization engine incorporates silky-smooth mouse panning, dynamic zooming, adjustable playback speeds, and eye-catching neon glows that meticulously differentiate between the active search frontiers and the final deduced shortest path. 

Ultimately, the Adaptive Graph Search Suite serves as a powerful, visually captivating bridge between theoretical computer science and practical software engineering, allowing users to tangibly watch and compare how algorithmic logic systematically conquers complex geographic networks.

---

## 🏗 System Architecture

The project is structured into three strictly decoupled layers, communicating seamlessly via structured JSON data pipelines:

1.  **Core C++ Engine (`src/`):** 
    Pure object-oriented C++ classes. Includes an abstract `Algorithm` interface and a `Graph` class storing data in `std::unordered_map` and adjacency lists. Time measurements use `<chrono>`. Outputs rich trace metadata including Time/Space complexities.
2.  **API Bridge (`server.py`):**
    A dependency-free Python backend routing HTTP logic dynamically to the compiled C++ executable (`subprocess.run()`). It queries `data/maps/` structures and serves the frontend.
3.  **Visualization Client (`ui/`):**
    A reactive GUI utilizing native JS and the Canvas API. Includes pan/zoom capabilities, playback controls, and real-time complexity/latency performance data panels.

---

## 🧠 Supported Search Algorithms

| Algorithm | Heuristic? | Time Complexity | Space Complexity | Use Case |
|-----------|------------|-----------------|------------------|----------|
| **Breadth-First (BFS)** | No | $O(V + E)$ | $O(V)$ | Finding shortest paths on unweighted graphs. |
| **Depth-First (DFS)** | No | $O(V + E)$ | $O(V)$ | Exhaustive maze solving and topological sorting. |
| **Dijkstra's Algorithm** | No | $O((V+E) \log V)$ | $O(V)$ | Optimal shortest paths routing on weighted maps. |
| **A* Search (A-Star)** | Yes (Euclidean) | $O((V+E) \log V)$ | $O(V)$ | High-speed, directed mapping prioritizing the goal. |
| **Greedy Best-First** | Yes (Euclidean) | $O(V \log V)$ | $O(V)$ | Extreme-speed mapping prioritizing proximity over perfect optimality. |
| **Bellman-Ford Algorithm** | No | $O(V \cdot E)$ | $O(V)$ | Advanced routing capable of handling negative edge constraints. |
| **Floyd-Warshall** | No | $O(V^3)$ | $O(V^2)$ | All-pairs shortest path dynamic programming matrix solver. |

---

## 🗺 Map Datasets

The repository includes a Python generator (`bin/generate_maps.py`) that proceduralizes Map architectures into scalable datasets. Current profiles include contextualized Indian topologies generated with underlying Minimum Spanning Trees to guarantee connectivity:

*   `Mumbai_Pune_Expy` (50 unstructured randomized nodes mapped to highway distances)
*   `Delhi_NCR` (200 unstructured nodes simulating a major metropolitan area)
*   `Bengaluru_Traffic` (400 unstructured nodes mimicking dense traffic grids)
*   `Indian_Grid` (15x15 cartesian grid map for structured metric testing)
*   `Small_Campus` (5x5 small lattice network for algorithm dry runs)

*Data format:* `nodes.csv` (id,x,y) and `edges.csv` (u,v,w)

---

## 🛠 Compilation & Deployment

This project strictly utilizes local standard libraries without bloat.

### 1. Compile the C++ Core
```bash
make clean && make
```
*Outputs compiled binary to `bin/adaptive_map`.*

### 2. Generate Synthetic Road Networks
```bash
python3 bin/generate_maps.py
```

### 3. Launch API & Static Server
```bash
python3 server.py
```

### 4. Interface Access
Navigate your browser to: **http://127.0.0.1:9000/**

---

## 🎮 Interface Documentation

*   **Interactive Canvas:** Use Scroll Wheel to Zoom. Click and drag to Pan the camera around large networks.
*   **Search Engine:** Select a map dataset, source node, target node, and algorithm.
*   **Execution:** C++ executes the logic in roughly ~0.5ms. The UI then consumes the `trace.json` to animate the engine's internal states.
*   **Metrics Bar:** Highlights Time/Space constraints theoretically, alongside actual microsecond latency retrieved via `<chrono>` down at the bottom frame.

---

## 🎓 Academic Credit & Upgrade Status

*Initial Basic C Version Developed by:* Adarsh Dwivedi   
*Advanced Upgrade Rewrite:* Upgraded entirely to high-performance C++ by overriding raw structs with decoupled OOP, establishing complexity tracking, sealing new algorithms (Bellman, Floyd, Greedy), scaling contextual data ingest, and building a premium Glassmorphism rendering client.