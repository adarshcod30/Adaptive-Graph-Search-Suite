#pragma once

#include <cstdint>
#include <vector>

#include "agss/algorithm.hpp"
#include "agss/graph.hpp"

namespace agss {

/// ALT: A* with Landmarks and the Triangle inequality.
///
/// A* is only as good as its heuristic, and straight-line distance is a weak
/// lower bound on a road network -- roads bend, so the true cost is often far
/// above the crow-flies estimate and the search fans out anyway.
///
/// ALT replaces geometry with measurement. Pick a handful of landmarks, store
/// the exact distance from every node to and from each one, and then for any
/// pair (v, t) the triangle inequality gives two valid lower bounds per
/// landmark L:
///
///     d(v, t) >= d(v, L) - d(t, L)        (L "behind" the target)
///     d(v, t) >= d(L, t) - d(L, v)        (L "beyond" the target)
///
/// Taking the maximum over all landmarks yields a bound that is exact when the
/// route runs straight at a landmark, and never an over-estimate anywhere --
/// so A* keeps its optimality guarantee while pruning far harder.
///
/// Compared with Contraction Hierarchies: preprocessing is much cheaper
/// (a few Dijkstras rather than a full contraction) and the speedup smaller.
/// ALT also needs no geometry at all, so it works where coordinates are
/// missing or meaningless -- which is where straight-line A* cannot go.
class AltIndex {
public:
    struct BuildOptions {
        /// More landmarks tighten the bound but cost O(V) memory each.
        /// 8-16 is the usual sweet spot on road networks.
        int landmark_count = 12;
        /// Farthest-point selection needs a starting node; fixing it keeps
        /// preprocessing reproducible.
        NodeId seed_node = 0;
    };

    struct Stats {
        double build_ms = 0.0;
        int landmarks = 0;
        std::int64_t bytes = 0;
    };

    void build(const Graph& g, const BuildOptions& opts);
    void build(const Graph& g) { build(g, BuildOptions{}); }

    bool ready() const noexcept { return built_; }
    const Stats& stats() const noexcept { return stats_; }
    const std::vector<NodeId>& landmarks() const noexcept { return landmarks_; }

    /// Lower bound on the remaining cost from `v` to `target`.
    double lower_bound(NodeId v, NodeId target) const;

    SearchResult query(NodeId source, NodeId target, const SearchOptions& opts = {}) const;

private:
    const Graph* graph_ = nullptr;
    bool built_ = false;
    Stats stats_;

    std::vector<NodeId> landmarks_;
    /// from_[i][v] = d(landmark i, v); to_[i][v] = d(v, landmark i).
    /// Both are needed because road networks are directed.
    std::vector<std::vector<double>> from_;
    std::vector<std::vector<double>> to_;
};

}  // namespace agss
