#pragma once

#include <cstdint>
#include <unordered_map>
#include <vector>

#include "agss/algorithm.hpp"
#include "agss/graph.hpp"

namespace agss {

/// Contraction Hierarchies: the technique production routing engines use.
///
/// The idea is to spend time once, offline, so that every later query is
/// almost free. Nodes are contracted one at a time in increasing order of
/// "importance": removing a node means adding shortcut edges between its
/// neighbours wherever the path through it was the only shortest one. Once
/// every node has been contracted, each original edge and each shortcut is
/// oriented from the less important endpoint to the more important one.
///
/// A query is then a bidirectional Dijkstra that only ever moves *upward* in
/// that order. Because road networks have very low highway dimension -- long
/// trips funnel onto a small number of arterials -- both searches climb to
/// the shared "important" core almost immediately, so the settled region is a
/// tiny fraction of what plain Dijkstra touches.
///
/// The preprocessing is where all the subtlety lives: contracting a node
/// requires knowing whether a witness path already connects each neighbour
/// pair without going through it, and searching for those witnesses
/// exhaustively is far more expensive than the contraction itself. This
/// implementation bounds every witness search by hop count and by distance,
/// which is the standard compromise: a missed witness only costs an
/// unnecessary shortcut, never a wrong answer.
class ContractionHierarchy {
public:
    struct BuildOptions {
        /// Hop limit for witness searches. Higher finds more witnesses and
        /// adds fewer shortcuts, at the cost of slower preprocessing.
        int witness_hop_limit = 5;
        /// Node settle limit per witness search, the other half of the bound.
        int witness_node_limit = 60;
        /// Weights for the node-importance priority terms.
        int edge_difference_weight = 1;
        int contracted_neighbours_weight = 1;
        int original_edges_weight = 1;
        /// Give up after this long. Contraction Hierarchies exploit the low
        /// highway dimension of road networks; on a dense random graph the
        /// shortcuts compound and preprocessing runs away (a 400-node random
        /// graph takes ~12 s against ~1 s for a 25k-node city). Bailing out
        /// keeps a bad input from hanging a browser tab.
        double budget_ms = 20000.0;
    };

    struct Stats {
        double build_ms = 0.0;
        std::int64_t shortcuts = 0;
        std::int64_t original_edges = 0;
        /// How much bigger the search graph got. Around 1.3-1.6x is healthy on
        /// road networks; much more means the witness search is too weak.
        double edge_growth = 1.0;
        std::int64_t witness_searches = 0;
        std::int64_t max_level = 0;
        /// True when the budget ran out. The hierarchy is still *correct* --
        /// uncontracted nodes simply sit at the top level, and queries fall
        /// back to searching them normally -- just less effective.
        bool aborted = false;
    };

    ContractionHierarchy() = default;

    /// Preprocess `g`. The graph must outlive this object.
    void build(const Graph& g, const BuildOptions& opts);
    void build(const Graph& g) { build(g, BuildOptions{}); }

    bool ready() const noexcept { return built_; }
    const Stats& stats() const noexcept { return stats_; }

    /// Rank of a node in the contraction order; higher is more important.
    std::int32_t level(NodeId u) const noexcept { return level_[u]; }

    /// Answer a query against the prepared hierarchy. `trace` records the
    /// upward searches so the browser can animate them next to a plain
    /// Dijkstra and show how much less ground they cover.
    SearchResult query(NodeId source, NodeId target, const SearchOptions& opts = {}) const;

private:
    struct Arc {
        NodeId to;
        double weight;
        /// Node this shortcut bypasses, or kInvalidNode for an original edge.
        NodeId via;
    };

    /// Expand shortcuts recursively back into original edges.
    void unpack(NodeId from, NodeId to, double weight, std::vector<NodeId>& out) const;

    const Graph* graph_ = nullptr;
    bool built_ = false;
    Stats stats_;

    std::vector<std::int32_t> level_;
    /// Shortcut arc -> the node it bypasses, keyed by (from << 32 | to).
    /// Needed to expand a hierarchy path back into original edges.
    std::unordered_map<std::uint64_t, NodeId> via_;
    // Search graph, split by direction of travel through the hierarchy.
    std::vector<std::vector<Arc>> up_;    // toward more important nodes
    std::vector<std::vector<Arc>> down_;  // reverse arcs, for the backward search
};

}  // namespace agss
