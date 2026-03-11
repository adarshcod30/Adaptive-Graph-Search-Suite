#ifndef GRAPH_HPP
#define GRAPH_HPP

#include <vector>
#include <string>
#include <unordered_map>
#include <stdexcept>
#include <iostream>

struct Edge {
    int to;
    double weight;
};

struct Node {
    int id;
    double x;
    double y;
};

class Graph {
private:
    std::unordered_map<int, Node> nodes;
    std::unordered_map<int, std::vector<Edge>> adj;
    int numNodes;
    int numEdges;

public:
    Graph() : numNodes(0), numEdges(0) {}

    void addNode(int id, double x, double y) {
        if (nodes.find(id) == nodes.end()) {
            nodes[id] = {id, x, y};
            numNodes++;
        }
    }

    void addEdge(int u, int v, double weight) {
        adj[u].push_back({v, weight});
        numEdges++;
    }

    const std::unordered_map<int, Node>& getNodes() const { return nodes; }
    const std::vector<Edge>& getEdges(int u) const { 
        auto it = adj.find(u);
        if (it != adj.end()) {
            return it->second;
        }
        static const std::vector<Edge> empty;
        return empty;
    }
    
    int getNumNodes() const { return numNodes; }
    int getNumEdges() const { return numEdges; }

    static Graph* loadCSV(const std::string& nodesPath, const std::string& edgesPath);
    static Graph* generateGrid(int width, int height, bool weighted);
};

#endif
