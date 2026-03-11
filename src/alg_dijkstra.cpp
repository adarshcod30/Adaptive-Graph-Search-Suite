#include "Algorithm.hpp"
#include <queue>
#include <unordered_set>
#include <chrono>
#include <algorithm>
#include <unordered_map>
#include <limits>

SearchResult Dijkstra::run(const Graph& graph, int sourceId, int targetId) {
    SearchResult res;
    res.algorithmName = "Dijkstra's Algorithm";
    res.timeComplexity = "O((V + E) log V)";
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
    for (const auto& pair : nodes) dist[pair.first] = std::numeric_limits<double>::infinity();
    dist[sourceId] = 0.0;

    using PDI = std::pair<double, int>;
    std::priority_queue<PDI, std::vector<PDI>, std::greater<PDI>> pq;
    std::unordered_map<int, int> parent;
    std::unordered_set<int> visited; // fully explored set

    pq.push({0.0, sourceId});
    
    std::unordered_set<int> frontierSet = {sourceId}; // To track frontier for vis
    std::vector<int> currentExplored;

    while (!pq.empty()) {
        auto [d, u] = pq.top();
        pq.pop();

        if (visited.find(u) != visited.end()) continue;
        visited.insert(u);
        
        frontierSet.erase(u);
        currentExplored.push_back(u);

        // Record trace frame (slow down a bit for large graphs if needed, but we save all here)
        std::vector<int> curPath;
        int curr = u;
        while (curr != sourceId && parent.find(curr) != parent.end()) {
            curPath.push_back(curr);
            curr = parent[curr];
        }
        curPath.push_back(sourceId);
        std::reverse(curPath.begin(), curPath.end());
        
        std::vector<int> fv(frontierSet.begin(), frontierSet.end());
        res.framesFrontier.push_back(fv);
        res.framesExplored.push_back(currentExplored);
        res.framesPath.push_back(curPath);

        if (u == targetId) {
            res.success = true;
            break;
        }

        for (const auto& edge : graph.getEdges(u)) {
            int v = edge.to;
            double weight = edge.weight;
            if (visited.find(v) == visited.end() && dist[u] + weight < dist[v]) {
                dist[v] = dist[u] + weight;
                parent[v] = u;
                pq.push({dist[v], v});
                frontierSet.insert(v);
            }
        }
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    res.executionTimeMs = std::chrono::duration<double, std::milli>(end_time - start_time).count();

    if (res.success) {
        int curr = targetId;
        while (curr != sourceId) {
            res.finalPath.push_back(curr);
            curr = parent[curr];
        }
        res.finalPath.push_back(sourceId);
        std::reverse(res.finalPath.begin(), res.finalPath.end());
    }

    res.framesFrontier.push_back({});
    res.framesExplored.push_back(currentExplored);
    res.framesPath.push_back(res.finalPath);

    return res;
}
