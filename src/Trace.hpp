#ifndef TRACE_HPP
#define TRACE_HPP

#include "Graph.hpp"
#include "Algorithm.hpp"
#include <string>

class Trace {
public:
    static void exportJSON(const std::string& filepath, const Graph& graph, const SearchResult& res);
};

#endif
