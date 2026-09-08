#pragma once

#include <cstdint>
#include <unordered_map>
#include <vector>

#include "agss/algorithm.hpp"
#include "agss/graph.hpp"

namespace agss {

/// Customizable Contraction Hierarchies.
///
/// Plain CH bakes the edge weights into its shortcuts, so any change to the
/// metric -- live traffic, a different time of day, avoiding tolls -- means
/// redoing the whole contraction. On a city that is seconds of work for a
/// change that should cost milliseconds.
///
/// CCH splits preprocessing in two:
///
///   1. **Build** (slow, once): choose a contraction order and record the
///      *shape* of the shortcut graph. This step never looks at a weight, so
///      the result is valid for every metric on that road network.
///   2. **Customize** (fast, repeatable): given a weight per edge, fill in the
///      shortcut weights by enumerating lower triangles in contraction order.
///
/// Because build adds a shortcut for *every* pair of higher neighbours rather
/// than only where a witness search fails, the resulting graph is chordal:
/// for any triangle v < x < y in the order, the arc between x and y is
/// guaranteed to exist. That guarantee is what makes customization a simple
/// pass rather than another search. The price is more arcs than plain CH, so
/// queries are somewhat slower -- the trade is deliberate.
class CustomizableCH {
public:
    struct BuildOptions {
        double budget_ms = 30000.0;
    };

    struct Stats {
        double build_ms = 0.0;      ///< metric-independent, paid once
        double customize_ms = 0.0;  ///< per metric change
        std::int64_t chordal_edges = 0;
        std::int64_t original_edges = 0;
        double edge_growth = 1.0;
        std::int64_t triangles = 0;
        bool aborted = false;
    };

    void build(const Graph& g, const BuildOptions& opts);
    void build(const Graph& g) { build(g, BuildOptions{}); }

    /// Apply a metric. `weight_of_edge` is indexed by the original graph's
    /// EdgeId and must be finite and non-negative.
    void customize(const std::vector<double>& weight_of_edge);
    /// Convenience: use the graph's own weights.
    void customize();

    bool ready() const noexcept { return built_ && customized_; }
    const Stats& stats() const noexcept { return stats_; }
    std::int32_t level(NodeId u) const noexcept { return level_[u]; }

    SearchResult query(NodeId source, NodeId target, const SearchOptions& opts = {}) const;

private:
    /// One chordal edge {a, b} with level[a] < level[b], carrying a weight in
    /// each direction plus the node its shortcut passes through.
    struct ChordalEdge {
        NodeId lo, hi;
        double fwd = 0.0;  ///< lo -> hi
        double bwd = 0.0;  ///< hi -> lo
        NodeId fwd_via = kInvalidNode;
        NodeId bwd_via = kInvalidNode;
    };

    std::int64_t edge_index(NodeId a, NodeId b) const;
    void unpack_forward(NodeId from, NodeId to, std::vector<NodeId>& out) const;
    void unpack_backward(NodeId from, NodeId to, std::vector<NodeId>& out) const;

    const Graph* graph_ = nullptr;
    bool built_ = false;
    bool customized_ = false;
    Stats stats_;

    std::vector<std::int32_t> level_;
    std::vector<ChordalEdge> edges_;
    /// Upward incidence: for each node, the chordal edges to higher nodes.
    std::vector<std::vector<std::int32_t>> up_;
    std::unordered_map<std::uint64_t, std::int32_t> lookup_;
};

}  // namespace agss
