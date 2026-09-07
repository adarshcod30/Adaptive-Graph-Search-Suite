// Bellman-Ford, Floyd-Warshall, Johnson and Yen's K-shortest paths.
#include <algorithm>
#include <chrono>
#include <limits>
#include <queue>
#include <set>
#include <vector>

#include "agss/algorithm.hpp"

namespace agss {
namespace {

using Clock = std::chrono::steady_clock;
inline double ms_since(Clock::time_point t0) {
    return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}
constexpr double kInf = std::numeric_limits<double>::infinity();

bool endpoints_valid(const Graph& g, NodeId s, NodeId t) {
    return s >= 0 && t >= 0 && s < g.num_nodes() && t < g.num_nodes();
}

}  // namespace

class BellmanFord : public Algorithm {
public:
    std::string name() const override { return "Bellman-Ford Algorithm"; }
    std::string time_complexity() const override { return "O(V * E)"; }
    std::string space_complexity() const override { return "O(V)"; }
    bool handles_negative_weights() const override { return true; }

    SearchResult run(const Graph& g, NodeId source, NodeId target,
                     const SearchOptions& opts) const override {
        SearchResult res = make_result();
        if (!endpoints_valid(g, source, target)) return res;

        std::vector<double> dist(g.num_nodes(), kInf);
        std::vector<NodeId> parent(g.num_nodes(), kInvalidNode);

        const auto t0 = Clock::now();
        dist[source] = 0.0;
        if (opts.trace) opts.trace->discover(source, kInvalidNode);

        for (NodeId round = 0; round < g.num_nodes(); ++round) {
            bool changed = false;
            for (NodeId u = 0; u < g.num_nodes(); ++u) {
                if (dist[u] == kInf) continue;
                for (EdgeId e = g.edge_begin(u); e < g.edge_end(u); ++e) {
                    if (!opts.edge_open(e)) continue;
                    const NodeId v = g.edge_target(e);
                    ++res.edges_relaxed;
                    const double nd = dist[u] + g.edge_weight(e);
                    if (nd < dist[v]) {
                        const bool first = dist[v] == kInf;
                        dist[v] = nd;
                        parent[v] = u;
                        changed = true;
                        if (opts.trace) {
                            first ? opts.trace->discover(v, u) : opts.trace->relax(v, u);
                        }
                    }
                }
            }
            ++res.nodes_expanded;
            if (!changed) break;  // converged early
            if (round == g.num_nodes() - 1) {
                // A change on the V-th pass means a negative cycle is
                // reachable, so no shortest path is well defined.
                res.algorithm_ms = ms_since(t0);
                res.algorithm = name() + " (negative cycle detected)";
                return res;
            }
        }
        res.algorithm_ms = ms_since(t0);

        if (dist[target] != kInf) {
            res.path = reconstruct_path(parent, source, target);
            if (!res.path.empty()) {
                res.success = true;
                res.path_cost = dist[target];
            }
        }
        return res;
    }
};

/// All-pairs shortest paths by dynamic programming.
///
/// The previous implementation kept an id->index map and looked endpoints up
/// with `operator[]`, which silently inserts 0 for an unknown key. An edge
/// pointing at an undeclared node therefore aliased onto index 0 and the
/// algorithm reported a successful path through a node that did not exist.
/// With the CSR builder, node ids *are* dense indices and undeclared endpoints
/// are rejected at load, so that class of bug cannot be expressed here.
class FloydWarshall : public Algorithm {
public:
    std::string name() const override { return "Floyd-Warshall Algorithm"; }
    std::string time_complexity() const override { return "O(V^3)"; }
    std::string space_complexity() const override { return "O(V^2)"; }
    bool handles_negative_weights() const override { return true; }

    /// Above this the V^2 matrices stop fitting in RAM (25k nodes is already
    /// ~5 GB), so the run is refused rather than attempted.
    static constexpr NodeId kMaxNodes = 5000;

