# 🧭 MapTrace.X — Advanced C++ Routing & Serialization Engine

<p align="center">
  <img src="https://img.shields.io/badge/C%2B%2B-17-blue.svg" alt="C++17">
  <img src="https://img.shields.io/badge/Python-3-yellow.svg" alt="Python 3">
  <img src="https://img.shields.io/badge/UI-Glassmorphism-purple.svg" alt="UI Theme">
</p>

## 🚀 Overview

**MapTrace.X** is an advanced, high-performance graph processing engine built in **C++17**, wrapped with a lightweight **Python 3 API Bridge**, and visualized via an elevated, interactive **HTML5 Canvas UI** featuring Glassmorphism and dark mode aesthetics. 

It explores complex city-scale datasets (nodes and weighted edges mapped to 2D coordinates) calculating traversal paths and animating the programmatic behavior of famous heuristic and non-heuristic search algorithms.

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

---

## 🗺 Map Datasets

The repository includes a Python generator (`bin/generate_maps.py`) that proceduralizes map architectures into scalable datasets.

*   `manhattan_grid` (10x10 strict lattice network)
*   `small_city` (50 unstructured randomized nodes imitating European roads)
*   `large_city` (200 unstructured nodes simulating a major metropolitan area)

*Data format:* `nodes.csv` (id,x,y) and `edges.csv` (u,v,w)

---

## 🛠 Compilation & Deployment

This project strictly utilizes local standard libraries.

### 1. Compile the C++ Core
```bash
make clean && make
```
*Outputs compiled binary to `bin/adaptive_map`.*

### 2. Generate Synthetic Road Networks (Optional, maps are pre-included)
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
*   **Metrics Bar:** Highlights Time/Space constraints theoretically, alongside actual microsecond latency retrieved via `<chrono>`.

---

## 🎓 Academic Credit & Upgrade Status

*Initial Basic C Version Developed by:* Eashita Juneja and Team  
*Advanced V2 Rewrite:* Upgraded entirely to high-performance C++ by overriding raw structs with decoupled OOP, establishing complexity tracking, scaling data ingest, and building a premium Glassmorphism rendering client. 