#include "agss/customizable_ch.hpp"

#include <algorithm>
#include <chrono>
#include <limits>
#include <memory>
#include <queue>

namespace agss {
namespace {

using Clock = std::chrono::steady_clock;
constexpr double kInf = std::numeric_limits<double>::infinity();

inline std::uint64_t pair_key(NodeId a, NodeId b) {
    if (a > b) std::swap(a, b);
    return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(a)) << 32) |
           static_cast<std::uint32_t>(b);
}

struct HeapItem {
    double key;
    NodeId node;
    bool operator>(const HeapItem& o) const { return key > o.key; }
};
using MinHeap = std::priority_queue<HeapItem, std::vector<HeapItem>, std::greater<>>;

}  // namespace

std::int64_t CustomizableCH::edge_index(NodeId a, NodeId b) const {
    auto it = lookup_.find(pair_key(a, b));
    return it == lookup_.end() ? -1 : it->second;
}

void CustomizableCH::build(const Graph& g, const BuildOptions& opts) {
    const auto t0 = Clock::now();
    graph_ = &g;
    built_ = false;
    customized_ = false;
    stats_ = Stats{};
    edges_.clear();
    lookup_.clear();

    const auto n = static_cast<std::size_t>(g.num_nodes());
    level_.assign(n, 0);
    up_.assign(n, {});
    if (n == 0) {
        built_ = true;
        return;
    }

    // Undirected working adjacency. Direction is irrelevant here: the shape of
    // the chordal graph depends only on which nodes are adjacent, which is
    // precisely what makes this step metric-independent.
    std::vector<std::vector<NodeId>> adj(n);
    {
        std::vector<std::pair<NodeId, NodeId>> pairs;
        pairs.reserve(static_cast<std::size_t>(g.num_edges()));
        for (NodeId u = 0; u < g.num_nodes(); ++u) {
            for (EdgeId e = g.edge_begin(u); e < g.edge_end(u); ++e) {
                const NodeId v = g.edge_target(e);
                if (u != v) pairs.emplace_back(std::min(u, v), std::max(u, v));
            }
        }
        std::sort(pairs.begin(), pairs.end());
        pairs.erase(std::unique(pairs.begin(), pairs.end()), pairs.end());
        for (const auto& [a, b] : pairs) {
            adj[a].push_back(b);
            adj[b].push_back(a);
        }
    }

    std::vector<char> contracted(n, 0);
    auto live_degree = [&](NodeId v) {
        int d = 0;
        for (NodeId w : adj[v]) d += contracted[w] ? 0 : 1;
        return d;
    };

    // Minimum-degree ordering. Nested dissection gives better hierarchies on
    // road networks, but needs a graph partitioner; minimum degree is the
    // classic sparse-matrix heuristic, needs nothing, and is the same idea:
    // contract wherever it creates the fewest new arcs.
    MinHeap pq;
    for (NodeId v = 0; v < g.num_nodes(); ++v) pq.push({static_cast<double>(live_degree(v)), v});

    const auto deadline = t0 + std::chrono::duration_cast<Clock::duration>(
                                   std::chrono::duration<double, std::milli>(opts.budget_ms));
    std::int32_t rank = 0;
    int clock_check = 0;
    std::vector<NodeId> higher;

    while (!pq.empty()) {
        if (++clock_check >= 128) {
            clock_check = 0;
            if (Clock::now() > deadline) {
                stats_.aborted = true;
                break;
            }
        }
        const NodeId v = pq.top().node;
        const double key = pq.top().key;
        pq.pop();
        if (contracted[v]) continue;
        const double fresh = static_cast<double>(live_degree(v));
        if (!pq.empty() && fresh > pq.top().key && fresh > key) {
            pq.push({fresh, v});
            continue;
        }

        contracted[v] = 1;
        level_[v] = rank++;

        // Every pair of surviving neighbours becomes adjacent. No witness
        // search: that is what keeps this independent of the weights, and what
        // guarantees the chordal property customization relies on.
        higher.clear();
        for (NodeId w : adj[v]) {
            if (!contracted[w]) higher.push_back(w);
        }
        for (std::size_t i = 0; i < higher.size(); ++i) {
            for (std::size_t j = i + 1; j < higher.size(); ++j) {
                const NodeId a = higher[i], b = higher[j];
                if (std::find(adj[a].begin(), adj[a].end(), b) == adj[a].end()) {
                    adj[a].push_back(b);
                    adj[b].push_back(a);
                }
            }
        }
    }

    if (stats_.aborted) {
        for (NodeId v = 0; v < g.num_nodes(); ++v) {
            if (!contracted[v]) level_[v] = rank++;
        }
    }

    // Materialise the chordal edge set, oriented low level -> high level.
    for (NodeId a = 0; a < g.num_nodes(); ++a) {
        for (NodeId b : adj[a]) {
            if (level_[a] >= level_[b]) continue;
            const auto key = pair_key(a, b);
            if (lookup_.count(key)) continue;
            const auto idx = static_cast<std::int32_t>(edges_.size());
            lookup_.emplace(key, idx);
            edges_.push_back({a, b, kInf, kInf, kInvalidNode, kInvalidNode});
            up_[a].push_back(idx);
        }
    }

    stats_.chordal_edges = static_cast<std::int64_t>(edges_.size());
    stats_.original_edges = g.num_edges();
    stats_.edge_growth =
        g.num_edges() > 0 ? 2.0 * static_cast<double>(edges_.size()) / g.num_edges() : 1.0;
    stats_.build_ms = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
    built_ = true;
}

