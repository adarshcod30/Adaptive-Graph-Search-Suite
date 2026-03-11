#include "Algorithm.hpp"
#include <stack>
#include <unordered_set>
#include <chrono>
#include <algorithm>
#include <unordered_map>

SearchResult DFS::run(const Graph& graph, int sourceId, int targetId) {
    SearchResult res;
    res.algorithmName = "Depth-First Search";
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

    std::stack<int> s;
    std::unordered_set<int> visited;
    std::unordered_map<int, int> parent;
    
    s.push(sourceId);

    std::vector<int> currentFrontier = {sourceId};
    std::vector<int> currentExplored;

    res.framesFrontier.push_back(currentFrontier);
    res.framesExplored.push_back(currentExplored);
    res.framesPath.push_back({});

    while (!s.empty()) {
        int u = s.top();
        s.pop();

        if (visited.find(u) != visited.end()) continue;
        visited.insert(u);

        auto it = std::find(currentFrontier.begin(), currentFrontier.end(), u);
        if (it != currentFrontier.end()) {
            currentFrontier.erase(it);
        }
        currentExplored.push_back(u);

        if (u == targetId) {
            res.success = true;
            break;
        }

        for (const auto& edge : graph.getEdges(u)) {
            if (visited.find(edge.to) == visited.end()) {
                parent[edge.to] = u;
                s.push(edge.to);
                currentFrontier.push_back(edge.to);
            }
        }
        
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

    res.framesFrontier.push_back({});
    res.framesExplored.push_back(currentExplored);
    res.framesPath.push_back(res.finalPath);

    return res;
}
