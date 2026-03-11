#include "Algorithm.hpp"
#include <queue>
#include <unordered_set>
#include <chrono>
#include <algorithm>
#include <unordered_map>

SearchResult BFS::run(const Graph& graph, int sourceId, int targetId) {
    SearchResult res;
    res.algorithmName = "Breadth-First Search";
    res.timeComplexity = "O(V + E)";
    res.spaceComplexity = "O(V)";
    res.success = false;

    auto start_time = std::chrono::high_resolution_clock::now();

    const auto& nodes = graph.getNodes();
    if (nodes.find(sourceId) == nodes.end() || nodes.find(targetId) == nodes.end()) {
        auto end_time = std::chrono::high_resolution_clock::now();
        res.executionTimeMs = std::chrono::duration<double, std::milli>(end_time - start_time).count();
        return res;
    }

    std::queue<int> q;
    std::unordered_set<int> visited;
    std::unordered_map<int, int> parent;
    
    q.push(sourceId);
    visited.insert(sourceId);

    std::vector<int> currentFrontier = {sourceId};
    std::vector<int> currentExplored;

    // Record initial frame
    res.framesFrontier.push_back(currentFrontier);
    res.framesExplored.push_back(currentExplored);
    res.framesPath.push_back({});

    while (!q.empty()) {
        int u = q.front();
        q.pop();

        // Move u from frontier to explored conceptually for visualization
        auto it = std::find(currentFrontier.begin(), currentFrontier.end(), u);
        if (it != currentFrontier.end()) {
            currentFrontier.erase(it);
        }
        currentExplored.push_back(u);

        if (u == targetId) {
            res.success = true;
            break; // Found path
        }

        bool newNodesAdded = false;
        for (const auto& edge : graph.getEdges(u)) {
            if (visited.find(edge.to) == visited.end()) {
                visited.insert(edge.to);
                parent[edge.to] = u;
                q.push(edge.to);
                currentFrontier.push_back(edge.to);
                newNodesAdded = true;
            }
        }
        
        // Record frame after expanding a node (if it added things or we finished)
        std::vector<int> currentPath;
        int curr = u;
        while (curr != sourceId && parent.find(curr) != parent.end()) {
            currentPath.push_back(curr);
            curr = parent[curr];
        }
        currentPath.push_back(sourceId);
        std::reverse(currentPath.begin(), currentPath.end());

        res.framesFrontier.push_back(currentFrontier);
        res.framesExplored.push_back(currentExplored);
        res.framesPath.push_back(currentPath);
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

    // Final frame ensuring final path is shown
    res.framesFrontier.push_back({});
    res.framesExplored.push_back(currentExplored);
    res.framesPath.push_back(res.finalPath);

    return res;
}
