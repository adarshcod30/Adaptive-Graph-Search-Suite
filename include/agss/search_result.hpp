#pragma once

#include <string>
#include <vector>

#include "agss/graph.hpp"

namespace agss {

struct SearchResult {
    std::string algorithm;
    std::string time_complexity;
    std::string space_complexity;

    bool success = false;
    std::vector<NodeId> path;  ///< dense ids, source first
    double path_cost = 0.0;

    /// Wall time of the search itself. The previous engine's timer spanned the
    /// per-frame bookkeeping too, which inflated the reported figure by 5x at
    /// 3.6k nodes and 33x at 19.6k -- so the numbers described the trace
    /// writer, not the algorithm. This covers only the search.
    double algorithm_ms = 0.0;

    std::int64_t nodes_expanded = 0;
    std::int64_t edges_relaxed = 0;
};

}  // namespace agss
