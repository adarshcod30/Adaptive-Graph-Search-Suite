#ifndef ALGORITHM_HPP
#define ALGORITHM_HPP

#include "Graph.hpp"
#include <vector>
#include <string>

struct SearchResult {
    std::string algorithmName;
    std::string timeComplexity;
    std::string spaceComplexity;
    double executionTimeMs;
    std::vector<int> finalPath; // Ordered nodes from start to end
    std::vector<std::vector<int>> framesFrontier;
    std::vector<std::vector<int>> framesExplored;
    std::vector<std::vector<int>> framesPath;
    bool success;
};

class Algorithm {
public:
    virtual ~Algorithm() = default;
    virtual SearchResult run(const Graph& graph, int sourceId, int targetId) = 0;
};

class BFS : public Algorithm {
public:
    SearchResult run(const Graph& graph, int sourceId, int targetId) override;
};

class DFS : public Algorithm {
public:
    SearchResult run(const Graph& graph, int sourceId, int targetId) override;
};

class Dijkstra : public Algorithm {
public:
    SearchResult run(const Graph& graph, int sourceId, int targetId) override;
};

class AStar : public Algorithm {
public:
    SearchResult run(const Graph& graph, int sourceId, int targetId) override;
};

class BellmanFord : public Algorithm {
public:
    SearchResult run(const Graph& graph, int sourceId, int targetId) override;
};

class Greedy : public Algorithm {
public:
    SearchResult run(const Graph& graph, int sourceId, int targetId) override;
};

class FloydWarshall : public Algorithm {
public:
    SearchResult run(const Graph& graph, int sourceId, int targetId) override;
};

class BidirectionalBFS : public Algorithm {
public:
    SearchResult run(const Graph& graph, int sourceId, int targetId) override;
};

#endif
