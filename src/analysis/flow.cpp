#include <algorithm>
#include <chrono>
#include <limits>
#include <queue>
#include <vector>

#include "agss/analysis.hpp"

namespace agss::analysis {
namespace {
using Clock = std::chrono::steady_clock;
inline double ms_since(Clock::time_point t0) {
    return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}
constexpr double kInf = std::numeric_limits<double>::infinity();

/// Dinic's algorithm on a residual graph held in paired arrays: edge `i` and
/// `i ^ 1` are the forward/backward halves, so pushing flow is a two-line
/// update with no lookup. O(V^2 * E) worst case, far better in practice, and
/// on unit-capacity graphs O(E * sqrt(E)).
struct Dinic {
    struct REdge {
        NodeId to;
        double cap;
    };
    std::vector<REdge> edges;
    std::vector<std::vector<int>> adj;
    std::vector<int> level, iter;

    explicit Dinic(std::size_t n) : adj(n), level(n), iter(n) {}

    void add(NodeId u, NodeId v, double cap) {
        adj[static_cast<std::size_t>(u)].push_back(static_cast<int>(edges.size()));
        edges.push_back({v, cap});
        adj[static_cast<std::size_t>(v)].push_back(static_cast<int>(edges.size()));
        edges.push_back({u, 0.0});
    }

    bool bfs(NodeId s, NodeId t) {
        std::fill(level.begin(), level.end(), -1);
        std::queue<NodeId> q;
        level[static_cast<std::size_t>(s)] = 0;
        q.push(s);
        while (!q.empty()) {
            const NodeId u = q.front();
            q.pop();
            for (int id : adj[static_cast<std::size_t>(u)]) {
                const auto& e = edges[static_cast<std::size_t>(id)];
                if (e.cap > 1e-12 && level[static_cast<std::size_t>(e.to)] < 0) {
                    level[static_cast<std::size_t>(e.to)] = level[static_cast<std::size_t>(u)] + 1;
                    q.push(e.to);
                }
            }
        }
        return level[static_cast<std::size_t>(t)] >= 0;
    }

    double dfs(NodeId u, NodeId t, double pushed) {
        if (u == t) return pushed;
        for (int& i = iter[static_cast<std::size_t>(u)];
             i < static_cast<int>(adj[static_cast<std::size_t>(u)].size()); ++i) {
            const int id = adj[static_cast<std::size_t>(u)][static_cast<std::size_t>(i)];
            auto& e = edges[static_cast<std::size_t>(id)];
            if (e.cap <= 1e-12 ||
                level[static_cast<std::size_t>(e.to)] != level[static_cast<std::size_t>(u)] + 1)
                continue;
            const double d = dfs(e.to, t, std::min(pushed, e.cap));
            if (d > 1e-12) {
                e.cap -= d;
                edges[static_cast<std::size_t>(id) ^ 1].cap += d;
                return d;
            }
        }
        return 0.0;
    }

    double run(NodeId s, NodeId t) {
        double flow = 0.0;
        while (bfs(s, t)) {
            std::fill(iter.begin(), iter.end(), 0);
            while (const double f = dfs(s, t, kInf)) {
                if (f <= 1e-12) break;
                flow += f;
            }
        }
        return flow;
    }
};
}  // namespace

FlowReport max_flow(const Graph& g, NodeId source, NodeId sink) {
    FlowReport rep;
    const auto t0 = Clock::now();
    if (source < 0 || sink < 0 || source >= g.num_nodes() || sink >= g.num_nodes() ||
        source == sink) {
        rep.elapsed_ms = ms_since(t0);
        return rep;
    }

    Dinic din(static_cast<std::size_t>(g.num_nodes()));
    std::vector<std::pair<NodeId, NodeId>> ends;
    for (NodeId u = 0; u < g.num_nodes(); ++u) {
        for (EdgeId e = g.edge_begin(u); e < g.edge_end(u); ++e) {
            din.add(u, g.edge_target(e), g.edge_weight(e));
            ends.emplace_back(u, g.edge_target(e));
        }
    }
    rep.max_flow = din.run(source, sink);

    // Min cut = edges from the source-reachable residual side to the rest.
    din.bfs(source, sink);
    for (std::size_t i = 0; i < ends.size(); ++i) {
        const auto [u, v] = ends[i];
        if (din.level[static_cast<std::size_t>(u)] >= 0 &&
            din.level[static_cast<std::size_t>(v)] < 0) {
            rep.min_cut.emplace_back(u, v);
        }
    }
    rep.elapsed_ms = ms_since(t0);
    return rep;
}

}  // namespace agss::analysis
