#pragma once

#include <ostream>
#include <string>
#include <vector>

#include "agss/algorithm.hpp"
#include "agss/graph.hpp"
#include "agss/search_result.hpp"
#include "agss/trace.hpp"

namespace agss::json {

std::string escape(std::string_view s);

/// Serialise a number without a locale-dependent decimal separator and
/// without emitting bare `inf`/`nan`, which are not valid JSON.
std::string number(double v);

struct TraceDocument {
    const Graph* graph = nullptr;
    const SearchResult* result = nullptr;
    const Trace* trace = nullptr;
    /// Milliseconds spent building/serialising the trace, reported separately
    /// from the algorithm time so neither figure contaminates the other.
    double trace_ms = 0.0;
    const ClosureMask* closures = nullptr;
};

void write_trace(std::ostream& out, const TraceDocument& doc);
void write_graph(std::ostream& out, const Graph& g, int indent);

}  // namespace agss::json
