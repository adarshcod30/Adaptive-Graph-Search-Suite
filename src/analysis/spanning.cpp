#include <algorithm>
#include <chrono>
#include <numeric>
#include <vector>

#include "agss/analysis.hpp"

namespace agss::analysis {
namespace {
using Clock = std::chrono::steady_clock;
inline double ms_since(Clock::time_point t0) {
    return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

/// Union-find with path compression and union by rank: near-constant
/// amortised find, which is what makes Kruskal's O(E log E) sort the
/// dominant term rather than the connectivity checks.
class DisjointSet {
public:
    explicit DisjointSet(std::size_t n) : parent_(n), rank_(n, 0) {
        std::iota(parent_.begin(), parent_.end(), 0);
    }
    std::size_t find(std::size_t x) {
        while (parent_[x] != x) {
            parent_[x] = parent_[parent_[x]];  // halve the path as we walk it
            x = parent_[x];
        }
        return x;
    }
    bool unite(std::size_t a, std::size_t b) {
        a = find(a);
        b = find(b);
        if (a == b) return false;
        if (rank_[a] < rank_[b]) std::swap(a, b);
        parent_[b] = a;
        if (rank_[a] == rank_[b]) ++rank_[a];
        return true;
    }

private:
    std::vector<std::size_t> parent_;
    std::vector<int> rank_;
};
}  // namespace

SpanningTreeReport minimum_spanning_tree(const Graph& g) {
    SpanningTreeReport rep;
    const auto t0 = Clock::now();
    const auto n = static_cast<std::size_t>(g.num_nodes());
    if (n == 0) {
        rep.elapsed_ms = ms_since(t0);
        return rep;
    }

    struct WEdge {
        double w;
        NodeId u, v;
    };
    std::vector<WEdge> edges;
    edges.reserve(static_cast<std::size_t>(g.num_edges()));
    for (NodeId u = 0; u < g.num_nodes(); ++u) {
        for (EdgeId e = g.edge_begin(u); e < g.edge_end(u); ++e) {
            const NodeId v = g.edge_target(e);
            if (u < v) edges.push_back({g.edge_weight(e), u, v});  // one per undirected pair
        }
    }
    std::sort(edges.begin(), edges.end(), [](const WEdge& a, const WEdge& b) { return a.w < b.w; });

    DisjointSet ds(n);
    for (const auto& e : edges) {
        if (ds.unite(static_cast<std::size_t>(e.u), static_cast<std::size_t>(e.v))) {
            rep.edges.emplace_back(e.u, e.v);
            rep.total_weight += e.w;
            if (rep.edges.size() == n - 1) break;
        }
    }
    rep.spans_all_nodes = (n <= 1) || (rep.edges.size() == n - 1);
    rep.elapsed_ms = ms_since(t0);
    return rep;
}

}  // namespace agss::analysis
