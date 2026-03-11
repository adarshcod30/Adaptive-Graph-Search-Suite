#include "Trace.hpp"
#include <fstream>
#include <iostream>
#include <sstream>

std::string escapeJSON(const std::string& input) {
    std::string output;
    for (char c : input) {
        if (c == '"') output += "\\\"";
        else if (c == '\\') output += "\\\\";
        else if (c == '\b') output += "\\b";
        else if (c == '\f') output += "\\f";
        else if (c == '\n') output += "\\n";
        else if (c == '\r') output += "\\r";
        else if (c == '\t') output += "\\t";
        else output += c;
    }
    return output;
}

void Trace::exportJSON(const std::string& filepath, const Graph& graph, const SearchResult& res) {
    std::ofstream out(filepath);
    if (!out.is_open()) {
        std::cerr << "Error writing trace to " << filepath << std::endl;
        return;
    }

    out << "{\n";
    // Metadata
    out << "  \"metadata\": {\n";
    out << "    \"algorithm\": \"" << escapeJSON(res.algorithmName) << "\",\n";
    out << "    \"timeComplexity\": \"" << escapeJSON(res.timeComplexity) << "\",\n";
    out << "    \"spaceComplexity\": \"" << escapeJSON(res.spaceComplexity) << "\",\n";
    out << "    \"executionTimeMs\": " << res.executionTimeMs << ",\n";
    out << "    \"success\": " << (res.success ? "true" : "false") << "\n";
    out << "  },\n";

    // Graph
    out << "  \"graph\": {\n";
    out << "    \"nodes\": [\n";
    bool firstNode = true;
    for (const auto& pair : graph.getNodes()) {
        if (!firstNode) out << ",\n";
        out << "      { \"id\": " << pair.second.id << ", \"x\": " << pair.second.x << ", \"y\": " << pair.second.y << " }";
        firstNode = false;
    }
    out << "\n    ],\n";
    
    out << "    \"edges\": [\n";
    bool firstEdge = true;
    for (const auto& pair : graph.getNodes()) {
        int u = pair.first;
        for (const auto& edge : graph.getEdges(u)) {
            if (!firstEdge) out << ",\n";
            out << "      { \"u\": " << u << ", \"v\": " << edge.to << ", \"w\": " << edge.weight << " }";
            firstEdge = false;
        }
    }
    out << "\n    ]\n";
    out << "  },\n";

    // Frames
    out << "  \"frames\": [\n";
    for (size_t i = 0; i < res.framesFrontier.size(); ++i) {
        if (i > 0) out << ",\n";
        out << "    { \"frontier\": [";
        for (size_t j = 0; j < res.framesFrontier[i].size(); ++j) {
            if (j > 0) out << ",";
            out << res.framesFrontier[i][j];
        }
        out << "], \"explored\": [";
        for (size_t j = 0; j < res.framesExplored[i].size(); ++j) {
            if (j > 0) out << ",";
            out << res.framesExplored[i][j];
        }
        out << "], \"path\": [";
        for (size_t j = 0; j < res.framesPath[i].size(); ++j) {
            if (j > 0) out << ",";
            out << res.framesPath[i][j];
        }
        out << "] }";
    }
    out << "\n  ]\n";
    out << "}\n";
    
    out.close();
}
