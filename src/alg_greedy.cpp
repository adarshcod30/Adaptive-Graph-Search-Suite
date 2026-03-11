#include "Algorithm.hpp"
#include <queue>
#include <unordered_set>
#include <chrono>
#include <algorithm>
#include <unordered_map>
#include <limits>
#include <cmath>

inline double heuristic_greedy(const Node& a, const Node& b) {
    return std::sqrt(std::pow(a.x - b.x, 2) + std::pow(a.y - b.y, 2));
}

SearchResult Greedy::run(const Graph& graph, int sourceId, int targetId) {
    SearchResult res;
    res.algorithmName = "Greedy Best-First Search";
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

    const Node& targetNode = nodes.at(targetId);

    using PDI = std::pair<double, int>;
    std::priority_queue<PDI, std::vector<PDI>, std::greater<PDI>> pq;
    std::unordered_map<int, int> parent;
    std::unordered_set<int> visited; // Closed set

    pq.push({heuristic_greedy(nodes.at(sourceId), targetNode), sourceId});
    
    std::unordered_set<int> frontierSet = {sourceId};
    std::vector<int> currentExplored;

    while (!pq.empty()) {
        auto [h, u] = pq.top();
        pq.pop();

        if (visited.find(u) != visited.end()) continue;
        visited.insert(u);
        
        frontierSet.erase(u);
        currentExplored.push_back(u);

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
            if (visited.find(v) == visited.end() && frontierSet.find(v) == frontierSet.end()) {
                parent[v] = u;
                pq.push({heuristic_greedy(nodes.at(v), targetNode), v});
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
