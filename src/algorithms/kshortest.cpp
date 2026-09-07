#include "agss/kshortest.hpp"

#include <algorithm>
#include <chrono>
#include <limits>
#include <queue>
#include <set>

namespace agss {
namespace {
using Clock = std::chrono::steady_clock;
constexpr double kInf = std::numeric_limits<double>::infinity();

struct Entry {
    double key;
    NodeId node;
    bool operator>(const Entry& o) const { return key > o.key; }
};

/// Dijkstra restricted by a set of banned edges and banned nodes.
bool constrained_dijkstra(const Graph& g, NodeId source, NodeId target,
                          const std::vector<char>& banned_edge,
                          const std::vector<char>& banned_node, std::vector<NodeId>& out_path,
                          double& out_cost) {
    const auto n = static_cast<std::size_t>(g.num_nodes());
    std::vector<double> dist(n, kInf);
    std::vector<NodeId> parent(n, kInvalidNode);
    std::vector<char> settled(n, 0);
    std::priority_queue<Entry, std::vector<Entry>, std::greater<>> pq;

    dist[static_cast<std::size_t>(source)] = 0.0;
    pq.push({0.0, source});
    while (!pq.empty()) {
        const NodeId u = pq.top().node;
        pq.pop();
        if (settled[u]) continue;
        settled[u] = 1;
        if (u == target) break;
        for (EdgeId e = g.edge_begin(u); e < g.edge_end(u); ++e) {
            if (banned_edge[static_cast<std::size_t>(e)]) continue;
            const NodeId v = g.edge_target(e);
            if (banned_node[static_cast<std::size_t>(v)]) continue;
            const double nd = dist[u] + g.edge_weight(e);
            if (nd < dist[v]) {
                dist[v] = nd;
                parent[v] = u;
                pq.push({nd, v});
            }
        }
    }
    if (dist[static_cast<std::size_t>(target)] == kInf) return false;
    out_path = reconstruct_path(parent, source, target);
    out_cost = dist[static_cast<std::size_t>(target)];
    return !out_path.empty();
}
}  // namespace

KShortestReport k_shortest_paths(const Graph& g, NodeId source, NodeId target, int k) {
    KShortestReport rep;
    const auto t0 = Clock::now();
    if (k <= 0 || source < 0 || target < 0 || source >= g.num_nodes() ||
        target >= g.num_nodes()) {
        rep.elapsed_ms = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
        return rep;
    }

    std::vector<char> no_edges(static_cast<std::size_t>(g.num_edges()), 0);
    std::vector<char> no_nodes(static_cast<std::size_t>(g.num_nodes()), 0);

    std::vector<NodeId> first;
    double first_cost = 0.0;
    if (!constrained_dijkstra(g, source, target, no_edges, no_nodes, first, first_cost)) {
        rep.elapsed_ms = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
        return rep;
    }
    rep.routes.push_back({first, first_cost});

    // Candidates ordered by cost; the set also de-duplicates, since different
    // spur nodes frequently rediscover the same detour.
    std::set<std::pair<double, std::vector<NodeId>>> candidates;

    for (int round = 1; round < k; ++round) {
        const auto& prev = rep.routes.back().path;

        for (std::size_t i = 0; i + 1 < prev.size(); ++i) {
            const NodeId spur = prev[i];
            const std::vector<NodeId> root(prev.begin(), prev.begin() + static_cast<long>(i) + 1);

            std::vector<char> banned_edge = no_edges;
            std::vector<char> banned_node = no_nodes;

            // Ban the next hop of every accepted route sharing this root, so
            // the spur search is forced to diverge here.
            for (const auto& r : rep.routes) {
                if (r.path.size() > i &&
                    std::equal(root.begin(), root.end(), r.path.begin(),
                               r.path.begin() + static_cast<long>(i) + 1)) {
                    if (i + 1 < r.path.size()) {
                        const NodeId nxt = r.path[i + 1];
                        for (EdgeId e = g.edge_begin(spur); e < g.edge_end(spur); ++e) {
                            if (g.edge_target(e) == nxt) banned_edge[static_cast<std::size_t>(e)] = 1;
                        }
                    }
                }
            }
            // Ban the root's interior so the spur cannot loop back into it.
            for (std::size_t j = 0; j < i; ++j) banned_node[static_cast<std::size_t>(root[j])] = 1;

            std::vector<NodeId> spur_path;
            double spur_cost = 0.0;
            if (!constrained_dijkstra(g, spur, target, banned_edge, banned_node, spur_path,
                                      spur_cost)) {
                continue;
            }

            std::vector<NodeId> total(root.begin(), root.end() - 1);
            total.insert(total.end(), spur_path.begin(), spur_path.end());
            const double total_cost = path_cost(g, total);
            if (total_cost < 0.0) continue;

            bool already = false;
            for (const auto& r : rep.routes) {
                if (r.path == total) {
                    already = true;
                    break;
                }
            }
            if (!already) candidates.emplace(total_cost, total);
        }

        if (candidates.empty()) break;
        auto best = candidates.begin();
        rep.routes.push_back({best->second, best->first});
        candidates.erase(best);
    }

    rep.elapsed_ms = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
    return rep;
}

}  // namespace agss
