#include "Graph.hpp"
#include "Algorithm.hpp"
#include "Trace.hpp"
#include <iostream>
#include <string>
#include <memory>

void printHelp() {
    std::cout << "Usage: ./bin/adaptive_map --graph <dir> --alg <alg_name> --source <id> --target <id> --trace-out <file>\n";
    std::cout << "Algorithms: bfs, dfs, dijkstra, astar\n";
}

int main(int argc, char** argv) {
    std::string graphDir;
    std::string algName = "bfs";
    int sourceId = 0;
    int targetId = 1;
    std::string traceOut;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--graph" && i + 1 < argc) graphDir = argv[++i];
        else if (arg == "--alg" && i + 1 < argc) algName = argv[++i];
        else if (arg == "--source" && i + 1 < argc) sourceId = std::stoi(argv[++i]);
        else if (arg == "--target" && i + 1 < argc) targetId = std::stoi(argv[++i]);
        else if (arg == "--trace-out" && i + 1 < argc) traceOut = argv[++i];
        else if (arg == "--help") { printHelp(); return 0; }
    }

    if (graphDir.empty()) {
        std::cerr << "Error: --graph directory required.\n";
        printHelp();
        return 1;
    }

    std::string nodesPath = graphDir + "/nodes.csv";
    std::string edgesPath = graphDir + "/edges.csv";

    Graph* g = Graph::loadCSV(nodesPath, edgesPath);
    if (!g) {
        std::cerr << "Error loading graph from " << graphDir << std::endl;
        return 1;
    }

    std::unique_ptr<Algorithm> alg;
    if (algName == "bfs") alg = std::make_unique<BFS>();
    else if (algName == "dfs") alg = std::make_unique<DFS>();
    else if (algName == "dijkstra") alg = std::make_unique<Dijkstra>();
    else if (algName == "astar") alg = std::make_unique<AStar>();
    else if (algName == "greedy") alg = std::make_unique<Greedy>();
    else if (algName == "bellmanford") alg = std::make_unique<BellmanFord>();
    else if (algName == "floydwarshall") alg = std::make_unique<FloydWarshall>();
    else {
        std::cerr << "Unsupported algorithm: " << algName << std::endl;
        delete g;
        return 1;
    }

    SearchResult res = alg->run(*g, sourceId, targetId);
    
    std::cout << "Algorithm: " << res.algorithmName << "\n";
    std::cout << "Time elapsed: " << res.executionTimeMs << " ms\n";
    std::cout << "Success: " << (res.success ? "Yes" : "No") << "\n";

    if (!traceOut.empty()) {
        Trace::exportJSON(traceOut, *g, res);
        std::cout << "Trace written to " << traceOut << "\n";
    }

    delete g;
    return 0;
}
