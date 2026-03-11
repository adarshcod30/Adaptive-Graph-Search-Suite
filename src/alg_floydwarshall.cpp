#include "Algorithm.hpp"
#include <chrono>
#include <algorithm>
#include <unordered_map>
#include <limits>
#include <vector>

SearchResult FloydWarshall::run(const Graph& graph, int sourceId, int targetId) {
    SearchResult res;
    res.algorithmName = "Floyd-Warshall Algorithm";
    res.timeComplexity = "O(V^3)";
    res.spaceComplexity = "O(V^2)";
    res.success = false;

    auto start_time = std::chrono::high_resolution_clock::now();

    const auto& nodes = graph.getNodes();
    if (nodes.find(sourceId) == nodes.end() || nodes.find(targetId) == nodes.end()) {
        auto end_time = std::chrono::high_resolution_clock::now();
        res.executionTimeMs = std::chrono::duration<double, std::milli>(end_time - start_time).count();
        return res;
    }

    int n = graph.getNumNodes();

    // Mapping node IDs to indices (since IDs might not be continuous or properly starting at 0, 
    // though our python generator guarantees 0 to N-1).
    std::unordered_map<int, int> idToIndex;
    std::unordered_map<int, int> indexToId;
    int idx = 0;
    for (const auto& pair : nodes) {
        idToIndex[pair.first] = idx;
        indexToId[idx] = pair.first;
        idx++;
    }

    std::vector<std::vector<double>> dist(n, std::vector<double>(n, std::numeric_limits<double>::infinity()));
    std::vector<std::vector<int>> next(n, std::vector<int>(n, -1));

    for (int i = 0; i < n; i++) {
        dist[i][i] = 0.0;
        next[i][i] = indexToId[i];
    }

    for (const auto& pair : nodes) {
        int u = idToIndex[pair.first];
        for (const auto& edge : graph.getEdges(pair.first)) {
            int v = idToIndex[edge.to];
            dist[u][v] = edge.weight;
            next[u][v] = edge.to;
        }
    }

    // Floyd-Warshall
    for (int k = 0; k < n; k++) {
        for (int i = 0; i < n; i++) {
            if (dist[i][k] == std::numeric_limits<double>::infinity()) continue;
            for (int j = 0; j < n; j++) {
                if (dist[k][j] == std::numeric_limits<double>::infinity()) continue;
                if (dist[i][k] + dist[k][j] < dist[i][j]) {
                    dist[i][j] = dist[i][k] + dist[k][j];
                    next[i][j] = next[i][k];
                }
            }
        }
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    res.executionTimeMs = std::chrono::duration<double, std::milli>(end_time - start_time).count();

    int uIdx = idToIndex[sourceId];
    int vIdx = idToIndex[targetId];

    if (dist[uIdx][vIdx] != std::numeric_limits<double>::infinity()) {
        res.success = true;
        int curr = sourceId;
        res.finalPath.push_back(curr);
        while (curr != targetId) {
            curr = next[idToIndex[curr]][idToIndex[targetId]];
            res.finalPath.push_back(curr);
        }
    }

    // For visualization, we simply jump to the final path since O(V^3) frames would crash the browser.
    res.framesFrontier.push_back({});
    res.framesExplored.push_back(res.finalPath);
    res.framesPath.push_back(res.finalPath);

    return res;
}
