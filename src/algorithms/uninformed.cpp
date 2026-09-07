// BFS, DFS and Dial's bucket-queue shortest path.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <deque>
#include <limits>
#include <queue>
#include <vector>

#include "agss/algorithm.hpp"

namespace agss {
namespace {

using Clock = std::chrono::steady_clock;
inline double ms_since(Clock::time_point t0) {
    return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

bool endpoints_valid(const Graph& g, NodeId s, NodeId t) {
    return s >= 0 && t >= 0 && s < g.num_nodes() && t < g.num_nodes();
}

}  // namespace

class BFS : public Algorithm {
public:
    std::string name() const override { return "Breadth-First Search"; }
    std::string time_complexity() const override { return "O(V + E)"; }
    std::string space_complexity() const override { return "O(V)"; }
    /// Hop-minimal, but only cost-optimal when every edge weighs the same.
    bool guarantees_optimal() const override { return false; }

    SearchResult run(const Graph& g, NodeId source, NodeId target,
                     const SearchOptions& opts) const override {
        SearchResult res = make_result();
        if (!endpoints_valid(g, source, target)) return res;

        std::vector<NodeId> parent(g.num_nodes(), kInvalidNode);
        std::vector<char> seen(g.num_nodes(), 0);
        std::queue<NodeId> q;

        const auto t0 = Clock::now();
        q.push(source);
        seen[source] = 1;
        if (opts.trace) opts.trace->discover(source, kInvalidNode);

        while (!q.empty()) {
            const NodeId u = q.front();
            q.pop();
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
                if (seen[v]) continue;
                seen[v] = 1;
                parent[v] = u;
                q.push(v);
                if (opts.trace) opts.trace->discover(v, u);
            }
        }
        res.algorithm_ms = ms_since(t0);

        if (res.success) {
            res.path = reconstruct_path(parent, source, target);
            res.path_cost = path_cost(g, res.path);
        }
        return res;
    }
};

class DFS : public Algorithm {
public:
    std::string name() const override { return "Depth-First Search"; }
    std::string time_complexity() const override { return "O(V + E)"; }
    std::string space_complexity() const override { return "O(V)"; }
    bool guarantees_optimal() const override { return false; }

    SearchResult run(const Graph& g, NodeId source, NodeId target,
                     const SearchOptions& opts) const override {
        SearchResult res = make_result();
        if (!endpoints_valid(g, source, target)) return res;

        std::vector<NodeId> parent(g.num_nodes(), kInvalidNode);
        std::vector<char> visited(g.num_nodes(), 0);
        std::vector<NodeId> stack;

        const auto t0 = Clock::now();
        stack.push_back(source);
        if (opts.trace) opts.trace->discover(source, kInvalidNode);

        while (!stack.empty()) {
            const NodeId u = stack.back();
            stack.pop_back();
            if (visited[u]) continue;
            visited[u] = 1;
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
                if (visited[v]) continue;
                // Parent is only written for not-yet-settled nodes, so a
                // settled node's chain can never be rewritten underneath it.
                parent[v] = u;
                stack.push_back(v);
                if (opts.trace) opts.trace->discover(v, u);
            }
        }
        res.algorithm_ms = ms_since(t0);

        if (res.success) {
            res.path = reconstruct_path(parent, source, target);
            res.path_cost = path_cost(g, res.path);
        }
        return res;
    }
};

/// Dial's algorithm: Dijkstra with a bucket queue instead of a binary heap.
///
/// When edge weights are small integers the priority queue's log factor
/// disappears -- distances are monotonically non-decreasing, so a circular
/// array of buckets indexed by distance serves as an O(1) priority queue.
/// Runs in O(V + E + maxDist). A worked example of the data structure, not the
/// algorithm, being what determines the complexity.
///
/// The integer requirement is real, not a formality. Bucket indices *are* the
/// distances, so a fractional weight has to be rounded to be storable, and the
/// search then optimises the rounded cost function rather than the true one --
/// silently returning a path that is optimal for a graph nobody asked about.
/// Rather than round, this refuses to run on non-integer weights; the
/// differential harness treats a declined run as "no opinion" instead of
/// accepting a wrong answer.
class Dial : public Algorithm {
public:
    std::string name() const override { return "Dial's Algorithm (bucket queue)"; }
    std::string time_complexity() const override { return "O(V + E + W)"; }
    std::string space_complexity() const override { return "O(V + W)"; }

    /// True only when every weight is an integer, which is Dial's precondition.
    static bool integral_weights(const Graph& g) {
        for (EdgeId e = 0; e < g.num_edges(); ++e) {
            const double w = g.edge_weight(e);
            if (std::abs(w - std::nearbyint(w)) > 1e-9) return false;
        }
        return true;
    }

    SearchResult run(const Graph& g, NodeId source, NodeId target,
                     const SearchOptions& opts) const override {
        SearchResult res = make_result();
        if (!endpoints_valid(g, source, target)) return res;

        if (!integral_weights(g)) {
            res.algorithm = name() + " (skipped: needs integer edge weights)";
            return res;
        }

        // Bucket indices are distances, so the array must span the largest
        // single edge weight.
        long max_w = 1;
        for (EdgeId e = 0; e < g.num_edges(); ++e) {
            max_w = std::max(max_w, static_cast<long>(std::llround(g.edge_weight(e))));
        }
        const long num_buckets = max_w * 2 + 2;
        if (num_buckets > 4'000'000) {
            // Weight range too wide for buckets to pay off; use Dijkstra.
            res.algorithm = name() + " (skipped: weight range too wide for buckets)";
            return res;
        }

        constexpr long kUnreached = std::numeric_limits<long>::max();
        std::vector<long> dist(g.num_nodes(), kUnreached);
        std::vector<NodeId> parent(g.num_nodes(), kInvalidNode);
        std::vector<char> settled(g.num_nodes(), 0);
        std::vector<std::vector<NodeId>> buckets(static_cast<std::size_t>(num_buckets));

        const auto t0 = Clock::now();
        dist[source] = 0;
        buckets[0].push_back(source);
        if (opts.trace) opts.trace->discover(source, kInvalidNode);

        long idx = 0;
        long remaining = 1;
        long empty_scans = 0;
        while (remaining > 0 && empty_scans <= num_buckets) {
            auto& bucket = buckets[static_cast<std::size_t>(idx % num_buckets)];
            if (bucket.empty()) {
                ++idx;
                ++empty_scans;
                continue;
            }
            empty_scans = 0;
            const NodeId u = bucket.back();
            bucket.pop_back();
            --remaining;
            if (settled[u] || dist[u] != idx) continue;
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
                const long w = static_cast<long>(std::llround(g.edge_weight(e)));
                const long nd = dist[u] + w;
                if (nd < dist[v]) {
                    const bool first = dist[v] == kUnreached;
                    dist[v] = nd;
                    parent[v] = u;
                    buckets[static_cast<std::size_t>(nd % num_buckets)].push_back(v);
                    ++remaining;
                    if (opts.trace) {
                        first ? opts.trace->discover(v, u) : opts.trace->relax(v, u);
                    }
                }
            }
        }
        res.algorithm_ms = ms_since(t0);

        if (res.success) {
            res.path = reconstruct_path(parent, source, target);
            res.path_cost = path_cost(g, res.path);
        }
        return res;
    }
};

AGSS_REGISTER_ALGORITHM("bfs", BFS)
AGSS_REGISTER_ALGORITHM("dfs", DFS)
AGSS_REGISTER_ALGORITHM("dial", Dial)

}  // namespace agss
