// Dijkstra, A*, Greedy best-first and bidirectional Dijkstra.
#include <algorithm>
#include <chrono>
#include <limits>
#include <memory>
#include <queue>
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

struct QueueEntry {
    double key;
    NodeId node;
    bool operator>(const QueueEntry& o) const { return key > o.key; }
};
using MinHeap = std::priority_queue<QueueEntry, std::vector<QueueEntry>, std::greater<>>;

}  // namespace

class Dijkstra : public Algorithm {
public:
    std::string name() const override { return "Dijkstra's Algorithm"; }
    std::string time_complexity() const override { return "O((V + E) log V)"; }
    std::string space_complexity() const override { return "O(V)"; }

    SearchResult run(const Graph& g, NodeId source, NodeId target,
                     const SearchOptions& opts) const override {
        SearchResult res = make_result();
        if (!endpoints_valid(g, source, target)) return res;

        std::vector<double> dist(g.num_nodes(), kInf);
        std::vector<NodeId> parent(g.num_nodes(), kInvalidNode);
        std::vector<char> settled(g.num_nodes(), 0);
        MinHeap pq;

        const auto t0 = Clock::now();
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
                const double nd = dist[u] + g.edge_weight(e);
                if (nd < dist[v]) {
                    const bool first = dist[v] == kInf;
                    dist[v] = nd;
                    parent[v] = u;
                    pq.push({nd, v});
                    if (opts.trace) {
                        first ? opts.trace->discover(v, u) : opts.trace->relax(v, u);
                    }
                }
            }
        }
        res.algorithm_ms = ms_since(t0);

        if (res.success) {
            res.path = reconstruct_path(parent, source, target);
            res.path_cost = dist[target];
        }
        return res;
    }
};

class AStar : public Algorithm {
public:
    std::string name() const override { return "A* Search"; }
    std::string time_complexity() const override { return "O((V + E) log V)"; }
    std::string space_complexity() const override { return "O(V)"; }
    bool needs_coordinates() const override { return true; }

    SearchResult run(const Graph& g, NodeId source, NodeId target,
                     const SearchOptions& opts) const override {
        SearchResult res = make_result();
        if (!endpoints_valid(g, source, target)) return res;

        std::vector<double> gscore(g.num_nodes(), kInf);
        std::vector<NodeId> parent(g.num_nodes(), kInvalidNode);
        std::vector<char> settled(g.num_nodes(), 0);
        MinHeap pq;

        const auto t0 = Clock::now();
        gscore[source] = 0.0;
        pq.push({g.straight_line(source, target), source});
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
                const double tentative = gscore[u] + g.edge_weight(e);
                if (tentative < gscore[v]) {
                    const bool first = gscore[v] == kInf;
                    gscore[v] = tentative;
                    parent[v] = u;
                    pq.push({tentative + g.straight_line(v, target), v});
                    if (opts.trace) {
                        first ? opts.trace->discover(v, u) : opts.trace->relax(v, u);
                    }
                }
            }
        }
        res.algorithm_ms = ms_since(t0);

        if (res.success) {
            res.path = reconstruct_path(parent, source, target);
            res.path_cost = gscore[target];
        }
        return res;
    }
};

class Greedy : public Algorithm {
public:
    std::string name() const override { return "Greedy Best-First Search"; }
    std::string time_complexity() const override { return "O((V + E) log V)"; }
    std::string space_complexity() const override { return "O(V)"; }
    bool guarantees_optimal() const override { return false; }
    bool needs_coordinates() const override { return true; }

