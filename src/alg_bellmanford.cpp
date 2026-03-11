#include "Algorithm.hpp"
#include <chrono>
#include <algorithm>
#include <unordered_map>
#include <limits>
#include <unordered_set>

SearchResult BellmanFord::run(const Graph& graph, int sourceId, int targetId) {
    SearchResult res;
    res.algorithmName = "Bellman-Ford Algorithm";
    res.timeComplexity = "O(V * E)";
    res.spaceComplexity = "O(V)";
    res.success = false;

    auto start_time = std::chrono::high_resolution_clock::now();

    const auto& nodes = graph.getNodes();
    if (nodes.find(sourceId) == nodes.end() || nodes.find(targetId) == nodes.end()) {
        auto end_time = std::chrono::high_resolution_clock::now();
        res.executionTimeMs = std::chrono::duration<double, std::milli>(end_time - start_time).count();
        return res;
    }

    std::unordered_map<int, double> dist;
    std::unordered_map<int, int> parent;
    for (const auto& pair : nodes) {
        dist[pair.first] = std::numeric_limits<double>::infinity();
    }
    dist[sourceId] = 0.0;

    int numNodes = nodes.size();
    
    // Initial frame
    res.framesFrontier.push_back({sourceId});
    res.framesExplored.push_back({});
    res.framesPath.push_back({});

    std::unordered_set<int> currentExplored;

    for (int i = 0; i < numNodes - 1; ++i) {
        bool changed = false;
        std::unordered_set<int> relaxed_nodes;
        
        for (const auto& pair : nodes) {
            int u = pair.first;
            if (dist[u] == std::numeric_limits<double>::infinity()) continue;
            
            for (const auto& edge : graph.getEdges(u)) {
                int v = edge.to;
                double w = edge.weight;
                if (dist[u] + w < dist[v]) {
                    dist[v] = dist[u] + w;
                    parent[v] = u;
                    relaxed_nodes.insert(v);
                    currentExplored.insert(u); // mark relaxer as explored
                    changed = true;
                }
            }
        }
        
        if (changed) {
            std::vector<int> fv(relaxed_nodes.begin(), relaxed_nodes.end());
            std::vector<int> ev(currentExplored.begin(), currentExplored.end());
            
            // Build current path to target if exists
            std::vector<int> curPath;
            if (parent.find(targetId) != parent.end()) {
                int curr = targetId;
                while (curr != sourceId && parent.find(curr) != parent.end()) {
                    curPath.push_back(curr);
                    curr = parent[curr];
                }
                curPath.push_back(sourceId);
                std::reverse(curPath.begin(), curPath.end());
            }

            res.framesFrontier.push_back(fv);
            res.framesExplored.push_back(ev);
            res.framesPath.push_back(curPath);
        } else {
            break; // Stop early if no changes
        }
    }

    // Checking for negative-weight cycles is strictly optional here since it's a map routing suite 
    // but standard Bellman-Ford includes it.
    for (const auto& pair : nodes) {
        int u = pair.first;
        if (dist[u] == std::numeric_limits<double>::infinity()) continue;
        for (const auto& edge : graph.getEdges(u)) {
            if (dist[u] + edge.weight < dist[edge.to]) {
                // Negative cycle found, no valid shortest path can be guaranteed 
                // Let's just return false
                res.success = false;
                auto end_time = std::chrono::high_resolution_clock::now();
                res.executionTimeMs = std::chrono::duration<double, std::milli>(end_time - start_time).count();
                return res;
            }
        }
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    res.executionTimeMs = std::chrono::duration<double, std::milli>(end_time - start_time).count();

    if (dist[targetId] != std::numeric_limits<double>::infinity()) {
        res.success = true;
        int curr = targetId;
        while (curr != sourceId) {
            res.finalPath.push_back(curr);
            curr = parent[curr];
        }
        res.finalPath.push_back(sourceId);
        std::reverse(res.finalPath.begin(), res.finalPath.end());
    }

    std::vector<int> ev(currentExplored.begin(), currentExplored.end());
    res.framesFrontier.push_back({});
    res.framesExplored.push_back(ev);
    res.framesPath.push_back(res.finalPath);

    return res;
}
