#pragma once

#include <vector>

#include "agss/algorithm.hpp"
#include "agss/graph.hpp"

namespace agss {

struct AlternativeRoute {
    std::vector<NodeId> path;
    double cost = 0.0;
};

struct KShortestReport {
    std::vector<AlternativeRoute> routes;
    double elapsed_ms = 0.0;
};

/// Yen's algorithm: the K loopless shortest paths, in increasing cost order.
///
/// This is the "here are 3 alternate routes" feature every map app has. Yen's
/// works by taking the best path found so far, and for each prefix of it,
/// banning the edge it used and re-running a shortest-path search from that
/// spur node -- so each candidate diverges somewhere and rejoins.
/// Cost is O(K * V * (E + V log V)).
KShortestReport k_shortest_paths(const Graph& g, NodeId source, NodeId target, int k);

}  // namespace agss
