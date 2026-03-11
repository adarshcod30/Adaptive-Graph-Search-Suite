#include "Graph.hpp"
#include <fstream>
#include <sstream>
#include <random>

Graph* Graph::loadCSV(const std::string& nodesPath, const std::string& edgesPath) {
    Graph* g = new Graph();
    std::string line;

    // Load nodes
    std::ifstream fn(nodesPath);
    if (!fn.is_open()) return nullptr;
    std::getline(fn, line); // Skip header
    while (std::getline(fn, line)) {
        std::stringstream ss(line);
        std::string sid, sx, sy;
        std::getline(ss, sid, ',');
        std::getline(ss, sx, ',');
        std::getline(ss, sy, ',');
        if (!sid.empty() && !sx.empty() && !sy.empty()) {
            g->addNode(std::stoi(sid), std::stod(sx), std::stod(sy));
        }
    }
    fn.close();

    // Load edges
    std::ifstream fe(edgesPath);
    if (!fe.is_open()) {
        delete g;
        return nullptr;
    }
    std::getline(fe, line); // Skip header
    while (std::getline(fe, line)) {
        std::stringstream ss(line);
        std::string su, sv, sw;
        std::getline(ss, su, ',');
        std::getline(ss, sv, ',');
        std::getline(ss, sw, ',');
        if (!su.empty() && !sv.empty()) {
            double weight = sw.empty() ? 1.0 : std::stod(sw);
            g->addEdge(std::stoi(su), std::stoi(sv), weight);
        }
    }
    fe.close();

    return g;
}

Graph* Graph::generateGrid(int width, int height, bool weighted) {
    Graph* g = new Graph();
    std::mt19937 rng(42);
    std::uniform_real_distribution<double> dist(1.0, 10.0);

    for (int r = 0; r < height; ++r) {
        for (int c = 0; c < width; ++c) {
            int id = r * width + c;
            g->addNode(id, c, height - 1 - r); // Flip y for standard cartesian
        }
    }

    for (int r = 0; r < height; ++r) {
        for (int c = 0; c < width; ++c) {
            int u = r * width + c;
            if (c + 1 < width) {
                double w = weighted ? dist(rng) : 1.0;
                int v = u + 1;
                g->addEdge(u, v, w);
                g->addEdge(v, u, w);
            }
            if (r + 1 < height) {
                double w = weighted ? dist(rng) : 1.0;
                int v = u + width;
                g->addEdge(u, v, w);
                g->addEdge(v, u, w);
            }
        }
    }
    return g;
}