    SearchResult run(const Graph& g, NodeId source, NodeId target,
                     const SearchOptions& opts) const override {
        SearchResult res = make_result();
        if (!endpoints_valid(g, source, target)) return res;

        std::vector<NodeId> parent(g.num_nodes(), kInvalidNode);
        std::vector<char> settled(g.num_nodes(), 0);
        std::vector<char> queued(g.num_nodes(), 0);
        MinHeap pq;

        const auto t0 = Clock::now();
        pq.push({g.straight_line(source, target), source});
        queued[source] = 1;
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
                if (settled[v] || queued[v]) continue;
                queued[v] = 1;
                parent[v] = u;
                pq.push({g.straight_line(v, target), v});
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

/// Bidirectional Dijkstra: grow a forward search from the source and a
/// backward search from the target, stop when their settled sets touch.
///
/// The header declared a BidirectionalBFS for the whole life of the project
/// and never defined it. This is the weighted version, which is the useful
/// one. Because each side only has to reach the meeting point, the searched
/// area is roughly two half-radius balls instead of one full-radius ball --
/// a constant-factor win that grows with graph diameter.
///
/// Correctness note: it is *not* enough to stop at the first common node. The
/// shortest path may pass through a node not yet settled on either side, so we
/// track the best mu = dist_f[u] + w + dist_b[v] seen across the frontier and
/// stop only once topf + topb >= mu.
class BidirectionalDijkstra : public Algorithm {
public:
    std::string name() const override { return "Bidirectional Dijkstra"; }
    std::string time_complexity() const override { return "O((V + E) log V)"; }
    std::string space_complexity() const override { return "O(V)"; }

    SearchResult run(const Graph& g, NodeId source, NodeId target,
                     const SearchOptions& opts) const override {
        SearchResult res = make_result();
        if (!endpoints_valid(g, source, target)) return res;
        if (source == target) {
            res.success = true;
            res.path = {source};
            if (opts.trace) opts.trace->expand(source);
            return res;
        }

        // Reverse adjacency, needed by the backward search. Built once via the
        // same counting sort the forward CSR uses.
        const auto t0 = Clock::now();
        std::vector<EdgeId> roff(static_cast<std::size_t>(g.num_nodes()) + 1, 0);
        for (EdgeId e = 0; e < g.num_edges(); ++e) roff[g.edge_target(e) + 1]++;
        for (std::size_t i = 1; i < roff.size(); ++i) roff[i] += roff[i - 1];
        std::vector<NodeId> rsrc(static_cast<std::size_t>(g.num_edges()));
        std::vector<double> rw(static_cast<std::size_t>(g.num_edges()));
        std::vector<EdgeId> rid(static_cast<std::size_t>(g.num_edges()));
        {
            std::vector<EdgeId> cur(roff.begin(), roff.end() - 1);
            for (NodeId u = 0; u < g.num_nodes(); ++u) {
                for (EdgeId e = g.edge_begin(u); e < g.edge_end(u); ++e) {
                    const EdgeId slot = cur[g.edge_target(e)]++;
                    rsrc[slot] = u;
                    rw[slot] = g.edge_weight(e);
                    rid[slot] = e;
                }
            }
        }

        std::vector<double> df(g.num_nodes(), kInf), db(g.num_nodes(), kInf);
        std::vector<NodeId> pf(g.num_nodes(), kInvalidNode), pb(g.num_nodes(), kInvalidNode);
        std::vector<char> sf(g.num_nodes(), 0), sb(g.num_nodes(), 0);
        MinHeap qf, qb;

        df[source] = 0.0;
        db[target] = 0.0;
        qf.push({0.0, source});
        qb.push({0.0, target});
        if (opts.trace) {
            opts.trace->discover(source, kInvalidNode);
            opts.trace->discover(target, kInvalidNode);
        }

        double mu = kInf;
        NodeId meet = kInvalidNode;

        while (!qf.empty() && !qb.empty()) {
            if (qf.top().key + qb.top().key >= mu) break;

            // Expand whichever side currently has the smaller frontier key.
            const bool forward = qf.top().key <= qb.top().key;
            if (forward) {
                const NodeId u = qf.top().node;
                qf.pop();
                if (sf[u]) continue;
                sf[u] = 1;
                ++res.nodes_expanded;
                if (opts.trace) opts.trace->expand(u);
                for (EdgeId e = g.edge_begin(u); e < g.edge_end(u); ++e) {
                    if (!opts.edge_open(e)) continue;
                    const NodeId v = g.edge_target(e);
                    ++res.edges_relaxed;
                    const double nd = df[u] + g.edge_weight(e);
                    if (nd < df[v]) {
                        const bool first = df[v] == kInf;
                        df[v] = nd;
                        pf[v] = u;
                        qf.push({nd, v});
                        if (opts.trace)
                            first ? opts.trace->discover(v, u) : opts.trace->relax(v, u);
                    }
                    if (db[v] != kInf && df[u] + g.edge_weight(e) + db[v] < mu) {
                        mu = df[u] + g.edge_weight(e) + db[v];
                        meet = v;
                    }
                }
            } else {
                const NodeId u = qb.top().node;
                qb.pop();
                if (sb[u]) continue;
                sb[u] = 1;
                ++res.nodes_expanded;
                if (opts.trace) opts.trace->expand(u);
                for (EdgeId i = roff[u]; i < roff[u + 1]; ++i) {
                    if (!opts.edge_open(rid[i])) continue;
                    const NodeId v = rsrc[i];
                    ++res.edges_relaxed;
                    const double nd = db[u] + rw[i];
                    if (nd < db[v]) {
                        const bool first = db[v] == kInf;
                        db[v] = nd;
                        pb[v] = u;
                        qb.push({nd, v});
                        if (opts.trace)
                            first ? opts.trace->discover(v, u) : opts.trace->relax(v, u);
                    }
                    if (df[v] != kInf && df[v] + rw[i] + db[u] < mu) {
                        mu = df[v] + rw[i] + db[u];
                        meet = v;
                    }
                }
            }
        }
        res.algorithm_ms = ms_since(t0);

        if (meet != kInvalidNode && mu < kInf) {
            // Stitch: source ->...-> meet from the forward tree, then
            // meet ->...-> target by walking the backward tree's parents.
            auto front = reconstruct_path(pf, source, meet);
            if (!front.empty()) {
                res.path = std::move(front);
                NodeId curr = meet;
                std::size_t guard = 0;
                while (curr != target && guard++ <= static_cast<std::size_t>(g.num_nodes())) {
                    curr = pb[curr];
                    if (curr == kInvalidNode) break;
                    res.path.push_back(curr);
                }
                if (!res.path.empty() && res.path.back() == target) {
                    res.success = true;
                    res.path_cost = mu;
                } else {
                    res.path.clear();
                }
            }
        }
        return res;
    }
};

void register_weighted(Registry& r) {
    r.add("dijkstra", [] { return std::unique_ptr<Algorithm>(new Dijkstra()); });
    r.add("astar", [] { return std::unique_ptr<Algorithm>(new AStar()); });
    r.add("greedy", [] { return std::unique_ptr<Algorithm>(new Greedy()); });
    r.add("bidijkstra", [] { return std::unique_ptr<Algorithm>(new BidirectionalDijkstra()); });
}

}  // namespace agss
