#pragma once

#include <vector>

#include "agss/algorithm.hpp"
#include "agss/graph.hpp"

namespace agss {

struct IsochroneBand {
    double cutoff = 0.0;             ///< cost budget for this band
    std::vector<NodeId> nodes;       ///< nodes reachable within it
    std::vector<std::pair<double, double>> hull;  ///< convex hull, (x, y)
};

struct IsochroneReport {
    std::vector<IsochroneBand> bands;
    std::vector<double> cost_to;  ///< size V, infinity where unreachable
    double elapsed_ms = 0.0;
};

/// "Everywhere you can reach within N minutes."
///
/// One Dijkstra from the origin with the largest cutoff answers every band at
/// once -- the cost array is monotone, so smaller bands are just tighter
/// filters on the same result rather than separate searches.
IsochroneReport isochrone(const Graph& g, NodeId origin, const std::vector<double>& cutoffs,
                          const SearchOptions& opts = {});

/// Andrew's monotone chain convex hull. O(n log n).
std::vector<std::pair<double, double>> convex_hull(std::vector<std::pair<double, double>> pts);

}  // namespace agss
