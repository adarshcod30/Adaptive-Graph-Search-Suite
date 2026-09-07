#include <algorithm>
#include <chrono>
#include <vector>

#include "agss/analysis.hpp"

namespace agss::analysis {
namespace {
using Clock = std::chrono::steady_clock;
inline double ms_since(Clock::time_point t0) {
    return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

/// Undirected adjacency with a stable edge id per undirected pair, so the two
/// directions of one road are not mistaken for a 2-cycle (which would hide
/// every bridge in a bidirectional road network).
struct UndirectedView {
    std::vector<std::vector<std::pair<NodeId, std::int64_t>>> adj;
    std::vector<std::pair<NodeId, NodeId>> edge_ends;
};

UndirectedView build_undirected(const Graph& g) {
    UndirectedView uv;
    uv.adj.resize(static_cast<std::size_t>(g.num_nodes()));
    std::vector<std::pair<NodeId, NodeId>> pairs;
    pairs.reserve(static_cast<std::size_t>(g.num_edges()));
    for (NodeId u = 0; u < g.num_nodes(); ++u) {
        for (EdgeId e = g.edge_begin(u); e < g.edge_end(u); ++e) {
            const NodeId v = g.edge_target(e);
            if (u == v) continue;
            pairs.emplace_back(std::min(u, v), std::max(u, v));
        }
    }
    std::sort(pairs.begin(), pairs.end());
    pairs.erase(std::unique(pairs.begin(), pairs.end()), pairs.end());
    uv.edge_ends = pairs;
    for (std::size_t i = 0; i < pairs.size(); ++i) {
        uv.adj[pairs[i].first].emplace_back(pairs[i].second, static_cast<std::int64_t>(i));
        uv.adj[pairs[i].second].emplace_back(pairs[i].first, static_cast<std::int64_t>(i));
    }
    return uv;
}
}  // namespace

ConnectivityReport find_bridges(const Graph& g) {
    ConnectivityReport rep;
    const auto t0 = Clock::now();
    const auto n = static_cast<std::size_t>(g.num_nodes());
    if (n == 0) {
        rep.elapsed_ms = ms_since(t0);
        return rep;
    }

    const UndirectedView uv = build_undirected(g);
    std::vector<int> disc(n, -1), low(n, 0), subtree(n, 1);
    std::vector<char> is_ap(n, 0);
    int timer = 0;

    // Iterative Tarjan. A recursive version overflows the stack on real city
    // graphs, where DFS depth routinely reaches tens of thousands.
    struct Frame {
        NodeId u;
        std::int64_t parent_edge;
        std::size_t next;
        int children;
    };
    std::vector<Frame> stack;

    for (NodeId root = 0; root < g.num_nodes(); ++root) {
        if (disc[root] != -1) continue;
        ++rep.component_count;
        stack.push_back({root, -1, 0, 0});
        disc[root] = low[root] = timer++;

        while (!stack.empty()) {
            Frame& f = stack.back();
            if (f.next < uv.adj[f.u].size()) {
                const auto [v, eid] = uv.adj[f.u][f.next++];
                if (eid == f.parent_edge) continue;  // don't cross back on the same road
                if (disc[v] == -1) {
                    disc[v] = low[v] = timer++;
                    ++f.children;
                    stack.push_back({v, eid, 0, 0});
                } else {
                    low[f.u] = std::min(low[f.u], disc[v]);
                }
            } else {
                const Frame done = stack.back();
                stack.pop_back();
                if (stack.empty()) {
                    // Root is an articulation point iff it has >1 DFS child.
                    if (done.children > 1) is_ap[done.u] = 1;
                    break;
                }
                Frame& p = stack.back();
                low[p.u] = std::min(low[p.u], low[done.u]);
                subtree[p.u] += subtree[done.u];
                if (low[done.u] > disc[p.u]) {
                    rep.bridges.push_back(
                        {p.u, done.u, static_cast<std::int64_t>(subtree[done.u])});
                }
                if (low[done.u] >= disc[p.u] && stack.size() > 1) is_ap[p.u] = 1;
            }
        }
    }

    for (NodeId u = 0; u < g.num_nodes(); ++u) {
        if (is_ap[u]) rep.articulation_points.push_back(u);
    }
    rep.elapsed_ms = ms_since(t0);
    return rep;
}

SccReport strongly_connected_components(const Graph& g) {
    SccReport rep;
    const auto t0 = Clock::now();
    const auto n = static_cast<std::size_t>(g.num_nodes());
    rep.component_of.assign(n, kInvalidNode);
    if (n == 0) {
        rep.elapsed_ms = ms_since(t0);
        return rep;
    }

    std::vector<int> index(n, -1), low(n, 0);
    std::vector<char> on_stack(n, 0);
    std::vector<NodeId> comp_stack;
    int counter = 0;

    struct Frame {
        NodeId u;
        EdgeId next;
    };
    std::vector<Frame> call;

    for (NodeId root = 0; root < g.num_nodes(); ++root) {
        if (index[root] != -1) continue;
        call.push_back({root, g.edge_begin(root)});
        index[root] = low[root] = counter++;
        comp_stack.push_back(root);
        on_stack[root] = 1;

        while (!call.empty()) {
            Frame& f = call.back();
            if (f.next < g.edge_end(f.u)) {
                const NodeId v = g.edge_target(f.next++);
                if (index[v] == -1) {
                    index[v] = low[v] = counter++;
                    comp_stack.push_back(v);
                    on_stack[v] = 1;
                    call.push_back({v, g.edge_begin(v)});
                } else if (on_stack[v]) {
                    low[f.u] = std::min(low[f.u], index[v]);
                }
            } else {
                const NodeId u = f.u;
                call.pop_back();
                if (low[u] == index[u]) {
                    std::int64_t size = 0;
                    while (true) {
                        const NodeId w = comp_stack.back();
                        comp_stack.pop_back();
                        on_stack[w] = 0;
                        rep.component_of[w] = static_cast<NodeId>(rep.count);
                        ++size;
                        if (w == u) break;
                    }
                    rep.sizes.push_back(size);
                    ++rep.count;
                }
                if (!call.empty()) low[call.back().u] = std::min(low[call.back().u], low[u]);
            }
        }
    }
    rep.elapsed_ms = ms_since(t0);
    return rep;
}

}  // namespace agss::analysis