    SearchResult run(const Graph& g, NodeId source, NodeId target,
                     const SearchOptions& opts) const override {
        SearchResult res = make_result();
        if (!endpoints_valid(g, source, target)) return res;
        if (g.num_nodes() > kMaxNodes) {
            res.algorithm = name() + " (skipped: graph exceeds " +
                            std::to_string(kMaxNodes) + " nodes)";
            return res;
        }

        const auto n = static_cast<std::size_t>(g.num_nodes());
        const auto t0 = Clock::now();

        std::vector<double> dist(n * n, kInf);
        std::vector<NodeId> next(n * n, kInvalidNode);
        for (std::size_t i = 0; i < n; ++i) {
            dist[i * n + i] = 0.0;
            next[i * n + i] = static_cast<NodeId>(i);
        }
        for (NodeId u = 0; u < g.num_nodes(); ++u) {
            for (EdgeId e = g.edge_begin(u); e < g.edge_end(u); ++e) {
                if (!opts.edge_open(e)) continue;
                const NodeId v = g.edge_target(e);
                const std::size_t idx = static_cast<std::size_t>(u) * n + v;
                // Keep the cheapest of any parallel edges; the old code
                // overwrote, so a later heavier duplicate won.
                if (g.edge_weight(e) < dist[idx]) {
                    dist[idx] = g.edge_weight(e);
                    next[idx] = v;
                }
            }
        }

        for (std::size_t k = 0; k < n; ++k) {
            for (std::size_t i = 0; i < n; ++i) {
                const double dik = dist[i * n + k];
                if (dik == kInf) continue;
                for (std::size_t j = 0; j < n; ++j) {
                    const double dkj = dist[k * n + j];
                    if (dkj == kInf) continue;
                    if (dik + dkj < dist[i * n + j]) {
                        dist[i * n + j] = dik + dkj;
                        next[i * n + j] = next[i * n + k];
                    }
                }
            }
        }
        res.algorithm_ms = ms_since(t0);
        res.nodes_expanded = g.num_nodes();

        const auto s = static_cast<std::size_t>(source);
        const auto t = static_cast<std::size_t>(target);
        if (dist[s * n + t] != kInf && next[s * n + t] != kInvalidNode) {
            NodeId curr = source;
            res.path.push_back(curr);
            std::size_t guard = 0;
            while (curr != target && guard++ <= n) {
                curr = next[static_cast<std::size_t>(curr) * n + t];
                if (curr == kInvalidNode) {
                    res.path.clear();
                    break;
                }
                res.path.push_back(curr);
            }
            if (!res.path.empty() && res.path.back() == target) {
                res.success = true;
                res.path_cost = dist[s * n + t];
            } else {
                res.path.clear();
            }
        }
        if (opts.trace) {
            // O(V^3) frames would be meaningless to animate; show the result.
            for (NodeId v : res.path) opts.trace->expand(v);
        }
        return res;
    }
};

/// Johnson's algorithm: reweight with Bellman-Ford potentials so every edge
/// becomes non-negative, then run Dijkstra. Gives correct answers on graphs
/// with negative edges at O(VE + (V+E) log V) instead of Floyd-Warshall's
/// O(V^3) -- the right choice for sparse graphs, which road networks are.
class Johnson : public Algorithm {
public:
    std::string name() const override { return "Johnson's Algorithm"; }
    std::string time_complexity() const override { return "O(V*E + (V + E) log V)"; }
    std::string space_complexity() const override { return "O(V)"; }
    bool handles_negative_weights() const override { return true; }

    SearchResult run(const Graph& g, NodeId source, NodeId target,
                     const SearchOptions& opts) const override {
        SearchResult res = make_result();
        if (!endpoints_valid(g, source, target)) return res;

        const auto n = g.num_nodes();
        const auto t0 = Clock::now();

        // Potentials from a virtual node with a zero-weight edge to every
        // vertex. Since that node reaches everything, h starts at 0 and we
        // relax V-1 times over the real edge set.
        std::vector<double> h(n, 0.0);
        for (NodeId round = 0; round < n; ++round) {
            bool changed = false;
            for (NodeId u = 0; u < n; ++u) {
                for (EdgeId e = g.edge_begin(u); e < g.edge_end(u); ++e) {
                    if (!opts.edge_open(e)) continue;
                    const NodeId v = g.edge_target(e);
                    if (h[u] + g.edge_weight(e) < h[v]) {
                        h[v] = h[u] + g.edge_weight(e);
                        changed = true;
                    }
                }
            }
            if (!changed) break;
            if (round == n - 1) {
                res.algorithm_ms = ms_since(t0);
                res.algorithm = name() + " (negative cycle detected)";
                return res;
            }
        }

        // Reweighted Dijkstra: w'(u,v) = w(u,v) + h[u] - h[v] >= 0.
        struct Entry {
            double key;
            NodeId node;
            bool operator>(const Entry& o) const { return key > o.key; }
        };
        std::vector<double> dist(n, kInf);
        std::vector<NodeId> parent(n, kInvalidNode);
        std::vector<char> settled(n, 0);
        std::priority_queue<Entry, std::vector<Entry>, std::greater<>> pq;

        dist[source] = 0.0;
        pq.push({0.0, source});
        if (opts.trace) opts.trace->discover(source, kInvalidNode);

        while (!pq.empty()) {
            const NodeId u = pq.top().node;
            pq.pop();
            if (settled[u]) continue;
            settled[u] = 1;
            ++res.nodes_expanded;
            if (opts.trace) opts.trace->expand(u);
            if (u == target) {
                res.success = true;
                break;
            }
            for (EdgeId e = g.edge_begin(u); e < g.edge_end(u); ++e) {
                if (!opts.edge_open(e)) continue;
                const NodeId v = g.edge_target(e);
                ++res.edges_relaxed;
                if (settled[v]) continue;
                const double rw = g.edge_weight(e) + h[u] - h[v];
                const double nd = dist[u] + (rw < 0.0 ? 0.0 : rw);
                if (nd < dist[v]) {
                    const bool first = dist[v] == kInf;
                    dist[v] = nd;
                    parent[v] = u;
                    pq.push({nd, v});
                    if (opts.trace) first ? opts.trace->discover(v, u) : opts.trace->relax(v, u);
                }
            }
        }
        res.algorithm_ms = ms_since(t0);

        if (res.success) {
            res.path = reconstruct_path(parent, source, target);
            // Undo the potential shift to recover the true cost.
            res.path_cost = dist[target] - h[source] + h[target];
        }
        return res;
    }
};

AGSS_REGISTER_ALGORITHM("bellmanford", BellmanFord)
AGSS_REGISTER_ALGORITHM("floydwarshall", FloydWarshall)
AGSS_REGISTER_ALGORITHM("johnson", Johnson)

}  // namespace agss