void CustomizableCH::customize() {
    if (graph_ == nullptr) return;
    std::vector<double> w(static_cast<std::size_t>(graph_->num_edges()));
    for (EdgeId e = 0; e < graph_->num_edges(); ++e) {
        w[static_cast<std::size_t>(e)] = graph_->edge_weight(e);
    }
    customize(w);
}

void CustomizableCH::customize(const std::vector<double>& weight_of_edge) {
    if (!built_ || graph_ == nullptr) return;
    const auto t0 = Clock::now();
    const Graph& g = *graph_;

    for (auto& e : edges_) {
        e.fwd = kInf;
        e.bwd = kInf;
        e.fwd_via = kInvalidNode;
        e.bwd_via = kInvalidNode;
    }

    // Seed with the real arcs.
    for (NodeId u = 0; u < g.num_nodes(); ++u) {
        for (EdgeId e = g.edge_begin(u); e < g.edge_end(u); ++e) {
            const NodeId v = g.edge_target(e);
            if (u == v) continue;
            const auto idx = edge_index(u, v);
            if (idx < 0) continue;
            auto& ce = edges_[static_cast<std::size_t>(idx)];
            const double w = weight_of_edge[static_cast<std::size_t>(e)];
            if (ce.lo == u) {
                ce.fwd = std::min(ce.fwd, w);
            } else {
                ce.bwd = std::min(ce.bwd, w);
            }
        }
    }

    // Lower-triangle enumeration, in contraction order. For a triangle
    // v < x < y, a route x -> v -> y is a candidate for the arc x -> y, and
    // y -> v -> x for y -> x. Processing v before x and y means the arcs at v
    // are already final, so one pass suffices.
    std::vector<NodeId> order(static_cast<std::size_t>(g.num_nodes()));
    for (NodeId v = 0; v < g.num_nodes(); ++v) order[static_cast<std::size_t>(level_[v])] = v;

    std::int64_t triangles = 0;
    for (NodeId v : order) {
        const auto& inc = up_[static_cast<std::size_t>(v)];
        for (std::size_t i = 0; i < inc.size(); ++i) {
            const auto& ev = edges_[static_cast<std::size_t>(inc[i])];
            const NodeId x = ev.hi;
            for (std::size_t j = 0; j < inc.size(); ++j) {
                if (i == j) continue;
                const auto& ew = edges_[static_cast<std::size_t>(inc[j])];
                const NodeId y = ew.hi;
                if (level_[x] >= level_[y]) continue;  // orient the triangle

                const auto idx = edge_index(x, y);
                if (idx < 0) continue;  // only possible on an aborted build
                ++triangles;
                auto& target = edges_[static_cast<std::size_t>(idx)];

                // x -> v uses the arc back down to v, then v -> y goes up.
                const double via_fwd = ev.bwd + ew.fwd;
                if (via_fwd < target.fwd) {
                    target.fwd = via_fwd;
                    target.fwd_via = v;
                }
                const double via_bwd = ew.bwd + ev.fwd;
                if (via_bwd < target.bwd) {
                    target.bwd = via_bwd;
                    target.bwd_via = v;
                }
            }
        }
    }

    stats_.triangles = triangles;
    stats_.customize_ms = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
    customized_ = true;
}

void CustomizableCH::unpack_forward(NodeId from, NodeId to, std::vector<NodeId>& out) const {
    const auto idx = edge_index(from, to);
    if (idx < 0) {
        out.push_back(to);
        return;
    }
    const auto& e = edges_[static_cast<std::size_t>(idx)];
    const NodeId via = (e.lo == from) ? e.fwd_via : e.bwd_via;
    if (via == kInvalidNode) {
        out.push_back(to);
        return;
    }
    unpack_forward(from, via, out);
    unpack_forward(via, to, out);
}

void CustomizableCH::unpack_backward(NodeId from, NodeId to, std::vector<NodeId>& out) const {
    unpack_forward(from, to, out);
}

SearchResult CustomizableCH::query(NodeId source, NodeId target, const SearchOptions& opts) const {
    SearchResult res;
    res.algorithm = "Customizable Contraction Hierarchies";
    res.time_complexity = "O(k log k), k << V";
    res.space_complexity = "O(V + chordal edges)";
    if (!ready() || graph_ == nullptr) return res;

    const auto n = graph_->num_nodes();
    if (source < 0 || target < 0 || source >= n || target >= n) return res;
    if (source == target) {
        res.success = true;
        res.path = {source};
        return res;
    }

    const auto t0 = Clock::now();
    const auto sz = static_cast<std::size_t>(n);
    std::vector<double> df(sz, kInf), db(sz, kInf);
    std::vector<NodeId> pf(sz, kInvalidNode), pb(sz, kInvalidNode);
    std::vector<char> sf(sz, 0), sb(sz, 0);
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

    while (!qf.empty() || !qb.empty()) {
        if (!qf.empty() && qf.top().key <= mu) {
            const NodeId u = qf.top().node;
            const double d = qf.top().key;
            qf.pop();
            if (!sf[u] && d <= df[u]) {
                sf[u] = 1;
                ++res.nodes_expanded;
                if (opts.trace) opts.trace->expand(u);
                if (db[u] != kInf && df[u] + db[u] < mu) {
                    mu = df[u] + db[u];
                    meet = u;
                }
                for (auto idx : up_[static_cast<std::size_t>(u)]) {
                    const auto& e = edges_[static_cast<std::size_t>(idx)];
                    const NodeId v = e.hi;
                    const double w = e.fwd;
                    ++res.edges_relaxed;
                    if (w == kInf) continue;
                    if (df[u] + w < df[v]) {
                        const bool first = df[v] == kInf;
                        df[v] = df[u] + w;
                        pf[v] = u;
                        qf.push({df[v], v});
                        if (opts.trace)
                            first ? opts.trace->discover(v, u) : opts.trace->relax(v, u);
                    }
                }
            }
        } else if (!qb.empty() && qb.top().key <= mu) {
            const NodeId u = qb.top().node;
            const double d = qb.top().key;
            qb.pop();
            if (!sb[u] && d <= db[u]) {
                sb[u] = 1;
                ++res.nodes_expanded;
                if (opts.trace) opts.trace->expand(u);
                if (df[u] != kInf && df[u] + db[u] < mu) {
                    mu = df[u] + db[u];
                    meet = u;
                }
                for (auto idx : up_[static_cast<std::size_t>(u)]) {
                    const auto& e = edges_[static_cast<std::size_t>(idx)];
                    const NodeId v = e.hi;
                    const double w = e.bwd;  // arriving at u from above
                    ++res.edges_relaxed;
                    if (w == kInf) continue;
                    if (db[u] + w < db[v]) {
                        const bool first = db[v] == kInf;
                        db[v] = db[u] + w;
                        pb[v] = u;
                        qb.push({db[v], v});
                        if (opts.trace)
                            first ? opts.trace->discover(v, u) : opts.trace->relax(v, u);
                    }
                }
            }
        } else {
            break;
        }
    }
    res.algorithm_ms = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();

    if (meet == kInvalidNode || mu == kInf) return res;

    std::vector<NodeId> up_half;
    for (NodeId c = meet; c != kInvalidNode && c != source; c = pf[c]) up_half.push_back(c);
    std::reverse(up_half.begin(), up_half.end());

    res.path.push_back(source);
    NodeId prev = source;
    for (NodeId nxt : up_half) {
        unpack_forward(prev, nxt, res.path);
        prev = nxt;
    }
    for (NodeId c = pb[meet]; c != kInvalidNode; c = pb[c]) {
        unpack_forward(prev, c, res.path);
        prev = c;
        if (c == target) break;
    }

    if (!res.path.empty() && res.path.front() == source && res.path.back() == target) {
        res.success = true;
        res.path_cost = mu;
    } else {
        res.path.clear();
    }
    return res;
}

}  // namespace agss
